/**
 * ============================================================
 *  ESP32 Bluetooth Music Player v1.0.0
 *  RYNE Engine — Multi-Signal Vibe Estimation
 *
 *  Kompatibel dengan:
 *    - ESP32-CAM AI Thinker (dengan PSRAM 1MB — ring buffer 768KB)
 *    - ESP32 biasa tanpa PSRAM   (ring buffer fallback 64KB,
 *      performa lebih terbatas tapi tetap bisa jalan)
 *
 *  Fitur utama:
 *    - Bluetooth A2DP Source → stream audio ke speaker BT
 *    - Putar MP3 & WAV dari SD Card
 *    - RYNE Engine: observasi real-time buffer, decode,
 *      playback, BT, dan vibe user multi-signal
 *    - Multi-signal vibe: skip rate + volume trend +
 *      repeat pattern + audio loudness
 *    - Equalizer 5 preset (Flat/Bass/Pop/Rock/Jazz)
 *    - Crossfade antar lagu
 *    - AVRCP remote control
 *    - BT Scanner & TX Power adjustment
 *    - OLED 128x64 display dengan animated face
 *    - Sleep timer, shuffle, repeat, mute
 *
 *  FIX v1.0.0:
 *    - Track end terlalu cepat (silence guard 172 frame)
 *    - Pesan "vibe check: ok" dihapus, diganti 32 pesan variatif
 *    - Typo UVIBE_FOCUSED → UVIBE_FOKUS
 *
 *  Hardware minimum:
 *    - ESP32 (any variant dengan Bluetooth Classic)
 *    - SD Card (FAT32, berisi file .mp3 / .wav di root)
 *    - OLED SSD1306 128x64 I2C
 *    - 4–5 tombol (BOOT + A/B/C/D)
 *
 *  Catatan ESP32 tanpa PSRAM:
 *    - Ring buffer otomatis fallback ke heap 64KB
 *    - Decode chunk tetap 4KB
 *    - Disarankan pakai file MP3 bitrate rendah (128kbps)
 *      agar buffer tidak sering underrun
 *    - WAV file besar mungkin kurang stabil tanpa PSRAM
 * ============================================================
 */
#include "Arduino.h"
#include "SD_MMC.h"
#include "FS.h"
#include <vector>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_avrc_api.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "BluetoothA2DPSource.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include "freertos/semphr.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

// ----------------------------------------------------------
// KONFIGURASI
// ----------------------------------------------------------
#define BT_DEFAULT_NAME       "HUOREI-BS10"
#define OLED_SDA_PIN          21
#define OLED_SCL_PIN          22
#define OLED_WIDTH            128
#define OLED_HEIGHT           64
#define OLED_ADDR             0x3C
#define BTN_BOOT              0
#define BTN_A                 19
#define BTN_B                 23
#define BTN_C                 1
#define BTN_D                 3
#define LONG_PRESS_MS         800
#define COMBO_WINDOW_MS       80
#define DEBOUNCE_MS           20
#define VOL_STEP              8
#define VOL_MIN               0
#define VOL_MAX               127
#define VOL_DEFAULT           80
#define TICKER_INTERVAL_MS    260
#define TICKER_MAX_CHARS      21
#define BT_RECONNECT_MS       8000
#define OLED_DIM_TIMEOUT_MS   30000
#define SLEEP_TIMER_MIN       30
#define RING_BUF_SIZE         (768*1024)
#define RING_BUF_FALLBACK     (64*1024)
#define DECODE_CHUNK          (4*1024)
#define BT_SCAN_DURATION      8
#define BT_MAX_DEVICES        16
#define OLED_LIST_ROWS        4
#define TX_GAIN_DEFAULT       7
#define TX_GAIN_LEVELS        8
#define CROSSFADE_STEPS       64
#define VOL_SAVE_DEBOUNCE_MS  1500
#define DECODE_TASK_STACK     32768
#define CONFIRM_DISPLAY_MS    1500
#define STOP_TIMEOUT_MS       5000
#define STOP_SETTLE_MS        150
#define FACE_UPDATE_INTERVAL  400
#define DRAIN_MIN_BYTES       (8*1024)
#define BT_PREFILL_BYTES      (32*1024)

// Track end guard — berapa banyak silence frame sebelum benar-benar dianggap selesai
// Ini fix bug "lagu selesai terlalu cepat"
#define TRACK_END_SILENCE_FRAMES  172   // ~1 detik pada 44100Hz stereo 16bit
                                        // 44100 frames/s ÷ frameCount-per-cb
                                        // dikali ~4 callback = aman

// RYNE config
#define RYNE_INTERVAL_MS        500
#define RYNE_STUCK_MS           3000
#define RYNE_SKIP_STORM_COUNT   3
#define RYNE_SKIP_STORM_MS      15000
#define RYNE_BT_UNSTABLE_COUNT  2
#define RYNE_BT_UNSTABLE_MS     60000

// RYNE message
#define RYNE_MSG_DURATION_MS    3500
#define RYNE_TICKER_INTERVAL_MS 180

// Volume overlay
#define VOL_OVERLAY_MS          2000

// Vibe thresholds
#define VIBE_ENERGETIC_THRESHOLD   9000.f
#define VIBE_QUIET_THRESHOLD       1800.f
#define VIBE_CALM_THRESHOLD        4000.f

// Multi-signal vibe
#define VIBE_SKIP_BORED_COUNT      3      // skip >= N dalam window = bosen
#define VIBE_SKIP_WINDOW_MS        120000 // 2 menit
#define VIBE_REPEAT_NOSTALGIC_MS   60000  // repeat one > 60 detik = nostalgia
#define VIBE_LISTEN_ENGAGED_MS     90000  // dengerin > 90 detik tanpa skip = engaged
#define VIBE_VOL_CHANGE_WINDOW_MS  30000  // window pantau perubahan volume
#define VIBE_VOL_EXCITED_CHANGES   3      // vol naik >= N kali = excited

// ----------------------------------------------------------
// ENUM
// ----------------------------------------------------------
enum RepeatMode { REPEAT_OFF=0, REPEAT_ALL, REPEAT_ONE };
enum AppMode {
  MODE_PLAYER=0, MODE_EXPERIMENTAL_MENU,
  MODE_BT_SCAN, MODE_TX_GAIN, MODE_EQUALIZER
};
enum DecodeState {
  DECODE_IDLE=0, DECODE_RUNNING, DECODE_STOP_REQ, DECODE_STOPPED
};
enum FaceType {
  FACE_NEUTRAL=0, FACE_HAPPY, FACE_SAD, FACE_SHOCK,
  FACE_SLEEPY, FACE_SMIRK, FACE_FRUSTRATED, FACE_CHILL,
  FACE_COUNT
};

// Vibe user kategori
enum UserVibeCategory {
  UVIBE_UNKNOWN=0,
  UVIBE_SEMANGAT,    // energik, vol naik, tidak skip
  UVIBE_SENDU,       // lagu pelan, repeat, malam
  UVIBE_BOSEN,       // sering skip
  UVIBE_NOSTALGIK,   // repeat one lama
  UVIBE_FOKUS,       // dengerin lama tanpa skip
  UVIBE_GELISAH,     // skip + vol naik turun
  UVIBE_SANTAI,      // calm audio, tidak banyak interaksi
  UVIBE_EXCITED,     // vol sering naik, lagu keras
};

// ----------------------------------------------------------
// RYNE SIGNALS
// ----------------------------------------------------------
struct RyneSignals {
  bool bufferHealthy;
  bool bufferCritical;
  bool bufferUnderrun;
  bool decodeActive;
  bool decodeStuck;
  bool decodeEOF;
  bool playbackStuck;
  bool skipStorm;
  bool trackEnding;
  bool btConnected;
  bool btUnstable;
  bool btReconnecting;
  bool vibeEnergetic;
  bool vibeCalm;
  bool vibeQuiet;
  UserVibeCategory userVibe;
  uint32_t lastUpdateMs;
};

struct RyneOutput {
  char     faceMsg[32];
  FaceType faceSuggested;
  bool     hasNewMsg;
  uint32_t msgTimestamp;
};

static RyneSignals   ryne           = {};
static RyneOutput    ryneOut        = {};
static TaskHandle_t  ryneTaskHandle = nullptr;

// Internal RYNE tracking
static size_t   ryneLastDecoded   = 0;
static size_t   ryneLastPlayed    = 0;
static uint32_t ryneDecodeCheckMs = 0;
static uint32_t rynePlayCheckMs   = 0;
static uint32_t ryneSkipTimes[RYNE_SKIP_STORM_COUNT+1] = {};
static int      ryneSkipCount     = 0;
static int      ryneBTDropCount   = 0;
static uint32_t ryneBTDropMs      = 0;

// Vibe audio
volatile uint64_t vibeAccum        = 0;
volatile uint32_t vibeSampleCount  = 0;
static float      vibeSmooth       = 0.f;

// Multi-signal vibe tracking
static uint32_t vibeSkipTimestamps[VIBE_SKIP_BORED_COUNT+2] = {};
static int      vibeSkipWindowCount  = 0;
static uint32_t vibeCurrentTrackStart= 0;  // kapan track ini mulai diputar
static uint32_t vibeLastSkipMs       = 0;
static int      vibeVolUpCount       = 0;  // berapa kali vol naik dalam window
static int      vibeVolDownCount     = 0;
static uint32_t vibeVolWindowStart   = 0;
static int      vibeLastVolume       = VOL_DEFAULT;
static uint32_t vibeRepeatOneStart   = 0;  // kapan mulai repeat one track ini
static UserVibeCategory vibeUserCurrent = UVIBE_UNKNOWN;
static uint32_t vibeLastCategoryMs  = 0;   // kapan terakhir kategori berubah

// RYNE face msg state
static bool     ryneMsgActive     = false;
static uint32_t ryneMsgEndMs      = 0;

// RYNE message ticker
static int      ryneTickerOffset  = 0;
static uint32_t ryneTickerLastMs  = 0;

// Volume overlay
static bool     volOverlayActive  = false;
static uint32_t volOverlayEndMs   = 0;

// ----------------------------------------------------------
// STRUCT
// ----------------------------------------------------------
struct BTDevice {
  char    name[64];
  uint8_t bda[6];
  int     rssi;
  bool    hasName;
};
struct DecoderBundle {
  AudioDecoder*       codec;
  EncodedAudioStream* stream;
  bool                isMp3;
};
struct EmotionalState {
  FaceType    face;
  const char* msg;
};

// ----------------------------------------------------------
// BITMAP FACES 16x16
// ----------------------------------------------------------
static const uint8_t PROGMEM face_neutral[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10011110,0b01111001,
  0b10011110,0b01111001,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10111110,0b01111101,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
static const uint8_t PROGMEM face_happy[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10111110,0b01111101,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10100000,0b00000101,
  0b10011111,0b11111001,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
static const uint8_t PROGMEM face_sad[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10110000,0b00001101,
  0b10011110,0b01111001,
  0b10011110,0b01111001,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10000111,0b11100001,
  0b10001000,0b00010001,
  0b10000000,0b00000001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
static const uint8_t PROGMEM face_shock[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10011110,0b01111001,
  0b10111111,0b11111101,
  0b10111111,0b11111101,
  0b10011110,0b01111001,
  0b10000000,0b00000001,
  0b10000111,0b11100001,
  0b10001000,0b00010001,
  0b10000111,0b11100001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
static const uint8_t PROGMEM face_sleepy[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10111110,0b01111101,
  0b10011110,0b01111001,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10000110,0b01100001,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
static const uint8_t PROGMEM face_smirk[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10111110,0b01111001,
  0b10000000,0b01111001,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b10000001,0b11110001,
  0b10000000,0b00010001,
  0b10000000,0b00000001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
static const uint8_t PROGMEM face_frustrated[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10001100,0b00110001,
  0b10011110,0b01111001,
  0b10011110,0b01111001,
  0b10000000,0b00000001,
  0b10100000,0b00000101,
  0b10011111,0b11111001,
  0b10000000,0b00000001,
  0b10000000,0b00000001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
static const uint8_t PROGMEM face_chill[] = {
  0b00111111,0b11111100,
  0b01000000,0b00000010,
  0b10000000,0b00000001,
  0b10011110,0b01111001,
  0b10111111,0b11111101,
  0b10011110,0b01111001,
  0b10000110,0b01100001,
  0b10000000,0b00000001,
  0b10100000,0b00000101,
  0b10011110,0b01111001,
  0b10000000,0b00000001,
  0b01000000,0b00000010,
  0b00111111,0b11111100,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};

// ----------------------------------------------------------
// TX GAIN
// ----------------------------------------------------------
static const char* txGainLabel[TX_GAIN_LEVELS]={
  "-12dBm","-9dBm","-6dBm","-3dBm",
  "  0dBm","+3dBm","+6dBm","+9dBm"
};
static const esp_power_level_t txGainEnum[TX_GAIN_LEVELS]={
  ESP_PWR_LVL_N12,ESP_PWR_LVL_N9,ESP_PWR_LVL_N6,ESP_PWR_LVL_N3,
  ESP_PWR_LVL_N0, ESP_PWR_LVL_P3,ESP_PWR_LVL_P6,ESP_PWR_LVL_P9
};

// ----------------------------------------------------------
// EQUALIZER
// ----------------------------------------------------------
#define EQ_PRESETS 5
static const char* eqPresetName[EQ_PRESETS]={
  "Flat","Bass","Pop","Rock","Jazz"
};
static const int8_t eqPresetGain[EQ_PRESETS][3]={
  {0,0,0},{8,0,-2},{3,2,3},{5,-2,4},{4,2,-1}
};
struct BiquadState { float x1,x2,y1,y2; };
struct BiquadCoef  { float b0,b1,b2,a1,a2; };
static BiquadCoef  eqCoefL[3],eqCoefR[3];
static BiquadState eqStateL[3],eqStateR[3];

void calcLowShelf(BiquadCoef& c,float gDB,float fc,float fs){
  float A=powf(10.f,gDB/40.f),w0=2.f*M_PI*fc/fs,cw=cosf(w0),sw=sinf(w0);
  float al=sw/2.f*sqrtf((A+1.f/A)*(1.f)+2.f);
  if(isnan(al)||al<=0.f)al=0.1f;
  float a0=(A+1)+(A-1)*cw+2*sqrtf(A)*al;
  if(fabsf(a0)<1e-10f){c.b0=1;c.b1=0;c.b2=0;c.a1=0;c.a2=0;return;}
  c.b0=A*((A+1)-(A-1)*cw+2*sqrtf(A)*al)/a0;
  c.b1=2*A*((A-1)-(A+1)*cw)/a0;
  c.b2=A*((A+1)-(A-1)*cw-2*sqrtf(A)*al)/a0;
  c.a1=-2*((A-1)+(A+1)*cw)/a0;
  c.a2=((A+1)+(A-1)*cw-2*sqrtf(A)*al)/a0;
}
void calcPeaking(BiquadCoef& c,float gDB,float fc,float fs,float Q){
  float A=powf(10.f,gDB/40.f),w0=2.f*M_PI*fc/fs,cw=cosf(w0),sw=sinf(w0);
  float al=sw/(2.f*Q);if(al<=0.f)al=0.01f;
  float a0=1.f+al/A;
  if(fabsf(a0)<1e-10f){c.b0=1;c.b1=0;c.b2=0;c.a1=0;c.a2=0;return;}
  c.b0=(1.f+al*A)/a0;c.b1=(-2.f*cw)/a0;c.b2=(1.f-al*A)/a0;
  c.a1=(-2.f*cw)/a0;c.a2=(1.f-al/A)/a0;
}
void calcHighShelf(BiquadCoef& c,float gDB,float fc,float fs){
  float A=powf(10.f,gDB/40.f),w0=2.f*M_PI*fc/fs,cw=cosf(w0),sw=sinf(w0);
  float al=sw/2.f*sqrtf((A+1.f/A)*(1.f)+2.f);
  if(isnan(al)||al<=0.f)al=0.1f;
  float a0=(A+1)-(A-1)*cw+2*sqrtf(A)*al;
  if(fabsf(a0)<1e-10f){c.b0=1;c.b1=0;c.b2=0;c.a1=0;c.a2=0;return;}
  c.b0=A*((A+1)+(A-1)*cw+2*sqrtf(A)*al)/a0;
  c.b1=-2*A*((A-1)+(A+1)*cw)/a0;
  c.b2=A*((A+1)+(A-1)*cw-2*sqrtf(A)*al)/a0;
  c.a1=2*((A-1)-(A+1)*cw)/a0;
  c.a2=((A+1)-(A-1)*cw-2*sqrtf(A)*al)/a0;
}
inline float procBQ(BiquadCoef& c,BiquadState& s,float x){
  if(isnan(x)||isinf(x))x=0.f;
  float y=c.b0*x+c.b1*s.x1+c.b2*s.x2-c.a1*s.y1-c.a2*s.y2;
  if(isnan(y)||isinf(y)){y=0.f;s.x1=0;s.x2=0;s.y1=0;s.y2=0;}
  s.x2=s.x1;s.x1=x;s.y2=s.y1;s.y1=y;return y;
}
void buildEQCoefs(int i){
  const float fs=44100.f;
  calcLowShelf (eqCoefL[0],(float)eqPresetGain[i][0], 250.f,fs);
  calcPeaking  (eqCoefL[1],(float)eqPresetGain[i][1],1000.f,fs,1.4f);
  calcHighShelf(eqCoefL[2],(float)eqPresetGain[i][2],8000.f,fs);
  eqCoefR[0]=eqCoefL[0];eqCoefR[1]=eqCoefL[1];eqCoefR[2]=eqCoefL[2];
  memset(eqStateL,0,sizeof(eqStateL));memset(eqStateR,0,sizeof(eqStateR));
}
inline int16_t applyEQ_L(int16_t s){
  float x=s;
  x=procBQ(eqCoefL[0],eqStateL[0],x);
  x=procBQ(eqCoefL[1],eqStateL[1],x);
  x=procBQ(eqCoefL[2],eqStateL[2],x);
  return (int16_t)constrain((int)x,-32768,32767);
}
inline int16_t applyEQ_R(int16_t s){
  float x=s;
  x=procBQ(eqCoefR[0],eqStateR[0],x);
  x=procBQ(eqCoefR[1],eqStateR[1],x);
  x=procBQ(eqCoefR[2],eqStateR[2],x);
  return (int16_t)constrain((int)x,-32768,32767);
}

// ----------------------------------------------------------
// EXPERIMENTAL MENU
// ----------------------------------------------------------
#define EXP_MENU_ITEMS 5
static const char* expMenuNames[EXP_MENU_ITEMS]={
  "1. BT Scan","2. TX Gain BT","3. AVRCP Ctrl","4. Equalizer","5. Crossfade"
};

// ----------------------------------------------------------
// RING BUFFER
// ----------------------------------------------------------
static uint8_t*         ringBuf    =nullptr;
static int32_t          ringBufSz  =0;
static volatile int32_t ringHead   =0;
static volatile int32_t ringTail   =0;
static volatile bool    btCbRunning=false;
static portMUX_TYPE     ringMux    =portMUX_INITIALIZER_UNLOCKED;

inline int32_t ringAvail(){
  int32_t h=ringHead,t=ringTail;
  return (h>=t)?(h-t):(ringBufSz-t+h);
}
inline int32_t ringFree(){return ringBufSz-1-ringAvail();}

void IRAM_ATTR ringWrite(const uint8_t* src,int32_t len){
  int32_t fr=ringFree();if(len>fr)len=fr;if(len<=0)return;
  int32_t h=ringHead,te=ringBufSz-h;
  if(len<=te){memcpy(ringBuf+h,src,len);ringHead=(h+len)%ringBufSz;}
  else{memcpy(ringBuf+h,src,te);memcpy(ringBuf,src+te,len-te);ringHead=len-te;}
}
void IRAM_ATTR ringRead(uint8_t* dst,int32_t len){
  int32_t av=ringAvail(),tr=(len>av)?av:len;
  if(tr>0){
    int32_t t=ringTail,te=ringBufSz-t;
    if(tr<=te){memcpy(dst,ringBuf+t,tr);ringTail=(t+tr)%ringBufSz;}
    else{memcpy(dst,ringBuf+t,te);memcpy(dst+te,ringBuf,tr-te);ringTail=tr-te;}
  }
  if(tr<len)memset(dst+tr,0,len-tr);
}
void ringResetSafe(){
  uint32_t t0=millis();
  while(btCbRunning&&millis()-t0<50){taskYIELD();}
  portENTER_CRITICAL(&ringMux);
  ringHead=0;ringTail=0;
  portEXIT_CRITICAL(&ringMux);
}

// ----------------------------------------------------------
// DECODER BUNDLE
// ----------------------------------------------------------
static DecoderBundle*    activeBundle  =nullptr;
static DecoderBundle*    pendingDelete =nullptr;
static volatile bool     decoderInUse  =false;

void safeDeleteBundle(DecoderBundle* b){
  if(!b)return;
  uint32_t t0=millis();
  while(decoderInUse&&millis()-t0<100){taskYIELD();}
  if(b->stream){b->stream->end();delete b->stream;b->stream=nullptr;}
  if(b->codec) {delete b->codec;b->codec=nullptr;}
  delete b;
}

// ----------------------------------------------------------
// OBJEK GLOBAL
// ----------------------------------------------------------
TwoWire             wireOLED=TwoWire(1);
Adafruit_SSD1306    display(OLED_WIDTH,OLED_HEIGHT,&wireOLED,-1);
BluetoothA2DPSource a2dp;
File                audioFile;
std::vector<String> playlist;
std::vector<int>    shuffleOrder;
int                 currentTrack      =0;
int                 shuffleIdx        =0;
bool                isMp3             =false;

static volatile DecodeState decodeState     =DECODE_IDLE;
static TaskHandle_t         decodeTaskHandle=nullptr;

volatile bool       isPlaying         =false;
volatile bool       btConnected       =false;
volatile bool       trackEnded        =false;
volatile bool       fileReadDone      =false;

volatile size_t     bytesPlayedTotal  =0;
volatile size_t     bytesDecodedTotal =0;
volatile size_t     totalDecodedFinal =0;
volatile bool       audioTrackEndedLog=false;

static volatile bool prefillReady     =false;

static volatile bool pendingLoadTrack =false;
static volatile int  pendingLoadIdx   =0;
static uint32_t      stopWaitStartMs  =0;
static bool          stopSettleWait   =false;
static uint32_t      stopSettleMs     =0;

int                 currentVolume     =VOL_DEFAULT;
int                 muteVolume        =0;
bool                isMuted           =false;
RepeatMode          repeatMode        =REPEAT_OFF;
bool                shuffleMode       =false;
bool                sleepTimerOn      =false;
uint32_t            sleepTimerEnd     =0;
AppMode             appMode           =MODE_PLAYER;
char                savedBTName[64]   ={0};
bool                hasSavedBT        =false;

BTDevice            scannedDevices[BT_MAX_DEVICES];
int                 scannedCount      =0;
int                 scanListOffset    =0;
int                 scanSelected      =0;
volatile bool       isScanning        =false;
volatile bool       scanDone          =false;

enum BTRestartState{
  BT_RESTART_IDLE=0,BT_RESTART_STOP_REQ,
  BT_RESTART_WAIT,BT_RESTART_CONNECT
};
static BTRestartState btRestartState  =BT_RESTART_IDLE;
static uint32_t       btRestartMs     =0;
static String         pendingBTName   ="";

int                 currentTxGain     =TX_GAIN_DEFAULT;
int                 pendingTxGain     =TX_GAIN_DEFAULT;
int                 expMenuSelected   =0;

bool                featureAVRCP      =false;
bool                featureEQ         =false;
int                 eqPresetCurrent   =0;
int                 eqPresetPending   =0;
bool                featureCrossfade  =false;

volatile bool       avrcpStackReady   =false;
volatile bool       avrcpInitialized  =false;
volatile bool       avrcpInitPending  =false;
uint32_t            avrcpInitRequestMs=0;
volatile bool       avrcpPlay=false,avrcpPause=false,avrcpNext=false;
volatile bool       avrcpPrev=false,avrcpVolUp=false,avrcpVolDown=false;

static uint32_t     volPendingSaveMs  =0;
static bool         volPendingSave    =false;

volatile bool       crossfadeActive   =false;
volatile int        crossfadeLevel    =CROSSFADE_STEPS;
volatile bool       crossfadeIn       =false;
volatile bool       crossfadeDone     =false;

static bool         confirmDisplayActive=false;
static uint32_t     confirmDisplayEndMs =0;

volatile int        fadeLevel   =256;
volatile int        fadeTarget  =256;

volatile size_t     fileProgress  =0;
volatile size_t     fileTotalSize =0;

static int      tickerOffset      =0;
static uint32_t tickerLastMs      =0;
static uint32_t lastInteractionMs =0;
static bool     oledDimmed        =false;
static uint32_t btDisconnectedAt  =0;
static uint32_t animFrame         =0;
static uint32_t animLastMs        =0;
static bool     showOverlay       =false;
static uint32_t overlayEndMs      =0;
static String   overlayLines[5];
static int      overlayCount      =0;

static uint32_t lastOledUpdateMs  =0;
#define OLED_UPDATE_INTERVAL_MS   80

// Face state
static FaceType currentFace    = FACE_NEUTRAL;
static char     currentMsg[32] = "siap main musik!";
static uint32_t lastFaceUpdate = 0;

// Face liveliness animation
static bool     faceBlinkActive   = false;
static uint32_t faceBlinkEndMs    = 0;
static uint32_t faceNextBlinkMs   = 0;

// Volume/progress bar smooth animation
static float    volBarVisual      = -1.f;
static float    progBarVisual     = -1.f;

// Track end silence guard — FIX bug lagu selesai terlalu cepat
static volatile int32_t trackEndSilenceCount = 0;

struct BtnState{
  uint8_t  pin;
  bool     prev=HIGH,isDown=false,longFired=false;
  bool     rawPrev=HIGH,debouncing=false;
  uint32_t pressedAt=0,debounceStart=0;
};
BtnState btnBoot={BTN_BOOT},btnA={BTN_A},btnB={BTN_B},
         btnC={BTN_C},btnD={BTN_D};

// ----------------------------------------------------------
// FORWARD DECLARATIONS
// ----------------------------------------------------------
void scanPlaylist();void buildShuffleOrder();
void doLoadTrack(int idx);void requestStop();
void requestLoadTrack(int idx);void processStopAndLoad();
void actionPlayPause();void actionNext();void actionPrevious();
void actionRestartTrack();void actionVolUp();void actionVolDown();
void actionVolUpNoSave();void actionVolDownNoSave();
void applyVolume(int v);void scheduleVolSave();
void actionMuteToggle();void actionResetVolume();
void actionShuffleToggle();void actionRepeatToggle();
void actionShowSDInfo();void actionShowSysInfo();
void actionRescanPlaylist();void actionSleepTimerToggle();
void actionOpenExperimentalMenu();
void enterExperimentalMenu();void exitExperimentalMenu();
void drawExperimentalMenu();void handleButtonsExpMenu();
void enterBTScanMode();void exitBTScanMode();
void startBTScan();void stopBTScan();
void connectToScannedDevice(int idx);
void actionScanScrollUp();void actionScanScrollDown();
void actionScanSelect();void actionScanRescan();void actionScanExit();
void handleButtonsScanMode();
void enterTxGainMode();void exitTxGainMode();
void applyTxGain(int idx);void drawTxGainScreen();
void handleButtonsTxGain();
void enterEQMode();void exitEQMode();
void drawEQScreen();void handleButtonsEQ();void applyEQPreset(int idx);
bool initAVRCPStack();void setupAVRCPFilter();void deinitAVRCP();
void updateOLED();void drawScannerScreen();
void showOverlayText(String lines[],int count,uint32_t durMs);
void oledError(const char* l1,const char* l2);
void oledSplash(bool hasPSRAM);
void resetInteraction();void savePrefs();void loadPrefs();
int  updateBtn(BtnState& b);
bool comboPressed(BtnState& b1,BtnState& b2);
void handleButtons();void audioDecodeTask(void* param);
String getFileName(const String& p);String stripExt(const String& f);
String repeatLabel();void processBTRestartStateMachine();
void drawPlayerScreen();void drawFaceArea();
void ryneTask(void* param);void ryneNotify();
void ryneSendToFace(FaceType f,const char* msg);
void notifySkip();void showVolOverlay();void resetRyneTicker();
void notifyVolChange(bool up);
void updateMultiSignalVibe();
UserVibeCategory computeUserVibe();
const char* getVibeFallbackMsg(UserVibeCategory uv, int variant);
FaceType    getVibeFallbackFace(UserVibeCategory uv);

// ----------------------------------------------------------
// RESET RYNE TICKER
// ----------------------------------------------------------
void resetRyneTicker(){
  ryneTickerOffset=0;
  ryneTickerLastMs=millis();
}

// ----------------------------------------------------------
// NOTIFY VOL CHANGE — untuk multi-signal vibe
// ----------------------------------------------------------
void notifyVolChange(bool up){
  uint32_t now=millis();
  // Reset window kalau sudah terlalu lama
  if(now-vibeVolWindowStart>VIBE_VOL_CHANGE_WINDOW_MS){
    vibeVolUpCount  =0;
    vibeVolDownCount=0;
    vibeVolWindowStart=now;
  }
  if(up) vibeVolUpCount++;
  else   vibeVolDownCount++;
}

// ----------------------------------------------------------
// COMPUTE USER VIBE — logika multi-signal
// ----------------------------------------------------------
UserVibeCategory computeUserVibe(){
  uint32_t now=millis();

  // Hitung skip dalam window VIBE_SKIP_WINDOW_MS
  int recentSkips=0;
  for(int i=0;i<vibeSkipWindowCount&&i<(int)(sizeof(vibeSkipTimestamps)/sizeof(vibeSkipTimestamps[0]));i++){
    if(now-vibeSkipTimestamps[i]<VIBE_SKIP_WINDOW_MS) recentSkips++;
  }

  uint32_t listenDuration = (vibeCurrentTrackStart>0)?(now-vibeCurrentTrackStart):0;
  bool repeatOneLong      = (repeatMode==REPEAT_ONE&&vibeRepeatOneStart>0&&
                             now-vibeRepeatOneStart>VIBE_REPEAT_NOSTALGIC_MS);
  bool volExcited         = (vibeVolUpCount>=VIBE_VOL_EXCITED_CHANGES);
  bool volFlapping        = (vibeVolUpCount>=2&&vibeVolDownCount>=2);

  // === Prioritas evaluasi ===

  // Sering skip DAN vol naik turun = gelisah
  if(recentSkips>=VIBE_SKIP_BORED_COUNT&&volFlapping)
    return UVIBE_GELISAH;

  // Sering skip saja = bosen
  if(recentSkips>=VIBE_SKIP_BORED_COUNT)
    return UVIBE_BOSEN;

  // Repeat one lama = nostalgia
  if(repeatOneLong)
    return UVIBE_NOSTALGIK;

  // Vol sering naik + lagu keras = excited
  if(volExcited&&ryne.vibeEnergetic)
    return UVIBE_EXCITED;

  // Vol sering naik + lagu apapun = semangat
  if(volExcited)
    return UVIBE_SEMANGAT;

  // Dengerin lama tanpa skip = fokus
  if(listenDuration>VIBE_LISTEN_ENGAGED_MS&&recentSkips==0)
    return UVIBE_FOKUS;

  // Lagu pelan + dengerin lama = sendu
  if(ryne.vibeQuiet&&listenDuration>30000)
    return UVIBE_SENDU;

  // Lagu calm/sedang = santai
  if(ryne.vibeCalm)
    return UVIBE_SANTAI;

  // Lagu keras tapi tidak excited = semangat sedang
  if(ryne.vibeEnergetic)
    return UVIBE_SEMANGAT;

  return UVIBE_UNKNOWN;
}

// ----------------------------------------------------------
// GET VIBE FALLBACK MSG — pesan variatif per kategori
// ----------------------------------------------------------
const char* getVibeFallbackMsg(UserVibeCategory uv, int variant){
  // Setiap kategori punya 4 variant, dipilih bergilir
  switch(uv){
    case UVIBE_SEMANGAT:{
      static const char* m[]={"gas terus bestie!","lagi on fire nih~","semangat banget ya!","vibe semangat lu keliatan"};
      return m[variant%4];
    }
    case UVIBE_SENDU:{
      static const char* m[]={"lagi mellow nih...","santai aja ya~","vibes tenang banget","dalem banget rasanya"};
      return m[variant%4];
    }
    case UVIBE_BOSEN:{
      static const char* m[]={"ga ada yang cocok?","mau gue piliin ga?","coba shuffle deh~","kurang sreg nih lagunya?"};
      return m[variant%4];
    }
    case UVIBE_NOSTALGIK:{
      static const char* m[]={"lagu favorit nih~","nostalgia ya?","repeat terus sih...","suka banget lagunya!"};
      return m[variant%4];
    }
    case UVIBE_FOKUS:{
      static const char* m[]={"lagi fokus nih~","dengerin serius banget","masuk banget lagunya","flow state detected~"};
      return m[variant%4];
    }
    case UVIBE_GELISAH:{
      static const char* m[]={"lagi gelisah ya?","ga bisa diem nih...","vol naik turun trus","mau gue tenangin?"};
      return m[variant%4];
    }
    case UVIBE_SANTAI:{
      static const char* m[]={"santai banget~","chill mode on","slow living vibes","tenang dan damai~"};
      return m[variant%4];
    }
    case UVIBE_EXCITED:{
      static const char* m[]={"EXCITED banget!!!","full gas volume!","lagi hype nih ya!","ini bukan latihan!"};
      return m[variant%4];
    }
    default:{
      // UVIBE_UNKNOWN — fallback umum, jauh lebih variatif dari "vibe check: ok"
      static const char* m[]={
        "dengerin bareng yuk","musiknya enak nih~",
        "lagi on the way~","best seat in house",
        "audio clear, enjoy!","siap temani hari mu",
        "lagu berikutnya apa?","request dong~"
      };
      static int unk=0;
      return m[(unk++)%8];
    }
  }
}

FaceType getVibeFallbackFace(UserVibeCategory uv){
  switch(uv){
    case UVIBE_SEMANGAT:  return FACE_HAPPY;
    case UVIBE_SENDU:     return FACE_SLEEPY;
    case UVIBE_BOSEN:     return FACE_FRUSTRATED;
    case UVIBE_NOSTALGIK: return FACE_SMIRK;
    case UVIBE_FOKUS:     return FACE_CHILL;
    case UVIBE_GELISAH:   return FACE_SHOCK;
    case UVIBE_SANTAI:    return FACE_CHILL;
    case UVIBE_EXCITED:   return FACE_SHOCK;
    default:              return FACE_NEUTRAL;
  }
}

// ----------------------------------------------------------
// UPDATE MULTI-SIGNAL VIBE — dipanggil dari RYNE task
// ----------------------------------------------------------
void updateMultiSignalVibe(){
  uint32_t now=millis();

  // Update repeat one tracking
  if(repeatMode==REPEAT_ONE){
    if(vibeRepeatOneStart==0) vibeRepeatOneStart=now;
  }else{
    vibeRepeatOneStart=0;
  }

  // Reset vol window jika sudah expired
  if(now-vibeVolWindowStart>VIBE_VOL_CHANGE_WINDOW_MS){
    vibeVolUpCount=0;vibeVolDownCount=0;vibeVolWindowStart=now;
  }

  // Compute
  UserVibeCategory newVibe=computeUserVibe();

  // Update kalau berubah
  if(newVibe!=vibeUserCurrent){
    vibeUserCurrent  =newVibe;
    vibeLastCategoryMs=now;
  }
  ryne.userVibe=vibeUserCurrent;
}

// ----------------------------------------------------------
// RYNE TASK
// ----------------------------------------------------------
void ryneTask(void* param){
  Serial.println("[RYNE] Engine started.");
  for(;;){
    vTaskDelay(pdMS_TO_TICKS(RYNE_INTERVAL_MS));
    uint32_t now=millis();

    // === BUFFER SIGNALS ===
    int32_t avail=ringAvail();
    ryne.bufferHealthy  =(avail>(int32_t)BT_PREFILL_BYTES);
    ryne.bufferCritical =(avail<(int32_t)DRAIN_MIN_BYTES&&isPlaying);
    ryne.bufferUnderrun =(avail==0&&isPlaying&&!fileReadDone);

    // === DECODE SIGNALS ===
    ryne.decodeActive=(decodeState==DECODE_RUNNING);
    ryne.decodeEOF   =fileReadDone;

    size_t curDecoded=(size_t)bytesDecodedTotal;
    bool decodeShouldBeWriting=ryne.decodeActive&&isPlaying&&
                               (ringFree()>=DECODE_CHUNK);
    if(decodeShouldBeWriting){
      if(curDecoded==ryneLastDecoded){
        if(now-ryneDecodeCheckMs>RYNE_STUCK_MS) ryne.decodeStuck=true;
      }else{
        ryne.decodeStuck=false;ryneLastDecoded=curDecoded;
        ryneDecodeCheckMs=now;
      }
    }else{
      ryne.decodeStuck=false;ryneLastDecoded=curDecoded;
      ryneDecodeCheckMs=now;
    }

    // === PLAYBACK SIGNALS ===
    size_t curPlayed=(size_t)bytesPlayedTotal;
    if(isPlaying&&!fileReadDone){
      if(curPlayed==ryneLastPlayed){
        ryne.playbackStuck=(now-rynePlayCheckMs>RYNE_STUCK_MS);
      }else{
        ryne.playbackStuck=false;ryneLastPlayed=curPlayed;
        rynePlayCheckMs=now;
      }
    }else{
      ryne.playbackStuck=false;ryneLastPlayed=curPlayed;
      rynePlayCheckMs=now;
    }

    // Track ending >90%
    size_t fts=(size_t)fileTotalSize;
    size_t fp =(size_t)fileProgress;
    ryne.trackEnding=(fts>0&&fp>0&&(fp*100/fts)>90);

    // Skip storm
    {
      int validSkips=0;
      for(int i=0;i<ryneSkipCount&&i<RYNE_SKIP_STORM_COUNT;i++){
        if(now-ryneSkipTimes[i]<RYNE_SKIP_STORM_MS) validSkips++;
      }
      ryne.skipStorm=(validSkips>=RYNE_SKIP_STORM_COUNT);
    }

    // === BT SIGNALS ===
    ryne.btConnected   =btConnected;
    ryne.btReconnecting=(!btConnected&&btDisconnectedAt>0&&
                         now-btDisconnectedAt<BT_RECONNECT_MS);
    {
      static uint32_t lastDropAt=0;
      if(!btConnected&&btDisconnectedAt>0){
        if(btDisconnectedAt!=lastDropAt){
          lastDropAt=btDisconnectedAt;ryneBTDropCount++;ryneBTDropMs=now;
        }
      }
      if(now-ryneBTDropMs>RYNE_BT_UNSTABLE_MS) ryneBTDropCount=0;
      ryne.btUnstable=(ryneBTDropCount>=RYNE_BT_UNSTABLE_COUNT);
    }

    // === VIBE AUDIO SIGNALS ===
    {
      uint64_t accSum  =vibeAccum;
      uint32_t accCount=vibeSampleCount;
      vibeAccum=0;vibeSampleCount=0;
      if(isPlaying&&accCount>0){
        float avg=(float)accSum/(float)accCount;
        vibeSmooth=(vibeSmooth<=0.f)?avg:(vibeSmooth*0.7f+avg*0.3f);
        ryne.vibeEnergetic=(vibeSmooth>=VIBE_ENERGETIC_THRESHOLD);
        ryne.vibeQuiet    =(vibeSmooth>0.f&&vibeSmooth<VIBE_QUIET_THRESHOLD);
        ryne.vibeCalm     =(!ryne.vibeEnergetic&&!ryne.vibeQuiet&&
                             vibeSmooth<VIBE_CALM_THRESHOLD);
      }else{
        ryne.vibeEnergetic=false;ryne.vibeQuiet=false;ryne.vibeCalm=false;
        if(!isPlaying) vibeSmooth=0.f;
      }
    }

    // === MULTI-SIGNAL USER VIBE ===
    updateMultiSignalVibe();

    ryne.lastUpdateMs=now;
    ryneNotify();
  }
}

// ----------------------------------------------------------
// RYNE NOTIFY — prioritas pesan
// ----------------------------------------------------------
void ryneNotify(){
  if(ryneMsgActive&&millis()<ryneMsgEndMs) return;

  // Prioritas 1 — CRITICAL
  if(ryne.decodeStuck){
    ryneSendToFace(FACE_SHOCK,"decode macet, coba skip!");
    return;
  }
  if(ryne.playbackStuck){
    ryneSendToFace(FACE_FRUSTRATED,"audio stuck, buffer penuh?");
    return;
  }

  // Prioritas 2 — WARNING
  if(ryne.bufferCritical){
    ryneSendToFace(FACE_SAD,"buffer hampir habis tipis!");
    return;
  }
  if(ryne.bufferUnderrun){
    ryneSendToFace(FACE_SAD,"buffer kosong, mohon tunggu");
    return;
  }
  if(ryne.btUnstable){
    ryneSendToFace(FACE_FRUSTRATED,"koneksi BT tidak stabil nih");
    return;
  }
  if(ryne.btReconnecting){
    ryneSendToFace(FACE_SAD,"lagi nyambungin BT sabar ya");
    return;
  }

  // Prioritas 3 — BEHAVIORAL INFO
  if(ryne.skipStorm){
    ryneSendToFace(FACE_SMIRK,"skip mulu, ga ada yg cocok?");
    return;
  }
  if(ryne.trackEnding&&isPlaying){
    switch(ryne.userVibe){
      case UVIBE_NOSTALGIK:
        ryneSendToFace(FACE_SMIRK,"sayang ya mau abis~"); return;
      case UVIBE_FOKUS:                          // <-- fix di sini
        ryneSendToFace(FACE_CHILL,"hampir selesai, next?"); return;
      case UVIBE_EXCITED:
        ryneSendToFace(FACE_HAPPY,"kerennn lanjut gas!"); return;
      default:
        ryneSendToFace(FACE_HAPPY,"lagu hampir selesai~"); return;
    }
  }
  // Prioritas 4 — VIBE UPDATE
  // Hanya kirim pesan vibe kalau:
  // (a) vibe kategori baru berubah, atau
  // (b) sudah 20 detik sejak pesan vibe terakhir
  if(isPlaying){
    static UserVibeCategory lastSentVibe = UVIBE_UNKNOWN;
    static uint32_t         lastVibeMsgMs= 0;
    static int              vibeVariant  = 0;

    bool vibeChanged=(ryne.userVibe!=lastSentVibe);
    bool vibeCooldownOk=(millis()-lastVibeMsgMs>=20000);
    // Kalau baru connect / track baru, tunggu 5 detik dulu
    bool vibeReady=(vibeCurrentTrackStart>0&&millis()-vibeCurrentTrackStart>5000);

    if(vibeReady&&(vibeChanged||vibeCooldownOk)){
      lastVibeMsgMs  =millis();
      lastSentVibe   =ryne.userVibe;
      vibeVariant++;

      FaceType    f=getVibeFallbackFace(ryne.userVibe);
      const char* m=getVibeFallbackMsg(ryne.userVibe,vibeVariant);
      ryneSendToFace(f,m);
      return;
    }
  }

  ryneMsgActive=false;
}

// ----------------------------------------------------------
// RYNE SEND TO FACE
// ----------------------------------------------------------
void ryneSendToFace(FaceType f,const char* msg){
  bool msgChanged=(strcmp(ryneOut.faceMsg,msg)!=0||ryneOut.faceSuggested!=f);
  ryneOut.faceSuggested=f;
  strncpy(ryneOut.faceMsg,msg,31);
  ryneOut.faceMsg[31]  ='\0';
  ryneOut.hasNewMsg    =true;
  ryneOut.msgTimestamp =millis();
  ryneMsgActive     =true;
  ryneMsgEndMs      =millis()+RYNE_MSG_DURATION_MS;
  if(msgChanged) resetRyneTicker();
}

// ----------------------------------------------------------
// NOTIFY SKIP
// ----------------------------------------------------------
void notifySkip(){
  uint32_t now=millis();
  // RYNE skip storm tracking
  for(int i=RYNE_SKIP_STORM_COUNT-1;i>0;i--)
    ryneSkipTimes[i]=ryneSkipTimes[i-1];
  ryneSkipTimes[0]=now;
  if(ryneSkipCount<RYNE_SKIP_STORM_COUNT) ryneSkipCount++;

  // Multi-signal vibe skip tracking
  for(int i=VIBE_SKIP_BORED_COUNT;i>0;i--)
    vibeSkipTimestamps[i]=vibeSkipTimestamps[i-1];
  vibeSkipTimestamps[0]=now;
  vibeLastSkipMs=now;
  if(vibeSkipWindowCount<VIBE_SKIP_BORED_COUNT+1) vibeSkipWindowCount++;

  // Reset listen timer
  vibeCurrentTrackStart=now;
}

// ----------------------------------------------------------
// SHOW VOLUME OVERLAY
// ----------------------------------------------------------
void showVolOverlay(){
  volOverlayActive=true;
  volOverlayEndMs =millis()+VOL_OVERLAY_MS;
}

// ----------------------------------------------------------
// EMOTIONAL STATE DEFAULT — tanpa "vibe check: ok"
// ----------------------------------------------------------
EmotionalState getEmotionalState(){
  if(!btConnected){
    uint32_t putus=(btDisconnectedAt>0)?millis()-btDisconnectedAt:millis();
    if(putus>10000) return {FACE_SAD,      "nyari teman BT..."};
    else            return {FACE_FRUSTRATED,"BT lepas bentar"};
  }
  if(sleepTimerOn){
    int32_t sisa=(int32_t)((sleepTimerEnd-millis())/1000);
    if(sisa<0)sisa=0;
    if(sisa<120) return {FACE_SLEEPY,"mau tidur nih..."};
    else{
      static char sBuf[32];
      snprintf(sBuf,32,"zzz %dm%ds lagi",sisa/60,sisa%60);
      return {FACE_SLEEPY,sBuf};
    }
  }
  if(isMuted)               return {FACE_FRUSTRATED,"aku dibisuin :("};
  if(currentVolume>=VOL_MAX)     return {FACE_SHOCK,    "FULL BLAST!!!"};
  if(currentVolume<=20&&isPlaying) return {FACE_SLEEPY,"volume pelan bgt"};
  if(!isPlaying)            return {FACE_NEUTRAL,   "lagi istirahat~"};
  if(repeatMode==REPEAT_ONE)    return {FACE_SMIRK,  "ulang terus nih~"};
  if(shuffleMode)           return {FACE_HAPPY,     "shuffle time!"};
  if(repeatMode==REPEAT_ALL)    return {FACE_CHILL,  "nonstop jalan~"};

  // Fallback berbasis user vibe yang sudah dihitung RYNE
  // — tidak ada "vibe check: ok" di sini
  if(isPlaying){
    static int      fbVariant  = 0;
    static uint32_t fbLastMs   = 0;
    // Ganti variant setiap 8 detik supaya tidak monoton
    if(millis()-fbLastMs>8000){
      fbLastMs=millis();fbVariant++;
    }
    FaceType    f=getVibeFallbackFace(ryne.userVibe);
    const char* m=getVibeFallbackMsg(ryne.userVibe,fbVariant);
    return {f, m};
  }

  // Tidak playing, tidak ada kondisi khusus
  static const char* idleMsgs[]={
    "tekan play yuk~","mau dengerin apa?",
    "playlist siap nih","tinggal pencet A~"
  };
  static int idleIdx=0;
  static uint32_t idleLastMs=0;
  if(millis()-idleLastMs>6000){idleLastMs=millis();idleIdx=(idleIdx+1)%4;}
  return {FACE_NEUTRAL, idleMsgs[idleIdx]};
}

const uint8_t* getFaceBitmap(FaceType f){
  switch(f){
    case FACE_HAPPY:      return face_happy;
    case FACE_SAD:        return face_sad;
    case FACE_SHOCK:      return face_shock;
    case FACE_SLEEPY:     return face_sleepy;
    case FACE_SMIRK:      return face_smirk;
    case FACE_FRUSTRATED: return face_frustrated;
    case FACE_CHILL:      return face_chill;
    default:              return face_neutral;
  }
}

// ----------------------------------------------------------
// drawFaceArea — y=43..63
// ----------------------------------------------------------
void drawFaceArea(){
  display.fillRect(0,43,128,21,SSD1306_BLACK);
  display.drawLine(0,43,127,43,SSD1306_WHITE);

  // ── VOLUME OVERLAY ──────────────────────────────────────
  if(volOverlayActive){
    if(millis()<volOverlayEndMs){
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0,45);
      display.print(isMuted?"MUTE":"VOL ");
      const int bx=26,by=45,bw=80,bh=7;
      float targetFill=(float)map(currentVolume,VOL_MIN,VOL_MAX,0,bw-2);
      if(volBarVisual<0.f) volBarVisual=targetFill;
      volBarVisual+=(targetFill-volBarVisual)*0.35f;
      int fill=(int)(volBarVisual+0.5f);
      display.drawRect(bx,by,bw,bh,SSD1306_WHITE);
      if(fill>0) display.fillRect(bx+1,by+1,fill,bh-2,SSD1306_WHITE);
      char vb[5];
      snprintf(vb,sizeof(vb),"%3d%%",map(currentVolume,VOL_MIN,VOL_MAX,0,100));
      display.setCursor(110,45);display.print(vb);
      display.setCursor(0,56);
      for(int i=0;i<16;i++){
        int thr=i*(VOL_MAX/16);
        display.print(currentVolume>=thr?"=":"-");
      }
      return;
    }else{
      volOverlayActive=false;
    }
  }

  // ── UPDATE FACE STATE ────────────────────────────────────
  if(millis()-lastFaceUpdate>=FACE_UPDATE_INTERVAL){
    lastFaceUpdate=millis();
    if(ryneMsgActive&&millis()<ryneMsgEndMs){
      bool fc=(currentFace!=ryneOut.faceSuggested);
      bool mc=(strcmp(currentMsg,ryneOut.faceMsg)!=0);
      if(fc||mc) resetRyneTicker();
      currentFace=ryneOut.faceSuggested;
      strncpy(currentMsg,ryneOut.faceMsg,31);
      currentMsg[31]='\0';
    }else{
      static bool     wasRyne       = false;
      static uint32_t ryneExpiredAt = 0;
      if(ryneMsgActive){wasRyne=true;ryneExpiredAt=millis();}
      ryneMsgActive=false;

      // Cooldown 1 detik setelah RYNE msg berakhir sebelum ganti default
      bool inCooldown=(wasRyne&&millis()-ryneExpiredAt<1000);
      if(!inCooldown){
        wasRyne=false;
        EmotionalState es=getEmotionalState();
        bool fc=(currentFace!=es.face);
        bool mc=(strcmp(currentMsg,es.msg)!=0);
        if(fc||mc) resetRyneTicker();
        currentFace=es.face;
        strncpy(currentMsg,es.msg,31);
        currentMsg[31]='\0';
      }
    }
  }

  // ── ANIMASI HIDUP: breathing bob + kedip mata ──────────
  uint32_t nowAnim=millis();
  float bobWave=sinf(nowAnim*0.0015f);
  int bobOffset=(bobWave>0.3f)?-1:(bobWave<-0.3f?1:0);

  if(!faceBlinkActive&&nowAnim>=faceNextBlinkMs){
    faceBlinkActive=true;faceBlinkEndMs=nowAnim+130;
  }
  if(faceBlinkActive&&nowAnim>=faceBlinkEndMs){
    faceBlinkActive=false;
    faceNextBlinkMs=nowAnim+2500+(uint32_t)random(0,3000);
  }

  // ── GAMBAR FACE BITMAP ──────────────────────────────────
  const uint8_t* bmp=getFaceBitmap(currentFace);
  for(int row=0;row<13;row++){
    uint8_t b0=pgm_read_byte(&bmp[row*2]);
    uint8_t b1=pgm_read_byte(&bmp[row*2+1]);
    for(int bit=0;bit<8;bit++){
      if(b0&(0x80>>bit)) display.drawPixel(1+bit,   44+row+bobOffset,SSD1306_WHITE);
      if(b1&(0x80>>bit)) display.drawPixel(1+8+bit, 44+row+bobOffset,SSD1306_WHITE);
    }
  }
  if(faceBlinkActive){
    display.fillRect(2,47+bobOffset,13,4,SSD1306_BLACK);
    display.drawFastHLine(3,48+bobOffset,4,SSD1306_WHITE);
    display.drawFastHLine(9,48+bobOffset,4,SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // ── STATUS ICONS ─────────────────────────────────────────
  String st="";
  if(shuffleMode)                 st+="S";
  if(repeatMode==REPEAT_ALL)      st+="R";
  else if(repeatMode==REPEAT_ONE) st+="1";
  if(featureEQ)                   st+="E";

  int iconPx   =(int)st.length()*6;
  int msgStartX=19;
  int msgAreaPx=128-msgStartX-(iconPx>0?iconPx+2:0);
  int msgMaxCh =msgAreaPx/6;
  if(msgMaxCh<1) msgMaxCh=1;

  if(iconPx>0){
    display.setCursor(128-iconPx,47);
    display.print(st);
  }

  // ── BARIS 1 y=47 — PESAN DENGAN AUTO-SCROLL ─────────────
  String msg    =String(currentMsg);
  int    msgLen =(int)msg.length();

  if(msgLen<=msgMaxCh){
    ryneTickerOffset=0;
    display.setCursor(msgStartX,47);
    display.print(msg);
  }else{
    if(millis()-ryneTickerLastMs>=RYNE_TICKER_INTERVAL_MS){
      ryneTickerLastMs=millis();ryneTickerOffset++;
      String looped=msg+"   ";
      if(ryneTickerOffset>=(int)looped.length()) ryneTickerOffset=0;
    }
    String looped =msg+"   ";
    int    loopLen=(int)looped.length();
    String slice  ="";
    for(int i=0;i<msgMaxCh;i++){
      slice+=looped[(ryneTickerOffset+i)%loopLen];
    }
    display.setCursor(msgStartX,47);
    display.print(slice);
  }

  // ── BARIS 2 y=57 ─────────────────────────────────────────
  display.setCursor(msgStartX,57);
  if(ryneMsgActive){
    int32_t sisaMs=(int32_t)(ryneMsgEndMs-millis());
    if(sisaMs<0) sisaMs=0;
    char ind[19];
    snprintf(ind,sizeof(ind),"^RYNE %d.%ds",
      (int)(sisaMs/1000),(int)((sisaMs%1000)/100));
    display.print(ind);
  }else{
    if(!playlist.empty()&&isPlaying){
      char inf[19];
      snprintf(inf,sizeof(inf),"%02d/%02d %s",
        currentTrack+1,(int)playlist.size(),
        isMp3?"MP3":"WAV");
      display.print(inf);
    }else if(!btConnected){
      display.print("BT disconnected");
    }else{
      display.print("ready~");
    }
  }
}

// ----------------------------------------------------------
// AVRCP
// ----------------------------------------------------------
static void avrc_tg_callback(esp_avrc_tg_cb_event_t ev,
                              esp_avrc_tg_cb_param_t* p){
  if(ev==ESP_AVRC_TG_PASSTHROUGH_CMD_EVT){
    if(p->psth_cmd.key_state!=0)return;
    switch(p->psth_cmd.key_code){
      case ESP_AVRC_PT_CMD_PLAY:     avrcpPlay=true;    break;
      case ESP_AVRC_PT_CMD_PAUSE:    avrcpPause=true;   break;
      case ESP_AVRC_PT_CMD_STOP:     avrcpPause=true;   break;
      case ESP_AVRC_PT_CMD_FORWARD:  avrcpNext=true;    break;
      case ESP_AVRC_PT_CMD_BACKWARD: avrcpPrev=true;    break;
      case ESP_AVRC_PT_CMD_VOL_UP:   avrcpVolUp=true;   break;
      case ESP_AVRC_PT_CMD_VOL_DOWN: avrcpVolDown=true; break;
      default:break;
    }
  }
  if(ev==ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT)
    currentVolume=constrain((int)p->set_abs_vol.volume,VOL_MIN,VOL_MAX);
}
bool initAVRCPStack(){
  esp_err_t r=esp_avrc_tg_init();
  if(r==ESP_OK||r==ESP_ERR_INVALID_STATE){avrcpStackReady=true;return true;}
  avrcpStackReady=false;return false;
}
void setupAVRCPFilter(){
  if(!avrcpStackReady)return;
  if(esp_avrc_tg_register_callback(avrc_tg_callback)!=ESP_OK)return;
  esp_avrc_psth_bit_mask_t cs;memset(&cs,0,sizeof(cs));
  if(esp_avrc_tg_get_psth_cmd_filter(
       ESP_AVRC_PSTH_FILTER_ALLOWED_CMD,&cs)!=ESP_OK){
    memset(&cs,0,sizeof(cs));
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&cs,ESP_AVRC_PT_CMD_PLAY);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&cs,ESP_AVRC_PT_CMD_PAUSE);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&cs,ESP_AVRC_PT_CMD_STOP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&cs,ESP_AVRC_PT_CMD_FORWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&cs,ESP_AVRC_PT_CMD_BACKWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&cs,ESP_AVRC_PT_CMD_VOL_UP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&cs,ESP_AVRC_PT_CMD_VOL_DOWN);
  }
  esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD,&cs);
  esp_avrc_rn_evt_cap_mask_t es;memset(&es,0,sizeof(es));
  esp_avrc_rn_evt_bit_mask_operation(
    ESP_AVRC_BIT_MASK_OP_SET,&es,ESP_AVRC_RN_VOLUME_CHANGE);
  esp_avrc_tg_set_rn_evt_cap(&es);
  avrcpInitialized=true;avrcpInitPending=false;
}
void deinitAVRCP(){avrcpInitialized=false;avrcpInitPending=false;}

// ----------------------------------------------------------
// GAP CALLBACK
// ----------------------------------------------------------
static void gap_callback(esp_bt_gap_cb_event_t ev,
                         esp_bt_gap_cb_param_t* p){
  if(ev==ESP_BT_GAP_DISC_RES_EVT){
    if(scannedCount>=BT_MAX_DEVICES)return;
    BTDevice dev;memset(&dev,0,sizeof(dev));
    memcpy(dev.bda,p->disc_res.bda,6);dev.rssi=-100;
    for(int i=0;i<p->disc_res.num_prop;i++){
      auto* pr=&p->disc_res.prop[i];
      if(pr->type==ESP_BT_GAP_DEV_PROP_EIR){
        uint8_t nl=0;
        uint8_t* np=esp_bt_gap_resolve_eir_data(
          (uint8_t*)pr->val,ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,&nl);
        if(!np)np=esp_bt_gap_resolve_eir_data(
          (uint8_t*)pr->val,ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME,&nl);
        if(np&&nl>0){
          nl=min((int)nl,63);
          memcpy(dev.name,np,nl);dev.name[nl]=0;dev.hasName=true;
        }
      }else if(pr->type==ESP_BT_GAP_DEV_PROP_RSSI){
        dev.rssi=*((int8_t*)pr->val);
      }else if(pr->type==ESP_BT_GAP_DEV_PROP_BDNAME&&
               !dev.hasName&&pr->len>0){
        int nl2=min((int)pr->len,63);
        memcpy(dev.name,pr->val,nl2);dev.name[nl2]=0;dev.hasName=true;
      }
    }
    if(!dev.hasName||strlen(dev.name)==0)
      snprintf(dev.name,sizeof(dev.name),
        "BT:%02X:%02X:%02X",dev.bda[3],dev.bda[4],dev.bda[5]);
    bool dup=false;
    for(int i=0;i<scannedCount;i++){
      if(memcmp(scannedDevices[i].bda,dev.bda,6)==0){
        if(dev.hasName){memcpy(scannedDevices[i].name,dev.name,64);
          scannedDevices[i].hasName=true;}
        if(dev.rssi>scannedDevices[i].rssi) scannedDevices[i].rssi=dev.rssi;
        dup=true;break;
      }
    }
    if(!dup)scannedDevices[scannedCount++]=dev;
    if(appMode==MODE_BT_SCAN)drawScannerScreen();
  }else if(ev==ESP_BT_GAP_DISC_STATE_CHANGED_EVT){
    if(p->disc_st_chg.state==ESP_BT_GAP_DISCOVERY_STOPPED){
      isScanning=false;scanDone=true;
    }else if(p->disc_st_chg.state==ESP_BT_GAP_DISCOVERY_STARTED){
      isScanning=true;scanDone=false;
    }
  }
}

// ----------------------------------------------------------
// getSoundData — FIX: silence guard sebelum trackEnded
// ----------------------------------------------------------
int32_t IRAM_ATTR getSoundData(Frame* frameBuffer,int32_t frameCount){
  int32_t byteCount=frameCount*sizeof(Frame);
  btCbRunning=true;

  if(!isPlaying){
    memset(frameBuffer,0,byteCount);
    btCbRunning=false;return frameCount;
  }

  decoderInUse=true;
  DecoderBundle* bndl=activeBundle;
  if(!bndl){
    decoderInUse=false;
    memset(frameBuffer,0,byteCount);
    btCbRunning=false;return frameCount;
  }

  if(!prefillReady){
    if(ringAvail()>=BT_PREFILL_BYTES||fileReadDone){
      prefillReady=true;
    }else{
      memset(frameBuffer,0,byteCount);
      decoderInUse=false;btCbRunning=false;return frameCount;
    }
  }

  auto applyFX=[&](int32_t frames){
    int fl=fadeLevel,cfl=crossfadeLevel;
    bool cfA=crossfadeActive;
    uint64_t vibeSum=0;
    for(int32_t i=0;i<frames;i++){
      int16_t l=frameBuffer[i].channel1,r=frameBuffer[i].channel2;
      if(featureEQ&&eqPresetCurrent!=0){l=applyEQ_L(l);r=applyEQ_R(r);}
      vibeSum+=(uint32_t)abs((int)l)+(uint32_t)abs((int)r);
      int cfLvl=CROSSFADE_STEPS;
      if(cfA){
        cfLvl=cfl;
        if(crossfadeIn){
          if(cfl<CROSSFADE_STEPS){cfl++;crossfadeLevel=cfl;}
          else{crossfadeActive=false;cfA=false;}
        }else{
          if(cfl>0){cfl--;crossfadeLevel=cfl;}
          else{crossfadeActive=false;cfA=false;crossfadeDone=true;}
        }
      }
      int ms=(fl*cfLvl)/CROSSFADE_STEPS;
      frameBuffer[i].channel1=(int16_t)(((int32_t)l*ms)>>8);
      frameBuffer[i].channel2=(int16_t)(((int32_t)r*ms)>>8);
    }
    vibeAccum+=vibeSum;
    vibeSampleCount+=(uint32_t)frames;
    if(fl<fadeTarget)      fadeLevel=min(fl+2,(int)fadeTarget);
    else if(fl>fadeTarget) fadeLevel=max(fl-2,(int)fadeTarget);
  };

  int32_t av=ringAvail();

  if(fileReadDone){
    if(av<=0){
      // ── FIX BUG TRACK END TERLALU CEPAT ────────────────
      // Jangan langsung set trackEnded=true saat ring kosong.
      // Tunggu TRACK_END_SILENCE_FRAMES callback berturut-turut
      // yang semuanya kosong, baru anggap benar-benar selesai.
      // Ini beri waktu ~1 detik untuk audio tail / reverb drain.
      trackEndSilenceCount++;
      if(trackEndSilenceCount>=TRACK_END_SILENCE_FRAMES){
        if(!trackEnded){
          trackEnded=true;
          audioTrackEndedLog=true;
        }
        trackEndSilenceCount=0;
      }
      memset(frameBuffer,0,byteCount);
      decoderInUse=false;btCbRunning=false;return frameCount;
    }else{
      // Masih ada data — reset silence counter
      trackEndSilenceCount=0;
    }

    int32_t tr=(av<byteCount)?av:byteCount;
    ringRead((uint8_t*)frameBuffer,tr);
    if(tr<byteCount)memset((uint8_t*)frameBuffer+tr,0,byteCount-tr);
    bytesPlayedTotal+=tr;
    applyFX(frameCount);
    decoderInUse=false;btCbRunning=false;return frameCount;
  }

  if(av<byteCount){
    memset(frameBuffer,0,byteCount);
    decoderInUse=false;btCbRunning=false;return frameCount;
  }
  ringRead((uint8_t*)frameBuffer,byteCount);
  bytesPlayedTotal+=byteCount;
  applyFX(frameCount);
  decoderInUse=false;btCbRunning=false;return frameCount;
}

// ----------------------------------------------------------
// BT CONNECTION CALLBACK
// ----------------------------------------------------------
void onConnectionChanged(esp_a2d_connection_state_t state,void* obj){
  if(state==ESP_A2D_CONNECTION_STATE_CONNECTED){
    btConnected=true;btDisconnectedAt=0;
    Serial.println("[BT] Terhubung!");
    applyTxGain(currentTxGain);
    a2dp.set_volume(currentVolume);
    if(featureAVRCP&&avrcpStackReady){
      avrcpInitPending=true;avrcpInitRequestMs=millis();
    }
    if(!isPlaying&&!playlist.empty()){
      pendingLoadIdx=currentTrack;pendingLoadTrack=true;
    }
  }else{
    btConnected=false;isPlaying=false;
    if(decodeState==DECODE_RUNNING)decodeState=DECODE_STOP_REQ;
    btDisconnectedAt=millis();avrcpInitPending=false;
    Serial.println("[BT] Terputus.");
  }
  tickerOffset=0;
}

// ----------------------------------------------------------
// TX GAIN
// ----------------------------------------------------------
void applyTxGain(int idx){
  idx=constrain(idx,0,TX_GAIN_LEVELS-1);
  esp_bredr_tx_power_set(txGainEnum[idx],txGainEnum[idx]);
  currentTxGain=idx;
}

// ----------------------------------------------------------
// SETUP
// ----------------------------------------------------------
void setup(){
  Serial.begin(115200);delay(300);
  Serial.println("\n=== BT Music v1.0.0 — RYNE Multi-Vibe ===");

  bool hasPSRAM=psramFound();
  ringBuf  =hasPSRAM?(uint8_t*)ps_malloc(RING_BUF_SIZE)
                    :(uint8_t*)malloc(RING_BUF_FALLBACK);
  ringBufSz=hasPSRAM?RING_BUF_SIZE:RING_BUF_FALLBACK;
  if(!ringBuf){
    Serial.println("[FATAL] Ring buf!");while(1)delay(1000);
  }

  pinMode(BTN_BOOT,INPUT_PULLUP);pinMode(BTN_A,INPUT_PULLUP);
  pinMode(BTN_B,INPUT_PULLUP);
  pinMode(BTN_C,INPUT_PULLUP);pinMode(BTN_D,INPUT_PULLUP);

  wireOLED.begin(OLED_SDA_PIN,OLED_SCL_PIN,800000UL);
  if(!display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR))
    Serial.println("[WARN] OLED!");
  oledSplash(hasPSRAM);delay(1500);

  if(!SD_MMC.begin("/sdcard",false)){
    oledError("SD Card","Gagal Mount!");while(1)delay(1000);
  }
  scanPlaylist();
  if(playlist.empty()){
    oledError("SD Card","No MP3/WAV!");while(1)delay(1000);
  }

  loadPrefs();
  if(currentTrack>=(int)playlist.size())currentTrack=0;
  buildShuffleOrder();
  hasSavedBT=(strlen(savedBTName)>0);
  buildEQCoefs(eqPresetCurrent);

  // Init RYNE
  memset(&ryne,0,sizeof(ryne));
  memset(&ryneOut,0,sizeof(ryneOut));
  ryne.bufferHealthy=true;
  ryneMsgActive    =false;
  ryneTickerOffset =0;
  ryneTickerLastMs =millis();

  // Init vibe tracking
  vibeCurrentTrackStart=millis();
  vibeVolWindowStart   =millis();
  vibeUserCurrent      =UVIBE_UNKNOWN;

  // Splash
  display.clearDisplay();
  display.fillRect(0,0,128,13,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);display.setCursor(2,3);
  display.print(" BT MUSIC PLAYER v1.0.0");
  display.setTextColor(SSD1306_WHITE);
  display.drawLine(0,13,127,13,SSD1306_WHITE);
  display.setCursor(2,16);
  display.printf("%d lagu  TX:%s",
    (int)playlist.size(),txGainLabel[currentTxGain]);
  display.setCursor(2,26);
  display.printf("EQ:%-4s CF:%s RYNE:ON",
    featureEQ?eqPresetName[eqPresetCurrent]:"OFF",
    featureCrossfade?"ON":"OFF");
  display.drawLine(0,36,127,36,SSD1306_WHITE);
  display.setCursor(2,39);
  if(hasSavedBT){
    String s=String(savedBTName);
    if(s.length()>20)s=s.substring(0,18)+"..";
    display.print(s);
  }else display.print("Scan BT otomatis...");
  display.drawLine(0,50,127,50,SSD1306_WHITE);
  display.setCursor(2,53);display.print("RYNE MULTI-VIBE READY");
  display.display();delay(1800);

  a2dp.set_on_connection_state_changed(onConnectionChanged,nullptr);
  a2dp.set_volume(currentVolume);
  a2dp.set_data_callback_in_frames(getSoundData);
  if(hasSavedBT)a2dp.start(savedBTName);
  else          a2dp.start(BT_DEFAULT_NAME);
  delay(300);
  applyTxGain(currentTxGain);
  if(featureAVRCP)initAVRCPStack();
  if(!hasSavedBT){delay(200);enterBTScanMode();}

  xTaskCreatePinnedToCore(
    ryneTask,"RYNETask",4096,
    nullptr,1,&ryneTaskHandle,1
  );
  Serial.println("[RYNE] Task created Core 1.");

  lastInteractionMs=millis();
  if(appMode==MODE_PLAYER)updateOLED();
}

// ----------------------------------------------------------
// REQUEST STOP
// ----------------------------------------------------------
void requestStop(){
  if(decodeState==DECODE_IDLE&&activeBundle==nullptr)return;
  isPlaying=false;
  if(decodeState==DECODE_RUNNING)decodeState=DECODE_STOP_REQ;
  stopWaitStartMs=millis();
  stopSettleWait=false;
}

// ----------------------------------------------------------
// PROCESS STOP AND LOAD
// ----------------------------------------------------------
void processStopAndLoad(){
  if(decodeState==DECODE_IDLE&&!pendingLoadTrack)return;
  if(decodeState==DECODE_RUNNING)return;

  if(decodeState==DECODE_STOP_REQ){
    if(millis()-stopWaitStartMs>STOP_TIMEOUT_MS){
      Serial.println("[STOP] Timeout! Force kill.");
      if(decodeTaskHandle){
        vTaskDelete(decodeTaskHandle);decodeTaskHandle=nullptr;
      }
      decodeState=DECODE_STOPPED;
    }
    return;
  }

  if(decodeState==DECODE_STOPPED){
    if(!stopSettleWait){
      stopSettleWait=true;stopSettleMs=millis();return;
    }
    if(millis()-stopSettleMs<STOP_SETTLE_MS)return;
    stopSettleWait=false;

    DecoderBundle* toDelete=activeBundle;
    activeBundle=nullptr;

    uint32_t tw=millis();
    while(decoderInUse&&millis()-tw<100){taskYIELD();}
    safeDeleteBundle(toDelete);
    if(pendingDelete){safeDeleteBundle(pendingDelete);pendingDelete=nullptr;}
    if(audioFile)audioFile.close();

    ringResetSafe();

    fadeLevel=0;fadeTarget=256;
    prefillReady=false;
    crossfadeActive=false;crossfadeDone=false;
    crossfadeLevel=CROSSFADE_STEPS;
    fileReadDone=false;bytesPlayedTotal=0;
    bytesDecodedTotal=0;totalDecodedFinal=0;
    trackEnded=false;audioTrackEndedLog=false;
    trackEndSilenceCount=0;  // reset silence guard
    fileProgress=0;fileTotalSize=0;
    decodeTaskHandle=nullptr;
    decodeState=DECODE_IDLE;

    if(pendingLoadTrack){pendingLoadTrack=false;doLoadTrack(pendingLoadIdx);}
  }
}

// ----------------------------------------------------------
// DO LOAD TRACK
// ----------------------------------------------------------
void doLoadTrack(int idx){
  if(playlist.empty()||!btConnected)return;
  if(decodeState!=DECODE_IDLE){
    pendingLoadIdx=idx;pendingLoadTrack=true;return;
  }

  int sz=(int)playlist.size();
  currentTrack=((idx%sz)+sz)%sz;
  tickerOffset=0;
  String path=playlist[currentTrack];

  File newFile=SD_MMC.open(path.c_str());
  if(!newFile){
    Serial.printf("[ERROR] Buka gagal: %s\n",path.c_str());return;
  }
  if(audioFile)audioFile.close();
  audioFile=newFile;

  fileTotalSize=audioFile.size();fileProgress=0;
  fileReadDone=false;bytesPlayedTotal=0;
  bytesDecodedTotal=0;totalDecodedFinal=0;
  trackEnded=false;audioTrackEndedLog=false;
  trackEndSilenceCount=0;  // reset silence guard
  prefillReady=false;

  String ext=path.substring(path.lastIndexOf('.')+1);
  ext.toUpperCase();isMp3=(ext=="MP3");

  DecoderBundle* nb=new DecoderBundle();
  nb->isMp3=isMp3;
  nb->codec =isMp3?(AudioDecoder*)new MP3DecoderHelix()
                  :(AudioDecoder*)new WAVDecoder();
  nb->stream=new EncodedAudioStream(&audioFile,nb->codec);
  nb->stream->begin();

  ringResetSafe();

  if(featureCrossfade){
    crossfadeLevel=0;crossfadeIn=true;crossfadeActive=true;
    fadeLevel=0;fadeTarget=256;
  }else{
    crossfadeActive=false;fadeLevel=0;fadeTarget=256;
  }
  crossfadeDone=false;

  DecoderBundle* old=activeBundle;
  activeBundle=nb;
  if(old){pendingDelete=old;Serial.println("[WARN] old bundle!");}

  decodeState=DECODE_IDLE;
  isPlaying=true;

  BaseType_t r=xTaskCreatePinnedToCore(
    audioDecodeTask,"DecodeTask",DECODE_TASK_STACK,
    nullptr,3,&decodeTaskHandle,0
  );
  if(r!=pdPASS){
    Serial.println("[ERROR] Gagal buat decode task!");
    isPlaying=false;activeBundle=nullptr;
    safeDeleteBundle(nb);decodeState=DECODE_IDLE;return;
  }
  decodeState=DECODE_RUNNING;
  a2dp.set_volume(currentVolume);

  // Reset vibe tracking untuk track baru
  vibeCurrentTrackStart=millis();
  vibeRepeatOneStart   =(repeatMode==REPEAT_ONE)?millis():0;
  resetRyneTicker();
  vibeAccum=0;vibeSampleCount=0;vibeSmooth=0.f;
  ryne.vibeEnergetic=false;ryne.vibeCalm=false;ryne.vibeQuiet=false;
  progBarVisual=0.f;

  savePrefs();
  Serial.printf("[PLAY] %s [%s] %.1fKB\n",
    getFileName(path).c_str(),isMp3?"MP3":"WAV",
    fileTotalSize/1024.f);
  updateOLED();
}

// ----------------------------------------------------------
// REQUEST LOAD TRACK
// ----------------------------------------------------------
void requestLoadTrack(int idx){
  pendingLoadIdx=idx;pendingLoadTrack=true;
  if(decodeState==DECODE_RUNNING){
    if(featureCrossfade&&isPlaying){
      crossfadeIn=false;crossfadeLevel=CROSSFADE_STEPS;
      crossfadeActive=true;crossfadeDone=false;
    }else requestStop();
  }else if(decodeState==DECODE_STOP_REQ||
           decodeState==DECODE_STOPPED){
  }else{
    pendingLoadTrack=false;doLoadTrack(idx);
  }
}

// ----------------------------------------------------------
// DECODE TASK
// ----------------------------------------------------------
void audioDecodeTask(void* param){
  Serial.println("[DECODE] Start.");
  uint8_t* buf=psramFound()
    ?(uint8_t*)ps_malloc(DECODE_CHUNK)
    :(uint8_t*)malloc(DECODE_CHUNK);
  if(!buf){
    fileReadDone=true;decodeState=DECODE_STOPPED;
    decodeTaskHandle=nullptr;vTaskDelete(nullptr);return;
  }

  while(true){
    if(decodeState==DECODE_STOP_REQ)break;
    DecoderBundle* bndl=activeBundle;
    if(!bndl||!bndl->stream){vTaskDelay(pdMS_TO_TICKS(20));continue;}
    if(ringFree()<DECODE_CHUNK){vTaskDelay(pdMS_TO_TICKS(5));continue;}
    if(decodeState==DECODE_STOP_REQ)break;
    int32_t n=bndl->stream->readBytes(buf,DECODE_CHUNK);
    if(n>0){
      portENTER_CRITICAL(&ringMux);
      ringWrite(buf,n);
      portEXIT_CRITICAL(&ringMux);
      bytesDecodedTotal+=n;
      fileProgress=audioFile.position();
    }else{
      totalDecodedFinal=bytesDecodedTotal;
      Serial.printf("[DECODE] EOF. Decoded:%u Ring:%d\n",
        (unsigned)bytesDecodedTotal,(int)ringAvail());
      free(buf);
      fileReadDone=true;decodeState=DECODE_STOPPED;
      decodeTaskHandle=nullptr;vTaskDelete(nullptr);return;
    }
    taskYIELD();
  }

  free(buf);
  decodeState=DECODE_STOPPED;
  decodeTaskHandle=nullptr;
  vTaskDelete(nullptr);
}

// ----------------------------------------------------------
// BT RESTART STATE MACHINE
// ----------------------------------------------------------
void processBTRestartStateMachine(){
  switch(btRestartState){
    case BT_RESTART_IDLE:break;
    case BT_RESTART_STOP_REQ:
      if(isPlaying||decodeState==DECODE_RUNNING)requestStop();
      btRestartState=BT_RESTART_WAIT;btRestartMs=millis();break;
    case BT_RESTART_WAIT:
      if(decodeState==DECODE_IDLE&&!isPlaying)
        btRestartState=BT_RESTART_CONNECT;
      else if(millis()-btRestartMs>2000)
        btRestartState=BT_RESTART_CONNECT;
      break;
    case BT_RESTART_CONNECT:
      appMode=MODE_PLAYER;
      strncpy(savedBTName,pendingBTName.c_str(),63);
      savedBTName[63]=0;
      hasSavedBT=true;savePrefs();
      a2dp.set_auto_reconnect(true);a2dp.start(savedBTName);
      {String ls[3]={"Menghubungi:",
                     String(savedBTName).substring(0,20),
                     "Mohon tunggu..."};
       showOverlayText(ls,3,2500);}
      pendingBTName="";
      btRestartState=BT_RESTART_IDLE;
      resetInteraction();break;
  }
}

// ----------------------------------------------------------
// LOOP
// ----------------------------------------------------------
void loop(){
  if(avrcpInitPending&&featureAVRCP&&btConnected&&
     avrcpStackReady&&millis()-avrcpInitRequestMs>=500)
    setupAVRCPFilter();

  if(audioTrackEndedLog){
    audioTrackEndedLog=false;
    Serial.println("[AUDIO] Track selesai.");
  }

  if(confirmDisplayActive&&millis()>=confirmDisplayEndMs){
    confirmDisplayActive=false;
    if(appMode==MODE_TX_GAIN){
      appMode=MODE_EXPERIMENTAL_MENU;
      resetInteraction();drawExperimentalMenu();
    }else if(appMode==MODE_EQUALIZER)exitEQMode();
    else if(appMode==MODE_EXPERIMENTAL_MENU)drawExperimentalMenu();
  }

  processBTRestartStateMachine();

  if(appMode==MODE_EXPERIMENTAL_MENU){
    handleButtonsExpMenu();delay(10);return;
  }
  if(appMode==MODE_BT_SCAN){
    handleButtonsScanMode();
    if(isScanning&&millis()-animLastMs>=300){
      animLastMs=millis();animFrame=(animFrame+1)%4;
      drawScannerScreen();
    }
    if(scanDone){scanDone=false;drawScannerScreen();}
    delay(10);return;
  }
  if(appMode==MODE_TX_GAIN){
    if(!confirmDisplayActive)handleButtonsTxGain();
    delay(10);return;
  }
  if(appMode==MODE_EQUALIZER){
    if(!confirmDisplayActive)handleButtonsEQ();
    delay(10);return;
  }

  // ==================== MODE PLAYER ====================
  processStopAndLoad();

  if(crossfadeDone&&featureCrossfade&&pendingLoadTrack){
    crossfadeDone=false;requestStop();return;
  }
  if(pendingLoadTrack&&decodeState==DECODE_IDLE){
    int idx=pendingLoadIdx;pendingLoadTrack=false;
    doLoadTrack(idx);return;
  }

  handleButtons();

  if(featureAVRCP&&avrcpInitialized){
    if(avrcpPlay&&!isPlaying) {avrcpPlay=false; actionPlayPause();}
    if(avrcpPause&&isPlaying) {avrcpPause=false;actionPlayPause();}
    if(avrcpNext) {avrcpNext=false; actionNext();}
    if(avrcpPrev) {avrcpPrev=false; actionPrevious();}
    if(avrcpVolUp)  {avrcpVolUp=false;  actionVolUpNoSave();  scheduleVolSave();}
    if(avrcpVolDown){avrcpVolDown=false;actionVolDownNoSave();scheduleVolSave();}
  }

  if(volPendingSave&&millis()-volPendingSaveMs>=VOL_SAVE_DEBOUNCE_MS){
    volPendingSave=false;savePrefs();
  }

  if(!btConnected&&btDisconnectedAt>0&&
     millis()-btDisconnectedAt>BT_RECONNECT_MS){
    btDisconnectedAt=millis();a2dp.reconnect();
  }

  if(!oledDimmed&&millis()-lastInteractionMs>OLED_DIM_TIMEOUT_MS){
    display.dim(true);oledDimmed=true;
  }

  if(!btConnected&&millis()-animLastMs>=400){
    animLastMs=millis();animFrame=(animFrame+1)%4;
  }

  if(isPlaying&&!playlist.empty()){
    String fn=stripExt(getFileName(playlist[currentTrack]));
    if((int)fn.length()>TICKER_MAX_CHARS&&
       millis()-tickerLastMs>=TICKER_INTERVAL_MS){
      tickerLastMs=millis();tickerOffset++;
      String s=fn+"    ";
      if(tickerOffset>=(int)s.length())tickerOffset=0;
    }
  }

  if(showOverlay&&millis()>overlayEndMs)showOverlay=false;

  if(sleepTimerOn&&isPlaying&&millis()>sleepTimerEnd){
    sleepTimerOn=false;fadeTarget=0;requestStop();
  }

  if(volOverlayActive&&millis()>volOverlayEndMs)
    volOverlayActive=false;

  if(trackEnded&&decodeState!=DECODE_RUNNING&&
     decodeState!=DECODE_STOP_REQ){
    trackEnded=false;tickerOffset=0;

    if(decodeTaskHandle!=nullptr){trackEnded=true;goto skip_autonext;}

    if(repeatMode==REPEAT_ONE){
      vibeRepeatOneStart=(vibeRepeatOneStart==0)?millis():vibeRepeatOneStart;
      doLoadTrack(currentTrack);
    }else if(shuffleMode){
      int nextShufIdx=(shuffleIdx+1)%(int)shuffleOrder.size();
      if(nextShufIdx==0&&repeatMode==REPEAT_OFF){
        isPlaying=false;
      }else{
        shuffleIdx=nextShufIdx;
        doLoadTrack(shuffleOrder[shuffleIdx]);
      }
    }else{
      int next=currentTrack+1;
      if(next>=(int)playlist.size()){
        if(repeatMode==REPEAT_ALL)doLoadTrack(0);
        else isPlaying=false;
      }else{
        doLoadTrack(next);
      }
    }
  }
  skip_autonext:;

  if(millis()-lastOledUpdateMs>=OLED_UPDATE_INTERVAL_MS){
    lastOledUpdateMs=millis();updateOLED();
  }

  delay(5);
}

// ----------------------------------------------------------
// PLAYLIST
// ----------------------------------------------------------
void scanPlaylist(){
  playlist.clear();
  File root=SD_MMC.open("/");
  if(!root||!root.isDirectory())return;
  File f=root.openNextFile();
  while(f){
    if(!f.isDirectory()){
      String name=String(f.name());
      int dot=name.lastIndexOf('.');
      if(dot>=0){
        String ext=name.substring(dot+1);ext.toUpperCase();
        if(ext=="MP3"||ext=="WAV")
          playlist.push_back(name.startsWith("/")?name:"/"+name);
      }
    }
    f=root.openNextFile();
  }
  root.close();
  std::sort(playlist.begin(),playlist.end());
  Serial.printf("[PLAYLIST] %d lagu.\n",(int)playlist.size());
}

void buildShuffleOrder(){
  shuffleOrder.resize(playlist.size());
  for(int i=0;i<(int)playlist.size();i++)shuffleOrder[i]=i;
  for(int i=(int)shuffleOrder.size()-1;i>0;i--){
    int j=random(0,i+1);
    int t=shuffleOrder[i];
    shuffleOrder[i]=shuffleOrder[j];shuffleOrder[j]=t;
  }
  shuffleIdx=0;
  for(int i=0;i<(int)shuffleOrder.size();i++){
    if(shuffleOrder[i]==currentTrack){shuffleIdx=i;break;}
  }
}

// ----------------------------------------------------------
// AKSI PLAYER
// ----------------------------------------------------------
void actionPlayPause(){
  if(playlist.empty())return;resetInteraction();
  if(isPlaying){
    fadeTarget=0;isPlaying=false;
  }else{
    if(!activeBundle||decodeState==DECODE_IDLE){
      fadeLevel=0;fadeTarget=256;doLoadTrack(currentTrack);
    }else{
      fadeLevel=0;fadeTarget=256;prefillReady=true;isPlaying=true;
    }
  }
}
void actionNext(){
  resetInteraction();notifySkip();
  if(shuffleMode){
    shuffleIdx=(shuffleIdx+1)%(int)shuffleOrder.size();
    requestLoadTrack(shuffleOrder[shuffleIdx]);
  }else{
    requestLoadTrack((currentTrack+1)%(int)playlist.size());
  }
}
void actionPrevious(){
  resetInteraction();notifySkip();
  if(shuffleMode){
    shuffleIdx=(shuffleIdx-1+(int)shuffleOrder.size())
               %(int)shuffleOrder.size();
    requestLoadTrack(shuffleOrder[shuffleIdx]);
  }else{
    requestLoadTrack((currentTrack-1+(int)playlist.size())
                     %(int)playlist.size());
  }
}
void actionRestartTrack(){resetInteraction();requestLoadTrack(currentTrack);}
void applyVolume(int v){
  currentVolume=constrain(v,VOL_MIN,VOL_MAX);
  a2dp.set_volume(currentVolume);
}
void actionVolUp(){
  resetInteraction();
  if(isMuted){isMuted=false;currentVolume=muteVolume;}
  applyVolume(currentVolume+VOL_STEP);
  notifyVolChange(true);
  showVolOverlay();savePrefs();
}
void actionVolDown(){
  resetInteraction();
  if(isMuted){isMuted=false;currentVolume=muteVolume;}
  applyVolume(currentVolume-VOL_STEP);
  notifyVolChange(false);
  showVolOverlay();savePrefs();
}
void actionVolUpNoSave(){
  resetInteraction();
  if(isMuted){isMuted=false;currentVolume=muteVolume;}
  applyVolume(currentVolume+VOL_STEP);
  notifyVolChange(true);showVolOverlay();
}
void actionVolDownNoSave(){
  resetInteraction();
  if(isMuted){isMuted=false;currentVolume=muteVolume;}
  applyVolume(currentVolume-VOL_STEP);
  notifyVolChange(false);showVolOverlay();
}
void scheduleVolSave(){volPendingSaveMs=millis();volPendingSave=true;}
void actionMuteToggle(){
  resetInteraction();
  if(!isMuted){muteVolume=currentVolume;currentVolume=0;isMuted=true;}
  else{currentVolume=muteVolume;isMuted=false;}
  a2dp.set_volume(currentVolume);showVolOverlay();
}
void actionResetVolume(){
  resetInteraction();isMuted=false;
  applyVolume(VOL_DEFAULT);showVolOverlay();savePrefs();
}
void actionShuffleToggle(){
  resetInteraction();shuffleMode=!shuffleMode;
  if(shuffleMode)buildShuffleOrder();savePrefs();
  String ls[2]={"SHUFFLE",shuffleMode?"ON":"OFF"};
  showOverlayText(ls,2,1500);
}
void actionRepeatToggle(){
  resetInteraction();
  repeatMode=(RepeatMode)(((int)repeatMode+1)%3);savePrefs();
  // Update repeat one tracking
  if(repeatMode==REPEAT_ONE) vibeRepeatOneStart=millis();
  else                       vibeRepeatOneStart=0;
  String ls[2]={"REPEAT",repeatLabel()};showOverlayText(ls,2,1500);
}
void actionShowSDInfo(){
  resetInteraction();
  uint64_t tot=SD_MMC.totalBytes()/(1024*1024);
  uint64_t use=SD_MMC.usedBytes()/(1024*1024);
  String ls[4]={"== SD INFO ==",
    "Total:"+String(tot)+"MB",
    "Pakai:"+String(use)+"MB",
    "Sisa:"+String(tot-use)+"MB"};
  showOverlayText(ls,4,3000);
}
void actionShowSysInfo(){
  resetInteraction();
  uint32_t us=millis()/1000,um=us/60;us%=60;
  float temp=temperatureRead();
  // Tampilkan vibe kategori
  const char* vibeStr="?";
  switch(vibeUserCurrent){
    case UVIBE_SEMANGAT:  vibeStr="Semangat"; break;
    case UVIBE_SENDU:     vibeStr="Sendu";    break;
    case UVIBE_BOSEN:     vibeStr="Bosen";    break;
    case UVIBE_NOSTALGIK: vibeStr="Nostalgi"; break;
    case UVIBE_FOKUS:     vibeStr="Fokus";    break;
    case UVIBE_GELISAH:   vibeStr="Gelisah";  break;
    case UVIBE_SANTAI:    vibeStr="Santai";   break;
    case UVIBE_EXCITED:   vibeStr="Excited";  break;
    default:              vibeStr="Unknown";  break;
  }
  String ls[5]={"== SISTEM v1.0.0 ==",
    "PSRAM:"+(psramFound()
      ?String(ESP.getPsramSize()/1024)+"KB"
      :String("N/A")),
    "Heap:"+String(ESP.getFreeHeap()/1024)+"KB T:"+String((int)temp)+"C",
    "Up:"+String(um)+"m"+String(us)+"s",
    "Vibe:"+String(vibeStr)};
  showOverlayText(ls,5,3500);
}
void actionRescanPlaylist(){
  resetInteraction();bool was=isPlaying;
  if(was)requestStop();
  scanPlaylist();buildShuffleOrder();
  if(currentTrack>=(int)playlist.size())currentTrack=0;
  String ls[3]={"== RESCAN SD ==",
    String(playlist.size())+" lagu","Selesai"};
  showOverlayText(ls,3,2000);
  if(was&&!playlist.empty()){
    pendingLoadIdx=currentTrack;pendingLoadTrack=true;
  }
}
void actionSleepTimerToggle(){
  resetInteraction();
  if(!sleepTimerOn){
    sleepTimerOn=true;
    sleepTimerEnd=millis()+(uint32_t)SLEEP_TIMER_MIN*60000;
    String ls[2]={"SLEEP TIMER",String(SLEEP_TIMER_MIN)+"mnt"};
    showOverlayText(ls,2,1500);
  }else{
    sleepTimerOn=false;
    String ls[2]={"SLEEP TIMER","OFF"};showOverlayText(ls,2,1500);
  }
}
void actionOpenExperimentalMenu(){
  resetInteraction();enterExperimentalMenu();
}

// ----------------------------------------------------------
// TOMBOL
// ----------------------------------------------------------
int updateBtn(BtnState& b){
  bool raw=digitalRead(b.pin);
  uint32_t now=millis();
  int evt=0;
  if(raw!=b.rawPrev){
    b.rawPrev=raw;b.debounceStart=now;b.debouncing=true;
  }
  if(b.debouncing&&(now-b.debounceStart>=DEBOUNCE_MS)){
    b.debouncing=false;
    if(raw!=b.prev){
      if(raw==LOW){
        b.isDown=true;b.pressedAt=now;b.longFired=false;
      }else if(b.isDown){
        if(!b.longFired)evt=1;
        b.isDown=false;
      }
      b.prev=raw;
    }
  }
  if(b.isDown&&!b.longFired&&now-b.pressedAt>=LONG_PRESS_MS){
    b.longFired=true;evt=2;
  }
  return evt;
}
bool comboPressed(BtnState& b1,BtnState& b2){
  if(!b1.isDown||!b2.isDown)return false;
  return abs((int32_t)(b1.pressedAt-b2.pressedAt))<=COMBO_WINDOW_MS;
}
void handleButtons(){
  static bool cAB=false,cAC=false,cBC=false,cCD=false,cAD=false;
  bool ab=comboPressed(btnA,btnB),ac=comboPressed(btnA,btnC);
  bool bc=comboPressed(btnB,btnC),cd=comboPressed(btnC,btnD);
  bool ad=comboPressed(btnA,btnD);
  if(ab&&!cAB){cAB=true;actionShowSDInfo();}
  if(ac&&!cAC){cAC=true;actionShowSysInfo();}
  if(bc&&!cBC){cBC=true;actionRescanPlaylist();}
  if(cd&&!cCD){cCD=true;actionSleepTimerToggle();}
  if(ad&&!cAD){cAD=true;actionOpenExperimentalMenu();}
  if(!ab)cAB=false;if(!ac)cAC=false;if(!bc)cBC=false;
  if(!cd)cCD=false;if(!ad)cAD=false;
  int eBoot=updateBtn(btnBoot),eA=updateBtn(btnA),eB=updateBtn(btnB);
  int eC=updateBtn(btnC),eD=updateBtn(btnD);
  if(eBoot==1)actionShuffleToggle();
  if(eBoot==2)actionRepeatToggle();
  if(!ab&&!ac&&!ad&&eA==1)actionPlayPause();
  if(!ab&&!ac&&!ad&&eA==2)actionNext();
  if(!ab&&!bc&&eB==1)actionPrevious();
  if(!ab&&!bc&&eB==2)actionRestartTrack();
  if(!ac&&!bc&&!cd&&eC==1)actionVolUp();
  if(!ac&&!bc&&!cd&&eC==2)actionMuteToggle();
  if(!cd&&!ad&&eD==1)actionVolDown();
  if(!cd&&!ad&&eD==2)actionResetVolume();
}

// ----------------------------------------------------------
// EXPERIMENTAL MENU
// ----------------------------------------------------------
String expMenuStatus(int i){
  switch(i){
    case 0:return hasSavedBT
      ?String(savedBTName).substring(0,10):"->Scan";
    case 1:return String(txGainLabel[currentTxGain]);
    case 2:return featureAVRCP?"[ON]":"[OFF]";
    case 3:return featureEQ
      ?(String("[ON]")+eqPresetName[eqPresetCurrent]):"[OFF]";
    case 4:return featureCrossfade?"[ON]":"[OFF]";
    default:return "";
  }
}
void enterExperimentalMenu(){
  appMode=MODE_EXPERIMENTAL_MENU;expMenuSelected=0;
  confirmDisplayActive=false;resetInteraction();
  if(isPlaying)requestStop();drawExperimentalMenu();
}
void exitExperimentalMenu(){
  appMode=MODE_PLAYER;confirmDisplayActive=false;resetInteraction();
  if(btConnected&&!playlist.empty()){
    if(decodeState==DECODE_IDLE&&!isPlaying)doLoadTrack(currentTrack);
    else if(!isPlaying){
      pendingLoadIdx=currentTrack;pendingLoadTrack=true;
    }
  }
  updateOLED();
}
void drawExperimentalMenu(){
  display.clearDisplay();display.setTextColor(SSD1306_WHITE);
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  display.setCursor(8,2);display.print("* EXPERIMENTAL MENU");
  display.setTextColor(SSD1306_WHITE);
  static int mOff=0;
  if(expMenuSelected<mOff)mOff=expMenuSelected;
  if(expMenuSelected>=mOff+4)mOff=expMenuSelected-3;
  if(mOff<0)mOff=0;
  for(int row=0;row<4;row++){
    int idx=mOff+row;if(idx>=EXP_MENU_ITEMS)break;
    int y=13+row*12;String st=expMenuStatus(idx);
    if(idx==expMenuSelected){
      display.fillRect(0,y,127,11,SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(2,y+2);display.print(">");
    }else{
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(2,y+2);display.print(" ");
    }
    display.setCursor(8,y+2);display.print(expMenuNames[idx]);
    int sx=128-(int)st.length()*6-2;
    if(sx>64){display.setCursor(sx,y+2);display.print(st);}
    display.setTextColor(SSD1306_WHITE);
  }
  display.fillRect(0,61,128,3,SSD1306_WHITE);
  display.setCursor(0,53);
  display.print("[C/D]:Nav  [A]:Pilih  [BOOT]:Exit");
  display.display();
}
void handleButtonsExpMenu(){
  if(confirmDisplayActive)return;
  int eBoot=updateBtn(btnBoot),eA=updateBtn(btnA);
  int eC=updateBtn(btnC),eD=updateBtn(btnD);
  if(eBoot==1){exitExperimentalMenu();return;}
  if(eC==1){
    expMenuSelected=(expMenuSelected-1+EXP_MENU_ITEMS)%EXP_MENU_ITEMS;
    drawExperimentalMenu();resetInteraction();
  }
  if(eD==1){
    expMenuSelected=(expMenuSelected+1)%EXP_MENU_ITEMS;
    drawExperimentalMenu();resetInteraction();
  }
  if(eA==1){
    resetInteraction();
    switch(expMenuSelected){
      case 0:enterBTScanMode();break;
      case 1:enterTxGainMode();break;
      case 2:
        featureAVRCP=!featureAVRCP;
        if(featureAVRCP){
          if(!avrcpStackReady)initAVRCPStack();
          if(btConnected&&avrcpStackReady){
            avrcpInitPending=true;avrcpInitRequestMs=millis();
          }
        }else deinitAVRCP();
        savePrefs();
        {display.clearDisplay();
         display.fillRect(0,0,128,12,SSD1306_WHITE);
         display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
         display.setCursor(14,2);display.print("AVRCP REMOTE");
         display.setTextColor(SSD1306_WHITE);display.setTextSize(2);
         display.setCursor(featureAVRCP?34:22,24);
         display.print(featureAVRCP?"ON":"OFF");
         display.setTextSize(1);display.setCursor(4,50);
         display.print(featureAVRCP?"Aktif & Tersimpan":"Nonaktif");
         display.display();
         confirmDisplayActive=true;
         confirmDisplayEndMs=millis()+CONFIRM_DISPLAY_MS;}
        break;
      case 3:enterEQMode();break;
      case 4:
        featureCrossfade=!featureCrossfade;savePrefs();
        {display.clearDisplay();
         display.fillRect(0,0,128,12,SSD1306_WHITE);
         display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
         display.setCursor(20,2);display.print("CROSSFADE");
         display.setTextColor(SSD1306_WHITE);display.setTextSize(2);
         display.setCursor(featureCrossfade?34:22,24);
         display.print(featureCrossfade?"ON":"OFF");
         display.setTextSize(1);display.setCursor(4,50);
         display.print(featureCrossfade?"Aktif":"Nonaktif");
         display.display();
         confirmDisplayActive=true;
         confirmDisplayEndMs=millis()+CONFIRM_DISPLAY_MS;}
        break;
    }
  }
}

// ----------------------------------------------------------
// TX GAIN MODE
// ----------------------------------------------------------
void enterTxGainMode(){
  appMode=MODE_TX_GAIN;pendingTxGain=currentTxGain;
  confirmDisplayActive=false;resetInteraction();drawTxGainScreen();
}
void exitTxGainMode(){
  appMode=MODE_PLAYER;confirmDisplayActive=false;resetInteraction();
  if(btConnected&&!isPlaying&&!playlist.empty()){
    if(decodeState==DECODE_IDLE)doLoadTrack(currentTrack);
    else{pendingLoadIdx=currentTrack;pendingLoadTrack=true;}
  }
  updateOLED();
}
void drawTxGainScreen(){
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  display.setCursor(6,2);display.print("  BT TX POWER GAIN");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2,15);
  display.print("Aktif:");display.print(txGainLabel[currentTxGain]);
  display.drawLine(0,24,127,24,SSD1306_WHITE);
  display.setCursor(2,27);display.print("Pilihan:");
  display.setTextSize(2);
  int xp=(128-(int)strlen(txGainLabel[pendingTxGain])*12)/2;
  if(xp<0)xp=0;
  display.setCursor(xp,37);display.print(txGainLabel[pendingTxGain]);
  display.setTextSize(1);
  if(pendingTxGain>0){display.setCursor(0,38);display.print("<");}
  if(pendingTxGain<TX_GAIN_LEVELS-1){
    display.setCursor(122,38);display.print(">");
  }
  display.drawLine(0,52,127,52,SSD1306_WHITE);
  display.setCursor(0,55);display.print("[C]-");
  for(int i=0;i<TX_GAIN_LEVELS;i++){
    int bx=28+i*10;
    if(i<=pendingTxGain)display.fillRect(bx,55,8,8,SSD1306_WHITE);
    else display.drawRect(bx,55,8,8,SSD1306_WHITE);
  }
  display.setCursor(112,55);display.print("+[D]");
  display.display();
}
void handleButtonsTxGain(){
  int eBoot=updateBtn(btnBoot),eA=updateBtn(btnA);
  int eC=updateBtn(btnC),eD=updateBtn(btnD);
  if(eBoot==1){
    appMode=MODE_EXPERIMENTAL_MENU;
    resetInteraction();drawExperimentalMenu();return;
  }
  if(eA==1){
    applyTxGain(pendingTxGain);savePrefs();
    display.clearDisplay();
    display.fillRect(0,0,128,12,SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
    display.setCursor(12,2);display.print("TX GAIN TERSIMPAN");
    display.setTextColor(SSD1306_WHITE);display.setTextSize(2);
    int xp2=(128-(int)strlen(txGainLabel[currentTxGain])*12)/2;
    if(xp2<0)xp2=0;
    display.setCursor(xp2,24);display.print(txGainLabel[currentTxGain]);
    display.setTextSize(1);display.setCursor(30,50);display.print("OK!");
    display.display();
    confirmDisplayActive=true;
    confirmDisplayEndMs=millis()+CONFIRM_DISPLAY_MS;return;
  }
  if(eC==1){
    pendingTxGain=max(0,pendingTxGain-1);
    drawTxGainScreen();resetInteraction();
  }
  if(eD==1){
    pendingTxGain=min(TX_GAIN_LEVELS-1,pendingTxGain+1);
    drawTxGainScreen();resetInteraction();
  }
}

// ----------------------------------------------------------
// EQUALIZER MODE
// ----------------------------------------------------------
void applyEQPreset(int idx){eqPresetCurrent=idx;buildEQCoefs(idx);}
void enterEQMode(){
  appMode=MODE_EQUALIZER;eqPresetPending=eqPresetCurrent;
  confirmDisplayActive=false;resetInteraction();drawEQScreen();
}
void exitEQMode(){
  appMode=MODE_EXPERIMENTAL_MENU;confirmDisplayActive=false;
  resetInteraction();drawExperimentalMenu();
}
void drawEQScreen(){
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  display.setCursor(14,2);display.print("EQUALIZER MENU");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2,14);display.print("Status:");
  display.print(featureEQ?"ON  ":"OFF ");
  display.print("|");display.print(eqPresetName[eqPresetCurrent]);
  display.drawLine(0,22,127,22,SSD1306_WHITE);
  for(int i=0;i<EQ_PRESETS;i++){
    int y=24+i*7;
    if(i==eqPresetPending){
      display.fillRect(0,y-1,127,8,SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    }else display.setTextColor(SSD1306_WHITE);
    display.setCursor(2,y);
    display.print(i==eqPresetPending?">":" ");
    display.print(eqPresetName[i]);
    char g[24];
    snprintf(g,sizeof(g)," L:%+d M:%+d H:%+d",
      eqPresetGain[i][0],eqPresetGain[i][1],eqPresetGain[i][2]);
    display.print(g);
    display.setTextColor(SSD1306_WHITE);
  }
  display.drawLine(0,59,127,59,SSD1306_WHITE);
  display.setCursor(0,61);
  display.print("[C/D]Pilih [A]OK [B]On/Off");
  display.display();
}
void handleButtonsEQ(){
  int eBoot=updateBtn(btnBoot),eA=updateBtn(btnA),eB=updateBtn(btnB);
  int eC=updateBtn(btnC),eD=updateBtn(btnD);
  if(eBoot==1){exitEQMode();return;}
  if(eC==1){
    eqPresetPending=(eqPresetPending-1+EQ_PRESETS)%EQ_PRESETS;
    drawEQScreen();resetInteraction();
  }
  if(eD==1){
    eqPresetPending=(eqPresetPending+1)%EQ_PRESETS;
    drawEQScreen();resetInteraction();
  }
  if(eB==1){
    featureEQ=!featureEQ;savePrefs();
    display.clearDisplay();
    display.fillRect(0,0,128,12,SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
    display.setCursor(20,2);display.print("EQUALIZER");
    display.setTextColor(SSD1306_WHITE);display.setTextSize(2);
    display.setCursor(featureEQ?34:22,24);
    display.print(featureEQ?"ON":"OFF");
    display.setTextSize(1);display.setCursor(2,50);
    display.print(featureEQ?eqPresetName[eqPresetCurrent]:"EQ Dimatikan");
    display.display();
    confirmDisplayActive=true;
    confirmDisplayEndMs=millis()+CONFIRM_DISPLAY_MS;return;
  }
  if(eA==1){
    applyEQPreset(eqPresetPending);featureEQ=true;savePrefs();
    display.clearDisplay();
    display.fillRect(0,0,128,12,SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
    display.setCursor(14,2);display.print("EQ DITERAPKAN");
    display.setTextColor(SSD1306_WHITE);display.setTextSize(2);
    display.setCursor(4,18);display.print(eqPresetName[eqPresetCurrent]);
    display.setTextSize(1);
    char g[40];
    snprintf(g,sizeof(g),"L:%+d  M:%+d  H:%+d",
      eqPresetGain[eqPresetCurrent][0],
      eqPresetGain[eqPresetCurrent][1],
      eqPresetGain[eqPresetCurrent][2]);
    display.setCursor(2,40);display.print(g);
    display.setCursor(2,52);display.print("Tersimpan!");
    display.display();
    confirmDisplayActive=true;
    confirmDisplayEndMs=millis()+CONFIRM_DISPLAY_MS;return;
  }
}

// ----------------------------------------------------------
// BT SCANNER
// ----------------------------------------------------------
void enterBTScanMode(){
  appMode=MODE_BT_SCAN;scannedCount=0;
  scanListOffset=0;scanSelected=0;
  scanDone=false;isScanning=false;
  memset(scannedDevices,0,sizeof(scannedDevices));
  resetInteraction();if(isPlaying)requestStop();
  esp_bt_gap_register_callback(gap_callback);
  drawScannerScreen();delay(300);startBTScan();
}
void exitBTScanMode(){
  stopBTScan();appMode=MODE_PLAYER;resetInteraction();updateOLED();
  if(btConnected&&!isPlaying&&!playlist.empty()){
    if(decodeState==DECODE_IDLE)doLoadTrack(currentTrack);
    else{pendingLoadIdx=currentTrack;pendingLoadTrack=true;}
  }
}
void startBTScan(){
  scannedCount=0;scanListOffset=0;scanSelected=0;scanDone=false;
  memset(scannedDevices,0,sizeof(scannedDevices));
  esp_err_t r=esp_bt_gap_start_discovery(
    ESP_BT_INQ_MODE_GENERAL_INQUIRY,BT_SCAN_DURATION,0);
  isScanning=(r==ESP_OK);
  if(!isScanning)scanDone=true;
  drawScannerScreen();
}
void stopBTScan(){
  if(isScanning){esp_bt_gap_cancel_discovery();isScanning=false;}
}
void connectToScannedDevice(int idx){
  if(idx<0||idx>=scannedCount)return;
  pendingBTName=String(scannedDevices[idx].name);
  btRestartState=BT_RESTART_STOP_REQ;btRestartMs=millis();
  stopBTScan();
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  display.setCursor(10,2);display.print(">> MENGHUBUNGI <<");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,16);
  display.print(String(scannedDevices[idx].name).substring(0,21));
  display.drawLine(0,26,127,26,SSD1306_WHITE);
  display.setCursor(2,30);
  display.printf("RSSI: %d dBm",scannedDevices[idx].rssi);
  display.setCursor(2,42);display.print("Mohon tunggu...");
  display.display();
}
void actionScanScrollUp(){
  if(!scannedCount)return;
  scanSelected=max(0,scanSelected-1);
  if(scanSelected<scanListOffset)scanListOffset=scanSelected;
  drawScannerScreen();resetInteraction();
}
void actionScanScrollDown(){
  if(!scannedCount)return;
  scanSelected=min(scannedCount-1,scanSelected+1);
  if(scanSelected>=scanListOffset+OLED_LIST_ROWS)
    scanListOffset=scanSelected-OLED_LIST_ROWS+1;
  drawScannerScreen();resetInteraction();
}
void actionScanSelect(){
  if(!scannedCount){startBTScan();return;}
  connectToScannedDevice(scanSelected);resetInteraction();
}
void actionScanRescan(){
  if(isScanning){stopBTScan();delay(300);}
  startBTScan();resetInteraction();
}
void actionScanExit(){exitBTScanMode();resetInteraction();}
void handleButtonsScanMode(){
  int eBoot=updateBtn(btnBoot),eA=updateBtn(btnA),eB=updateBtn(btnB);
  int eC=updateBtn(btnC),eD=updateBtn(btnD);
  if(eBoot==1)actionScanExit();
  if(eA==1)actionScanSelect();
  if(eB==1)actionScanRescan();
  if(eC==1)actionScanScrollUp();
  if(eD==1)actionScanScrollDown();
}

// ----------------------------------------------------------
// DRAW PLAYER SCREEN
// ----------------------------------------------------------
void drawPlayerScreen(){
  display.clearDisplay();

  // HEADER
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  if(btConnected){
    display.setCursor(1,2);display.print("\x10 BT");
    char nb[8];
    snprintf(nb,sizeof(nb),"%02d/%02d",
      currentTrack+1,(int)playlist.size());
    display.setCursor(94,2);display.print(nb);
    const char* stStr=isPlaying?(isMuted?"MUTE":"PLAY"):"STOP";
    display.setCursor(46,2);display.print(stStr);
  }else{
    const char* ani[]={"-BT-","\\BT/","|BT|","/BT\\"};
    display.setCursor(2,2);display.print(ani[animFrame%4]);
    display.setCursor(40,2);display.print("MENCARI...");
  }
  display.setTextColor(SSD1306_WHITE);

  // NAMA LAGU
  display.drawLine(0,12,127,12,SSD1306_WHITE);
  if(!playlist.empty()){
    String fn=stripExt(getFileName(playlist[currentTrack]));
    if((int)fn.length()<=TICKER_MAX_CHARS){
      int px=max(0,(TICKER_MAX_CHARS-(int)fn.length())/2)*6;
      display.setCursor(px,14);display.print(fn);
    }else{
      String s=fn+"    ";
      int off=tickerOffset%(int)s.length();
      String vis=s.substring(off);
      if((int)vis.length()<TICKER_MAX_CHARS)
        vis+=s.substring(0,TICKER_MAX_CHARS-vis.length());
      display.setCursor(0,14);
      display.print(vis.substring(0,TICKER_MAX_CHARS));
    }
  }else{
    display.setCursor(16,14);display.print("- Tidak ada lagu -");
  }

  // PROGRESS BAR
  display.drawLine(0,22,127,22,SSD1306_WHITE);
  display.setCursor(0,24);display.print("PR");
  {
    const int bx=14,by=24,bw=90,bh=7;
    int fill=0,pct=0;
    size_t bpt=(size_t)bytesPlayedTotal;
    size_t tdf=(size_t)totalDecodedFinal;
    size_t bdt=(size_t)bytesDecodedTotal;
    size_t fp_v=(size_t)fileProgress;
    size_t fts=(size_t)fileTotalSize;
    if(isPlaying&&tdf>0){
      size_t played=(bpt<tdf)?bpt:tdf;
      pct =(int)((played*100ULL)/tdf);
      fill=constrain((int)((played*(bw-2))/tdf),0,bw-2);
    }else if(isPlaying&&bdt>0){
      size_t ep=(fts>0)?fts:1;
      size_t fp2=(fp_v<ep)?fp_v:ep;
      pct =(int)((fp2*100ULL)/ep);
      fill=constrain((int)((fp2*(bw-2))/ep),0,bw-2);
    }
    display.drawRect(bx,by,bw,bh,SSD1306_WHITE);
    if(progBarVisual<0.f) progBarVisual=(float)fill;
    progBarVisual+=((float)fill-progBarVisual)*0.4f;
    int fillVisual=(int)(progBarVisual+0.5f);
    if(fillVisual>0)display.fillRect(bx+1,by+1,fillVisual,bh-2,SSD1306_WHITE);
    display.setCursor(108,24);
    char pb[5];snprintf(pb,sizeof(pb),"%3d%%",pct);display.print(pb);
  }

  // INFO ROW
  display.drawLine(0,32,127,32,SSD1306_WHITE);
  display.setCursor(0,34);
  if(!playlist.empty())display.print(isMp3?"MP3":"WAV");
  else                 display.print("---");
  display.setCursor(30,34);
  if(repeatMode==REPEAT_ONE)      display.print("RPT:1");
  else if(repeatMode==REPEAT_ALL) display.print("RPT:A");
  else                            display.print("RPT:X");
  display.setCursor(72,34);
  display.print(shuffleMode?"SHF:Y":"SHF:N");
  display.setCursor(106,34);
  display.print(featureEQ?"EQ":"--");

  if(sleepTimerOn){
    int32_t sisa=(int32_t)((sleepTimerEnd-millis())/1000);
    if(sisa<0)sisa=0;
    char sb[20];snprintf(sb,sizeof(sb),"ZZZ %dm%ds",sisa/60,sisa%60);
    display.setCursor(0,41);display.print(sb);
  }

  drawFaceArea();
}

// ----------------------------------------------------------
// UPDATE OLED
// ----------------------------------------------------------
void updateOLED(){
  if(appMode==MODE_BT_SCAN){drawScannerScreen();return;}
  if(appMode==MODE_EXPERIMENTAL_MENU){drawExperimentalMenu();return;}
  if(appMode==MODE_TX_GAIN){drawTxGainScreen();return;}
  if(appMode==MODE_EQUALIZER){drawEQScreen();return;}

  if(showOverlay){
    display.clearDisplay();
    display.fillRect(0,0,128,12,SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
    display.setCursor(28,2);display.print("[ INFO ]");
    display.setTextColor(SSD1306_WHITE);
    int sy=16,lh=11;
    if(overlayCount<=2){sy=22;lh=18;}
    else if(overlayCount<=3){sy=16;lh=13;}
    for(int i=0;i<overlayCount;i++){
      int x=max(0,(int)(128-(int)overlayLines[i].length()*6)/2);
      display.setCursor(x,sy+i*lh);display.print(overlayLines[i]);
    }
    display.display();return;
  }

  drawPlayerScreen();
  display.display();
}

// ----------------------------------------------------------
// DRAW SCANNER SCREEN
// ----------------------------------------------------------
void drawScannerScreen(){
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  display.setCursor(10,2);display.print("BT DEVICE SCANNER");
  display.setTextColor(SSD1306_WHITE);
  if(isScanning){
    const char* sp[]={"-","\\","|","/"};
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(116,2);display.print(sp[animFrame%4]);
    display.setTextColor(SSD1306_WHITE);
  }else if(scannedCount>0){
    char c[5];snprintf(c,sizeof(c),"%2d",scannedCount);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(116,2);display.print(c);
    display.setTextColor(SSD1306_WHITE);
  }
  if(!scannedCount){
    display.setCursor(4,18);
    if(isScanning){
      display.print("Memindai perangkat...");
      const char* dot[]={".  ",".. ","..."};
      display.setCursor(4,30);display.print(dot[animFrame%3]);
    }else{
      display.print("Tidak ada device.");
      display.setCursor(4,30);display.print("[B] Scan ulang");
    }
  }else{
    for(int row=0;row<OLED_LIST_ROWS;row++){
      int idx=scanListOffset+row;if(idx>=scannedCount)break;
      int y=13+row*11;
      if(idx==scanSelected){
        display.fillRect(0,y,127,10,SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }else display.setTextColor(SSD1306_WHITE);
      String nm=String(scannedDevices[idx].name);
      if(nm.length()>14)nm=nm.substring(0,13)+".";
      display.setCursor(2,y+1);display.print(nm);
      char rs[6];
      snprintf(rs,sizeof(rs),"%4d",scannedDevices[idx].rssi);
      display.setCursor(100,y+1);display.print(rs);
      display.setTextColor(SSD1306_WHITE);
    }
    if(scannedCount>OLED_LIST_ROWS){
      int sbH=44,sbY=13;
      int tH=sbH*OLED_LIST_ROWS/scannedCount;
      int tY=sbY+sbH*scanListOffset/scannedCount;
      display.drawRect(126,sbY,2,sbH,SSD1306_WHITE);
      display.fillRect(126,tY,2,tH,SSD1306_WHITE);
    }
  }
  display.drawLine(0,57,127,57,SSD1306_WHITE);
  display.setCursor(0,59);
  display.print("[A]Pilih [B]Scan [C/D]Nav");
  display.display();
}

// ----------------------------------------------------------
// UTILITAS
// ----------------------------------------------------------
void showOverlayText(String lines[],int count,uint32_t durMs){
  for(int i=0;i<count&&i<5;i++)overlayLines[i]=lines[i];
  overlayCount=count;showOverlay=true;overlayEndMs=millis()+durMs;
  updateOLED();
}
void oledSplash(bool hasPSRAM){
  display.clearDisplay();
  display.fillRect(0,0,128,16,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  display.setCursor(4,1);display.print("BT MUSIC PLAYER v11.3");
  display.setCursor(4,9);display.print("RYNE MULTI-VIBE");
  display.setTextColor(SSD1306_WHITE);
  display.drawLine(0,16,127,16,SSD1306_WHITE);
  display.setCursor(4,20);display.print("Memuat sistem...");
  display.setCursor(4,30);
  display.printf("PSRAM: %s",hasPSRAM?"1MB OK  ":"HEAP   ");
  display.setCursor(4,40);display.print("Ring Buffer: OK");
  display.setCursor(4,50);display.print("RYNE Multi-Vibe: INIT");
  display.drawLine(0,60,127,60,SSD1306_WHITE);
  display.display();
}
void oledError(const char* l1,const char* l2){
  display.clearDisplay();
  display.fillRect(0,0,128,12,SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);display.setTextSize(1);
  display.setCursor(24,2);display.print("!! KESALAHAN !!");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2,16);display.print(l1);
  display.drawLine(0,26,127,26,SSD1306_WHITE);
  display.setCursor(2,30);display.print(l2);
  display.display();
}
void resetInteraction(){
  lastInteractionMs=millis();
  if(oledDimmed){display.dim(false);oledDimmed=false;}
}
String getFileName(const String& p){
  int s=p.lastIndexOf('/');return s>=0?p.substring(s+1):p;
}
String stripExt(const String& f){
  int d=f.lastIndexOf('.');return d>0?f.substring(0,d):f;
}
String repeatLabel(){
  return repeatMode==REPEAT_OFF?"OFF"
        :repeatMode==REPEAT_ALL?"ALL":"ONE";
}
void savePrefs(){
  Preferences p;p.begin("btmusic",false);
  p.putInt("volume",currentVolume);
  p.putInt("track",currentTrack);
  p.putInt("repeat",(int)repeatMode);
  p.putBool("shuffle",shuffleMode);
  p.putString("btname",String(savedBTName));
  p.putInt("txgain",currentTxGain);
  p.putBool("avrcp",featureAVRCP);
  p.putBool("eq",featureEQ);
  p.putInt("eqpreset",eqPresetCurrent);
  p.putBool("crossfade",featureCrossfade);
  p.end();
}
void loadPrefs(){
  Preferences p;p.begin("btmusic",true);
  currentVolume  =p.getInt("volume",VOL_DEFAULT);
  currentTrack   =p.getInt("track",0);
  repeatMode     =(RepeatMode)p.getInt("repeat",0);
  shuffleMode    =p.getBool("shuffle",false);
  String bn      =p.getString("btname","");
  strncpy(savedBTName,bn.c_str(),63);savedBTName[63]=0;
  currentTxGain  =constrain(
    p.getInt("txgain",TX_GAIN_DEFAULT),0,TX_GAIN_LEVELS-1);
  featureAVRCP   =p.getBool("avrcp",false);
  featureEQ      =p.getBool("eq",false);
  eqPresetCurrent=constrain(
    p.getInt("eqpreset",0),0,EQ_PRESETS-1);
  featureCrossfade=p.getBool("crossfade",false);
  p.end();
}
