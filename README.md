# README.md

```markdown
# 🎵 ESP32 Bluetooth Music Player v1.2.0
### Powered by RYNE Engine — Multi-Signal Vibe Estimation + PSRAM Learning

![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![Version](https://img.shields.io/badge/Version-1.2.0-green)
![License](https://img.shields.io/badge/License-MIT-yellow)
![Language](https://img.shields.io/badge/Language-C%2B%2B%20Arduino-orange)

---

## 📖 Daftar Isi

- [Tentang Proyek](#-tentang-proyek)
- [Fitur Lengkap](#-fitur-lengkap)
- [Hardware yang Dibutuhkan](#-hardware-yang-dibutuhkan)
- [Skema Wiring](#-skema-wiring)
- [Instalasi Library](#-instalasi-library)
- [Cara Upload](#-cara-upload)
- [Persiapan SD Card](#-persiapan-sd-card)
- [Panduan Tombol](#-panduan-tombol)
- [RYNE Engine](#-ryne-engine)
- [Vibe Learning System](#-vibe-learning-system)
- [OLED Display](#-oled-display)
- [Equalizer](#-equalizer)
- [Bluetooth Scanner](#-bluetooth-scanner)
- [Experimental Menu](#-experimental-menu)
- [Konfigurasi Lanjutan](#-konfigurasi-lanjutan)
- [Troubleshooting](#-troubleshooting)
- [Changelog](#-changelog)
- [Lisensi](#-lisensi)

---

## 🎯 Tentang Proyek

ESP32 Bluetooth Music Player adalah pemutar musik portabel berbasis ESP32 yang
memutar file MP3 dan WAV dari SD Card lalu mengirimkan audio via **Bluetooth
A2DP** ke speaker Bluetooth manapun.

Yang membedakan proyek ini dari pemutar MP3 biasa adalah **RYNE Engine** —
sebuah sistem kecerdasan buatan ringan yang berjalan langsung di dalam ESP32.
RYNE mengamati perilaku pendengar secara real-time (seberapa sering skip, naik
turun volume, durasi dengerin, mode repeat) lalu menyimpulkan "vibe" atau mood
pendengar saat itu, dan menampilkan ekspresi wajah animasi yang sesuai di layar
OLED.

Pada board ESP32 dengan PSRAM (seperti ESP32-CAM AI Thinker), RYNE bahkan bisa
**belajar** dari histori perilaku pendengar menggunakan algoritma gradient
descent ringan, sehingga prediksi vibe makin akurat seiring waktu.

---

## ✨ Fitur Lengkap

### 🎵 Pemutaran Audio
| Fitur | Detail |
|-------|--------|
| Format | MP3 & WAV |
| Sumber | SD Card (FAT32, file di root `/`) |
| Output | Bluetooth A2DP Source |
| Codec MP3 | Helix MP3 Decoder |
| Codec WAV | Built-in WAV Decoder |
| Bitrate MP3 | Disarankan 128kbps–320kbps |
| Sample Rate | 44100 Hz Stereo 16-bit |
| Ring Buffer | 2MB (PSRAM) / 64KB (Heap) |

### 🔵 Bluetooth
| Fitur | Detail |
|-------|--------|
| Protokol | Bluetooth Classic A2DP Source |
| Target | Speaker / Headphone / Receiver BT apapun |
| Auto Reconnect | Ya, setiap 8 detik |
| TX Power | Adjustable -12dBm s/d +9dBm (8 level) |
| AVRCP | Remote control dari speaker (opsional) |
| BT Scanner | Scan & pilih perangkat BT terdekat |
| Multi Device | Simpan nama perangkat favorit di flash |

### 🎛️ Kontrol Pemutaran
| Fitur | Detail |
|-------|--------|
| Play / Pause | Tombol A (tekan pendek) |
| Next Track | Tombol A (tekan panjang) |
| Previous Track | Tombol B (tekan pendek) |
| Restart Track | Tombol B (tekan panjang) |
| Volume Up | Tombol C (tekan pendek) |
| Volume Down | Tombol D (tekan pendek) |
| Mute Toggle | Tombol C (tekan panjang) |
| Reset Volume | Tombol D (tekan panjang) |
| Shuffle Toggle | Tombol BOOT (tekan pendek) |
| Repeat Mode | Tombol BOOT (tekan panjang) |

### 🔀 Mode Pemutaran
| Mode | Keterangan |
|------|-----------|
| Normal | Putar berurutan, berhenti di akhir |
| Repeat All | Ulangi semua lagu terus-menerus |
| Repeat One | Ulangi satu lagu saja |
| Shuffle | Acak urutan putar |
| Shuffle + Repeat All | Acak + terus berputar |

### 🎚️ Equalizer 5 Preset
| Preset | Bass | Mid | Treble | Cocok Untuk |
|--------|------|-----|--------|-------------|
| Flat | 0dB | 0dB | 0dB | Audio natural, referensi |
| Bass | +8dB | 0dB | -2dB | EDM, Hip-Hop, R&B |
| Pop | +3dB | +2dB | +3dB | Pop, K-Pop, Indie |
| Rock | +5dB | -2dB | +4dB | Rock, Metal, Punk |
| Jazz | +4dB | +2dB | -1dB | Jazz, Blues, Acoustic |

Equalizer menggunakan **biquad filter 3 band** (Low Shelf, Peaking, High Shelf)
yang diproses secara real-time di dalam audio callback.

### 🎭 Crossfade
- Transisi halus antar lagu dengan **64 step crossfade**
- Fade out lagu lama → Fade in lagu baru
- Bisa diaktifkan/nonaktifkan di Experimental Menu

### 😴 Sleep Timer
- Timer otomatis **30 menit** (konfigurasi via `#define SLEEP_TIMER_MIN`)
- Setelah timer habis, audio fade out dan berhenti otomatis
- Countdown tampil di layar OLED

### 💾 Persistensi Pengaturan
Semua pengaturan berikut tersimpan di **flash ESP32** (NVS/Preferences) dan
tetap ada setelah restart:
- Volume terakhir
- Track terakhir
- Mode repeat & shuffle
- Nama perangkat BT favorit
- TX Power level
- Status AVRCP, EQ, Crossfade
- Preset EQ aktif

---

## 🧠 RYNE Engine

RYNE (**Real-time Your-emotion Noticing Engine**) adalah otak dari player ini.
RYNE berjalan sebagai **FreeRTOS task terpisah** di Core 1 setiap 500ms,
mengamati puluhan sinyal sekaligus.

### Sinyal yang Dipantau RYNE

#### 📊 Buffer & Decode Signals
```
bufferHealthy   → Ring buffer cukup terisi (>prefill threshold)
bufferCritical  → Buffer hampir habis (<8KB) saat sedang play
bufferUnderrun  → Buffer kosong saat decode masih berjalan
decodeActive    → Decode task sedang berjalan
decodeStuck     → Decode tidak ada progress >3 detik
decodeEOF       → File audio sudah habis di-decode
playbackStuck   → Playback tidak ada progress >3 detik
```

#### 🎵 Track Signals
```
trackEnding     → Progress file sudah >90%
skipStorm       → Skip >= 3 kali dalam 15 detik
```

#### 🔵 Bluetooth Signals
```
btConnected     → Status koneksi BT saat ini
btReconnecting  → Sedang mencoba reconnect (<8 detik putus)
btUnstable      → Drop >= 2 kali dalam 60 detik
```

#### 🎶 Audio Vibe Signals
```
vibeEnergetic   → RMS audio >= 9000 (lagu keras/energik)
vibeCalm        → RMS audio 1800-4000 (lagu tenang)
vibeQuiet       → RMS audio < 1800 (lagu sangat pelan)
```

#### 👤 User Behavior Signals
```
skipRate        → Frekuensi skip dalam 2 menit terakhir
volUpCount      → Berapa kali volume dinaikkan (30 detik)
volDownCount    → Berapa kali volume diturunkan (30 detik)
listenDuration  → Berapa lama track ini sudah didengarkan
repeatOneActive → Apakah sedang repeat one > 60 detik
```

### Prioritas Pesan RYNE

RYNE menampilkan pesan di area wajah dengan prioritas:

```
1. CRITICAL  → decode macet / playback stuck
2. WARNING   → buffer kritis / BT tidak stabil / BT reconnecting
3. BEHAVIORAL→ skip storm / lagu hampir habis (adaptif per vibe)
4. VIBE INFO → update vibe setiap 20 detik (variatif)
```

---

## 🎭 Vibe Learning System

### 8 Kategori Vibe User

| Kategori | Trigger | Ekspresi Wajah | Contoh Pesan |
|----------|---------|----------------|--------------|
| 😤 Semangat | Vol naik + lagu keras | Happy | "gas terus bestie!" |
| 😔 Sendu | Lagu pelan + dengerin lama | Sleepy | "lagi mellow nih..." |
| 😒 Bosen | Skip >= 3x dalam 2 menit | Frustrated | "ga ada yang cocok?" |
| 🥺 Nostalgik | Repeat One > 60 detik | Smirk | "lagu favorit nih~" |
| 🧠 Fokus | Dengerin > 90 detik tanpa skip | Chill | "lagi fokus nih~" |
| 😰 Gelisah | Skip + vol naik turun | Shock | "lagi gelisah ya?" |
| 😎 Santai | Audio calm + minim interaksi | Chill | "santai banget~" |
| 🤩 Excited | Vol sering naik + lagu keras | Shock | "EXCITED banget!!!" |

### Weighted Sigmoid Scoring

Setiap kategori vibe dinilai menggunakan **logistic regression 1-vs-rest**:

```
score(kategori) = sigmoid( w1×skipRate + w2×volUp + w3×volDown
                         + w4×loudness + w5×listenDur
                         + w6×repeatOne + w7×quietFlag
                         + w8×calmFlag + bias )

sigmoid(z) = 1 / (1 + e^(-z))

Pemenang = argmax(score) dengan threshold minimum 0.35
```

### Bobot Default (Hard-coded Prior)

Bobot awal dirancang agar RYNE langsung masuk akal sejak boot pertama,
bahkan sebelum ada data training sama sekali:

```
BOSEN    : w_skip=+4.0  (skip rate tinggi = bosen)
NOSTALGIK: w_repeatOne=+4.0 (repeat lama = nostalgia)
GELISAH  : w_skip=+2.5, w_volUp=+1.5, w_volDown=+1.5
EXCITED  : w_volUp=+2.0, w_loudness=+2.0
SEMANGAT : w_volUp=+2.5, w_loudness=+0.8
FOKUS    : w_listenDur=+3.0, w_skip=-2.0 (skip negatif!)
SENDU    : w_quiet=+2.5, w_listenDur=+1.0
SANTAI   : w_calm=+3.0
```

### Batch Training (PSRAM Board Only)

Pada board dengan PSRAM (ESP32-CAM, dll):

```
┌─────────────────────────────────────────────────────┐
│  Histori Sample (PSRAM, max 700 sample × 40 byte)  │
│                                                      │
│  Setiap sample berisi:                               │
│  - 8 fitur ternormalisasi [0..1]                    │
│  - Label: kategori vibe yang diprediksi saat itu    │
│  - Feedback: wasCorrect (true/false)                 │
│  - Timestamp                                         │
└──────────────────┬──────────────────────────────────┘
                   │ Setiap 5 menit (kalau ada ≥30 sample)
                   ▼
┌─────────────────────────────────────────────────────┐
│  Gradient Descent (5 epoch, LR=0.05)                │
│                                                      │
│  Update bobot kategori yang salah prediksi:          │
│  w ← w - LR × (pred - target) × fitur              │
│                                                      │
│  Clamp: w ∈ [-8, +8], bias ∈ [-6, +3]              │
└──────────────────┬──────────────────────────────────┘
                   │ Setelah training
                   ▼
┌─────────────────────────────────────────────────────┐
│  Simpan ke SD Card: /vibe_w.bin (debounced 15 detik)│
│  Dimuat ulang saat boot berikutnya                  │
└─────────────────────────────────────────────────────┘
```

### Feedback Implisit

RYNE tidak butuh user memberi rating secara manual. Feedback dikumpulkan
secara implisit:

| Event | Feedback |
|-------|---------|
| Skip cepat (<15 detik setelah prediksi vibe) | `wasCorrect = false` |
| Skip normal | `wasCorrect = true` (prediksi sudah expired) |
| Dengerin >25 detik tanpa skip | `wasCorrect = true` (periodik) |

---

## 🖥️ OLED Display

Display OLED 128×64 dibagi menjadi beberapa zona:

```
┌────────────────────────────────┐  ← y=0
│ ▶ BT    PLAY         08/24    │  ← Header (inverted): status BT,
│                               │    play/stop, nomor track
├────────────────────────────────┤  ← y=12
│   Judul Lagu (ticker scroll)  │  ← Nama file tanpa ekstensi
├────────────────────────────────┤  ← y=22
│ PR [████████████░░░░░░░]  67% │  ← Progress bar animasi smooth
├────────────────────────────────┤  ← y=32
│ MP3   RPT:A   SHF:Y   EQ     │  ← Info: format, repeat, shuffle, EQ
├────────────────────────────────┤  ← y=43 (garis pemisah)
│ 😊  lagi fokus nih~       S1E │  ← Area wajah + pesan RYNE
│     ^RYNE 2.3s               │  ← Indikator RYNE atau info track
└────────────────────────────────┘  ← y=63
```

### Animasi Wajah

8 ekspresi wajah bitmap 16×16 pixel dengan animasi:
- **Bobbing**: wajah naik-turun mengikuti gelombang sinus
- **Blinking**: kedip otomatis setiap 2.5–5.5 detik
- **Transisi**: ganti ekspresi saat vibe berubah

| Ekspresi | Kode | Kondisi |
|----------|------|---------|
| Neutral | `FACE_NEUTRAL` | Idle / standby |
| Happy | `FACE_HAPPY` | Semangat / lagu hampir habis |
| Sad | `FACE_SAD` | Buffer kritis / BT putus |
| Shock | `FACE_SHOCK` | Excited / decode stuck |
| Sleepy | `FACE_SLEEPY` | Volume pelan / sendu |
| Smirk | `FACE_SMIRK` | Nostalgik / track ending |
| Frustrated | `FACE_FRUSTRATED` | Bosen / BT tidak stabil |
| Chill | `FACE_CHILL` | Fokus / santai |

### Volume Overlay

Saat volume berubah, area bawah OLED menampilkan:
```
VOL  [████████████░░░░░░░░]  75%
================--------
```
Overlay otomatis hilang setelah 2 detik.

### Auto-Dim

Layar otomatis redup setelah **30 detik** tanpa interaksi tombol.
Tekan tombol apapun untuk menyalakan kembali.

---

## 🔧 Hardware yang Dibutuhkan

### Komponen Wajib

| Komponen | Spesifikasi | Keterangan |
|----------|-------------|------------|
| ESP32 | Any variant dengan BT Classic | ESP32-WROOM-32, ESP32-S3 tidak support |
| SD Card | FAT32, Class 10 | Ukuran bebas (2GB–32GB) |
| SD Card Module | SPI atau MMC | Built-in pada ESP32-CAM |
| OLED Display | SSD1306, 128×64, I2C | Alamat 0x3C |
| Tombol | Push button, 5 buah | Normalnya NO (Normally Open) |
| Kabel | Jumper wire | Secukupnya |

### Komponen Opsional

| Komponen | Fungsi |
|----------|--------|
| Kapasitor 100µF | Stabilisasi daya |
| Resistor pull-up | Backup kalau pakai tombol tanpa pull-up internal |
| Enclosure/Case | Pelindung rangkaian |
| Baterai LiPo + modul charging | Untuk portable |

### Board yang Direkomendasikan

#### ⭐ ESP32-CAM AI Thinker (TERBAIK)
```
✅ Built-in PSRAM 4MB → Ring buffer 2MB
✅ Built-in SD Card reader (MMC interface)
✅ Bluetooth Classic support
✅ Murah & mudah didapat
⚠️  Tidak ada USB-Serial onboard
    (butuh FTDI untuk upload)
⚠️  GPIO terbatas (share dengan kamera)
```

#### ✅ ESP32-WROOM-32 Development Board
```
✅ USB-Serial onboard (mudah upload)
✅ Banyak GPIO tersedia
✅ Bluetooth Classic support
⚠️  Tidak ada PSRAM → ring buffer 64KB
⚠️  SD Card harus pakai modul eksternal
⚠️  Disarankan MP3 bitrate rendah (128kbps)
```

#### ✅ ESP32 DevKit V1
```
✅ Paling umum, dokumentasi banyak
✅ GPIO banyak
⚠️  Sama seperti WROOM-32, tanpa PSRAM
```

> ⚠️ **TIDAK SUPPORT**: ESP32-S2, ESP32-S3, ESP32-C3  
> Karena tidak memiliki **Bluetooth Classic** (hanya BLE).

---

## 🔌 Skema Wiring

### ESP32-CAM AI Thinker

```
ESP32-CAM          Komponen
─────────────────────────────────────────────

[SD Card - Built-in MMC]
GPIO 2  ────────── SD_DATA0 / MISO
GPIO 4  ────────── SD_DATA1
GPIO 12 ────────── SD_DATA2
GPIO 13 ────────── SD_DATA3 / CS
GPIO 14 ────────── SD_CLK
GPIO 15 ────────── SD_CMD / MOSI

[OLED SSD1306 128x64 I2C]
GPIO 21 ────────── SDA
GPIO 22 ────────── SCL
3.3V    ────────── VCC
GND     ────────── GND

[Tombol - Active LOW dengan Pull-up Internal]
GPIO 0  ──[BTN_BOOT]── GND  (tombol bawaan ESP32-CAM)
GPIO 19 ──[BTN_A   ]── GND  (Play/Pause / Next)
GPIO 23 ──[BTN_B   ]── GND  (Previous / Restart)
GPIO 1  ──[BTN_C   ]── GND  ⚠️ TX pin, hati-hati
GPIO 3  ──[BTN_D   ]── GND  ⚠️ RX pin, hati-hati

[Power]
5V      ────────── VIN (dari USB atau baterai)
GND     ────────── GND
```

> ⚠️ **Catatan GPIO 1 & 3**: Pada ESP32-CAM, GPIO 1 (TX) dan GPIO 3 (RX)
> dipakai untuk Serial. Kalau pakai Serial Monitor, ganti BTN_C dan BTN_D
> ke GPIO lain yang tersedia. Atau nonaktifkan Serial setelah boot.

### ESP32 DevKit (dengan modul SD Card SPI)

```
ESP32 DevKit       Komponen
─────────────────────────────────────────────

[SD Card Module SPI]
GPIO 23 ────────── MOSI
GPIO 19 ────────── MISO
GPIO 18 ────────── SCK/CLK
GPIO 5  ────────── CS
3.3V    ────────── VCC  (atau 5V kalau modul support)
GND     ────────── GND

[OLED SSD1306 128x64 I2C]
GPIO 21 ────────── SDA
GPIO 22 ────────── SCL
3.3V    ────────── VCC
GND     ────────── GND

[Tombol]
GPIO 0  ──[BTN_BOOT]── GND
GPIO 19 ──[BTN_A   ]── GND
GPIO 23 ──[BTN_B   ]── GND
GPIO 1  ──[BTN_C   ]── GND
GPIO 3  ──[BTN_D   ]── GND

[Power]
5V/VIN  ────────── 5V
GND     ────────── GND
```

> 💡 **Tips**: Selalu pasang kapasitor 100µF antara 3.3V dan GND dekat
> modul SD Card untuk mencegah glitch saat SD card diakses.

---

## 📚 Instalasi Library

### Melalui Arduino IDE Library Manager

Buka **Sketch → Include Library → Manage Libraries**, lalu cari dan install:

| Library | Author | Versi |
|---------|--------|-------|
| `ESP32-A2DP` | Phil Schatzmann | ≥ 1.8.0 |
| `arduino-audio-tools` | Phil Schatzmann | ≥ 0.9.8 |
| `Adafruit SSD1306` | Adafruit | ≥ 2.5.7 |
| `Adafruit GFX Library` | Adafruit | ≥ 1.11.5 |

### Melalui Library Manager URL (Board Manager)

Pastikan ESP32 board package sudah terinstall:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Versi yang direkomendasikan: **ESP32 Arduino Core 2.0.x**

### Verifikasi Instalasi

Buka **File → Examples → ESP32-A2DP → A2DP-Source** dan pastikan bisa
compile tanpa error. Jika bisa, semua dependency sudah benar.

---

## 🚀 Cara Upload

### Konfigurasi Arduino IDE

```
Board          : "AI Thinker ESP32-CAM"  (untuk ESP32-CAM)
               : "ESP32 Dev Module"      (untuk DevKit)
Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
CPU Frequency  : 240MHz (WiFi/BT)
Flash Frequency: 80MHz
Flash Mode     : QIO
Flash Size     : 4MB (32Mb)
Upload Speed   : 115200
```

> ⚠️ **WAJIB**: Gunakan partition scheme **"Huge APP"** karena sketch ini
> besar (~1.5MB setelah dikompilasi). Partition default tidak akan cukup.

### Untuk ESP32-CAM (Tanpa USB-Serial Onboard)

Butuh **FTDI FT232RL** atau modul USB-to-Serial lainnya:

```
FTDI            ESP32-CAM
───────────────────────────
GND    ──────── GND
5V/VCC ──────── 5V
TX     ──────── GPIO 3 (RX)
RX     ──────── GPIO 1 (TX)

Untuk masuk mode upload:
GPIO 0 ──────── GND  (sambungkan saat upload, lepas setelah)
```

Langkah upload ESP32-CAM:
1. Sambungkan GPIO 0 ke GND
2. Tekan tombol RESET atau sambungkan daya
3. Klik Upload di Arduino IDE
4. Setelah "Connecting..." muncul, lepas GPIO 0 dari GND
5. Tunggu hingga upload selesai
6. Tekan RESET untuk jalankan program

### Untuk ESP32 DevKit (USB Langsung)

Cukup colok USB, pilih port COM yang sesuai, klik Upload.

---

## 💾 Persiapan SD Card

### Format SD Card

```
Format  : FAT32 (BUKAN exFAT, BUKAN NTFS)
Cluster : 32KB (default)
Label   : Bebas (maks 11 karakter)
```

### Struktur File

Letakkan semua file audio langsung di **root** SD Card (tidak dalam folder):

```
SD Card (root /)
├── 01_lagu_satu.mp3
├── 02_lagu_dua.mp3
├── 03_lagu_tiga.wav
├── 04_instrumental.mp3
└── ... (dst)
```

> ⚠️ File dalam subfolder **tidak** akan terdeteksi. Semua harus di root `/`.

### Spesifikasi File Audio

#### MP3
```
Bitrate   : 128kbps – 320kbps (CBR/VBR keduanya support)
Sample rate: 44100 Hz
Channels  : Stereo (Mono juga bisa, akan di-convert)
Tags      : ID3v1/v2 support (tidak ditampilkan, tapi tidak crash)
```

#### WAV
```
Bit depth  : 16-bit
Sample rate: 44100 Hz
Channels   : Stereo
Encoding   : PCM (ADPCM/compressed WAV tidak support)
```

> 💡 **Tips**: File dengan nama alphabetical akan diurutkan secara otomatis.
> Gunakan prefix angka `01_`, `02_` dll untuk kontrol urutan playlist.

### File Konfigurasi (Auto-generated)

File berikut dibuat otomatis oleh sistem di root SD Card:
```
/vibe_w.bin     ← Bobot vibe terlatih (hanya board PSRAM)
```
Jangan hapus file ini jika ingin RYNE mempertahankan hasil learning-nya.

---

## 🎮 Panduan Tombol

### Tombol Tunggal

| Tombol | Tekan Pendek | Tekan Panjang (>0.8 detik) |
|--------|-------------|---------------------------|
| **BOOT** (GPIO 0) | Toggle Shuffle ON/OFF | Ganti mode Repeat (OFF→ALL→ONE) |
| **A** (GPIO 19) | Play / Pause | Next Track |
| **B** (GPIO 23) | Previous Track | Restart Track (dari awal) |
| **C** (GPIO 1) | Volume + (naik 8 step) | Mute / Unmute |
| **D** (GPIO 3) | Volume - (turun 8 step) | Reset Volume ke default (80) |

### Kombinasi Tombol (Tekan Bersamaan)

| Kombinasi | Fungsi |
|-----------|--------|
| **A + B** | Tampilkan info SD Card (total/pakai/sisa) |
| **A + C** | Tampilkan info sistem (RAM, suhu, vibe, histori) |
| **B + C** | Rescan ulang playlist dari SD Card |
| **C + D** | Toggle Sleep Timer 30 menit |
| **A + D** | Buka Experimental Menu |

> 💡 **Tips**: Kombinasi tombol harus ditekan dalam jendela **80ms**
> supaya terdeteksi sebagai combo.

### AVRCP Remote Control (Opsional)

Jika AVRCP diaktifkan di Experimental Menu, speaker/headphone yang mendukung
remote control bisa mengirim perintah:

| Tombol di Speaker | Aksi |
|-------------------|------|
| Play | Play |
| Pause / Stop | Pause |
| Next | Next Track |
| Previous | Previous Track |
| Volume + | Volume naik |
| Volume - | Volume turun |

---

## 🔬 Experimental Menu

Buka dengan **A + D** secara bersamaan. Navigasi: **C** = atas, **D** = bawah,
**A** = pilih, **BOOT** = keluar.

```
┌──────────────────────────────────┐
│      * EXPERIMENTAL MENU        │
├──────────────────────────────────┤
│ > 1. BT Scan        HUOREI-BS10 │
│   2. TX Gain BT       +9dBm     │
│   3. AVRCP Ctrl        [ON]     │
│   4. Equalizer      [ON]Bass    │
│   5. Crossfade         [ON]     │
└──────────────────────────────────┘
  [C/D]:Nav  [A]:Pilih  [BOOT]:Exit
```

### 1. BT Scan — Bluetooth Device Scanner

Scan dan pilih perangkat Bluetooth di sekitar:

```
┌──────────────────────────────────┐
│      BT DEVICE SCANNER       05 │
├──────────────────────────────────┤
│ > HUOREI-BS10           -65     │
│   JBL Flip 5            -72     │
│   Sony WH-1000XM4       -80     │
│   Bose QC35             -88     │
└──────────────────────────────────┘
  [A]Pilih  [B]Scan  [C/D]Nav
```

- Tampilkan nama dan RSSI (kekuatan sinyal) tiap perangkat
- Pilih perangkat → ESP32 otomatis restart koneksi BT
- Nama perangkat tersimpan di flash untuk auto-reconnect berikutnya

### 2. TX Gain BT — Transmit Power

Atur kekuatan sinyal transmit Bluetooth:

| Level | dBm | Jangkauan Estimasi |
|-------|-----|---------------------|
| 0 | -12 dBm | ~3 meter |
| 1 | -9 dBm | ~5 meter |
| 2 | -6 dBm | ~7 meter |
| 3 | -3 dBm | ~10 meter |
| 4 | 0 dBm | ~15 meter |
| 5 | +3 dBm | ~20 meter |
| 6 | +6 dBm | ~25 meter |
| **7** | **+9 dBm** | **~30 meter (default)** |

> ⚠️ TX power lebih tinggi = konsumsi daya lebih besar.

### 3. AVRCP Control

Toggle remote control dari speaker. Berguna untuk speaker dengan tombol
next/prev bawaan.

### 4. Equalizer

Pilih dari 5 preset EQ. Perubahan langsung terasa saat diputar.

### 5. Crossfade

Toggle transisi halus antar lagu. Saat aktif, lagu lama fade out dulu
baru lagu baru fade in.

---

## ⚙️ Konfigurasi Lanjutan

Semua konfigurasi ada di bagian atas sketch (`// KONFIGURASI`):

### Konfigurasi Dasar

```cpp
#define BT_DEFAULT_NAME    "HUOREI-BS10"  // Nama BT device yang dicari
#define OLED_SDA_PIN       21             // Pin SDA OLED
#define OLED_SCL_PIN       22             // Pin SCL OLED
#define OLED_ADDR          0x3C           // Alamat I2C OLED
#define VOL_DEFAULT        80             // Volume default (0-127)
#define VOL_STEP           8              // Langkah perubahan volume
#define SLEEP_TIMER_MIN    30             // Durasi sleep timer (menit)
```

### Konfigurasi Pin Tombol

```cpp
#define BTN_BOOT           0              // GPIO BOOT button
#define BTN_A              19             // GPIO tombol A
#define BTN_B              23             // GPIO tombol B
#define BTN_C              1              // GPIO tombol C
#define BTN_D              3              // GPIO tombol D
#define LONG_PRESS_MS      800            // Durasi tekan panjang (ms)
#define DEBOUNCE_MS        20             // Debounce time (ms)
```

### Konfigurasi Buffer

```cpp
#define RING_BUF_SIZE_PSRAM  (2*1024*1024) // Ring buffer dengan PSRAM (2MB)
#define RING_BUF_FALLBACK    (64*1024)     // Ring buffer tanpa PSRAM (64KB)
#define DECODE_CHUNK         (4*1024)      // Ukuran chunk decode per iterasi
#define BT_PREFILL_BYTES_PSRAM (32*1024)   // Prefill buffer PSRAM (32KB)
#define BT_PREFILL_BYTES_HEAP  (16*1024)   // Prefill buffer Heap (16KB)
```

### Konfigurasi Vibe

```cpp
#define VIBE_ENERGETIC_THRESHOLD  9000.f  // RMS threshold lagu keras
#define VIBE_QUIET_THRESHOLD      1800.f  // RMS threshold lagu pelan
#define VIBE_CALM_THRESHOLD       4000.f  // RMS threshold lagu tenang
#define VIBE_SKIP_BORED_COUNT     3       // Skip >= N = bosen
#define VIBE_SKIP_WINDOW_MS       120000  // Window deteksi skip (2 menit)
#define VIBE_REPEAT_NOSTALGIC_MS  60000   // Repeat > N ms = nostalgia
#define VIBE_LISTEN_ENGAGED_MS    90000   // Dengerin > N ms = fokus
```

### Konfigurasi RYNE

```cpp
#define RYNE_INTERVAL_MS       500        // Interval RYNE task (ms)
#define RYNE_STUCK_MS          3000       // Timeout decode/playback stuck
#define RYNE_SKIP_STORM_COUNT  3          // Skip storm threshold
#define RYNE_SKIP_STORM_MS     15000      // Window skip storm (15 detik)
#define RYNE_MSG_DURATION_MS   3500       // Durasi tampil pesan RYNE
```

### Konfigurasi Learning

```cpp
#define VIBE_HISTORY_CAPACITY   700       // Max sample histori (PSRAM)
#define VIBE_TRAIN_INTERVAL_MS  300000    // Interval batch training (5 menit)
#define VIBE_TRAIN_MIN_SAMPLES  30        // Min sample sebelum training
#define VIBE_TRAIN_EPOCHS       5         // Jumlah epoch per training
#define VIBE_TRAIN_LR           0.05f     // Learning rate gradient descent
#define VIBE_W_SAVE_DEBOUNCE_MS 15000     // Debounce simpan bobot (15 detik)
```

### Konfigurasi Tampilan

```cpp
#define TICKER_INTERVAL_MS     260        // Kecepatan scroll teks (ms)
#define TICKER_MAX_CHARS       21         // Max karakter sebelum scroll
#define OLED_DIM_TIMEOUT_MS    30000      // Timeout dim layar (30 detik)
#define FACE_UPDATE_INTERVAL   400        // Update animasi wajah (ms)
#define VOL_OVERLAY_MS         2000       // Durasi tampil overlay volume
```

---

## 🐛 Troubleshooting

### Audio Berhenti di Tengah Lagu

**Gejala**: Lagu berhenti tiba-tiba, progress bar diam, status PLAY tapi tidak ada suara.

**Penyebab & Solusi**:
```
1. Ring buffer underrun (non-PSRAM board, MP3 bitrate tinggi)
   → Kurangi bitrate MP3 ke 128kbps
   → Atau gunakan board dengan PSRAM

2. BT connection drop
   → Dekatkan ESP32 ke speaker
   → Turunkan TX power (coba +3dBm dulu)

3. Decode task stuck
   → RYNE akan mendeteksi dan tampilkan pesan "decode macet"
   → Tekan next untuk skip ke lagu berikutnya

4. File MP3 corrupt
   → Coba format ulang SD card dan copy ulang file
```

### Tidak Terdeteksi Speaker BT

**Gejala**: Scanning selesai tapi speaker tidak muncul.

**Solusi**:
```
1. Pastikan speaker dalam mode pairing (biasanya lampu blink cepat)
2. Hapus pairing lama di speaker (factory reset)
3. Scan ulang dengan tombol B di menu BT Scanner
4. Naikkan TX power ke +9dBm di Experimental Menu
5. Dekatkan ESP32 ke speaker saat scanning
```

### OLED Tidak Menyala

**Gejala**: Layar gelap total.

**Solusi**:
```
1. Periksa koneksi SDA (GPIO 21) dan SCL (GPIO 22)
2. Periksa tegangan VCC OLED (harus 3.3V)
3. Ganti alamat I2C: coba 0x3D (beberapa modul pakai ini)
   → Di sketch: #define OLED_ADDR 0x3D
4. Jalankan I2C scanner untuk deteksi alamat:
   https://playground.arduino.cc/Main/I2cScanner/
```

### SD Card Tidak Terdeteksi

**Gejala**: Serial menampilkan "SD Card Gagal Mount!" atau OLED error.

**Solusi**:
```
1. Format ulang SD card ke FAT32 (gunakan SDFormatter atau diskpart)
2. Periksa koneksi pin SD card
3. Pastikan SD card ≤ 32GB (beberapa modul tidak support SDXC)
4. Coba SD card lain (beberapa brand tidak compatible)
5. Tambah kapasitor 100µF dekat modul SD card
6. Periksa tegangan: modul SD card butuh 3.3V yang stabil
```

### Suara Terputus-putus / Noise

**Gejala**: Audio stuttering, ada bunyi klik/pop.

**Solusi**:
```
1. Kurangi bitrate MP3 (gunakan 128kbps)
2. Gunakan board dengan PSRAM untuk ring buffer lebih besar
3. Pastikan daya ESP32 stabil (gunakan power supply yang baik)
4. Jauhkan ESP32 dari sumber interferensi WiFi/2.4GHz
5. Kurangi OLED update interval:
   → Ubah OLED_UPDATE_INTERVAL_MS dari 80 ke 150
```

### Upload Gagal (ESP32-CAM)

**Gejala**: "Failed to connect to ESP32" saat upload.

**Solusi**:
```
1. Pastikan GPIO 0 tersambung ke GND saat menekan tombol upload
2. Coba speed lebih rendah: Upload Speed → 115200
3. Periksa koneksi TX/RX (TX FTDI → RX ESP32, RX FTDI → TX ESP32)
4. Gunakan FTDI dengan driver CP2102 atau CH340 yang sudah terinstall
5. Coba tekan RESET ESP32 tepat setelah klik Upload
```

### Volume Tidak Berubah di Speaker

**Gejala**: Ubah volume di ESP32 tapi speaker tidak berubah.

**Penjelasan**:
```
Volume di sini diatur via A2DP protocol ke speaker. Tidak semua
speaker mendukung volume control via A2DP. Jika tidak berubah:
→ Atur volume langsung di speaker
→ Aktifkan AVRCP untuk kontrol volume lebih kompatibel
```

### Serial Monitor Mengganggu Tombol C & D

**Gejala**: Tombol C (GPIO 1/TX) dan D (GPIO 3/RX) tidak responsif
saat Serial Monitor terbuka.

**Solusi**:
```
Opsi 1: Ganti pin tombol ke GPIO lain yang tidak konflik
         #define BTN_C  34   // Input only GPIO
         #define BTN_D  35   // Input only GPIO

Opsi 2: Tambahkan di akhir setup():
         Serial.end();  // Bebaskan GPIO 1 & 3
```

---

## 📊 Performa & Konsumsi Daya

### Konsumsi RAM (ESP32-CAM, PSRAM 4MB)

```
Ring Buffer (PSRAM)     : 2,048 KB
Vibe History (PSRAM)    : ~28 KB (700 × 40 byte)
Track Profiles (PSRAM)  : ~(jumlah lagu × 12 byte)
─────────────────────────────────────────────────
Total PSRAM terpakai    : ~2,076 KB dari 4,096 KB

Heap (RAM internal)     : ~120 KB free saat running
Stack RYNE Task         : 4 KB
Stack Decode Task       : 32 KB
```

### Konsumsi RAM (ESP32 tanpa PSRAM)

```
Ring Buffer (Heap)      : 64 KB
Vibe History            : 0 KB (tidak dialokasikan)
─────────────────────────────────────────────────
Total Heap terpakai     : ~200 KB dari ~320 KB
Heap tersisa (running)  : ~80-100 KB
```

### Konsumsi Daya (Estimasi)

```
ESP32 + BT aktif + SD aktif : ~250-300 mA @ 5V
ESP32 + BT aktif (idle SD)  : ~150-200 mA @ 5V
Dengan baterai LiPo 2000mAh : ~6-8 jam pemutaran
```

### Performa Audio

```
Latency BT A2DP          : ~200-300ms (normal untuk A2DP)
Buffer underrun rate      : <0.1% (PSRAM) / ~1-5% (no PSRAM, 320kbps)
CPU usage decode task     : ~30-40% Core 0
CPU usage RYNE task       : ~5-10% Core 1
CPU usage loop()          : ~10-15% Core 1
```

---

## 📝 Changelog

### v1.2.0 (Current)
```
[FIX CRITICAL] Lagu terhenti sebelum habis - root cause:
  - TRACK_END_SILENCE_FRAMES terlalu kecil (172 → 512)
    ~1 detik → ~5.8 detik, cukup untuk jeda di tengah lagu
  - getSoundData(): av<byteCount + fileReadDone=false
    sebelumnya: output silence total (SALAH)
    sekarang: output partial data + zero-pad (BENAR)
  - trackEndSilenceCount tidak di-reset saat decode berjalan
    sekarang: selalu di-reset saat fileReadDone=false
[FIX] BT_PREFILL_BYTES adaptif: 32KB (PSRAM) / 16KB (Heap)
[FIX] audioDecodeTask: tambah stuck timeout 8 detik
[IMPROVE] Log lebih detail saat trackEnded dan EOF
```

### v1.1.0
```
[FIX] Track berulang sebelum benar-benar habis (race condition)
  - auto-next sekarang selalu lewat requestAutoNext() bukan
    langsung doLoadTrack()
[NEW] Ring buffer naik 768KB → 2MB (board PSRAM)
[NEW] Vibe feature history buffer di PSRAM (700 sample)
[NEW] Per-track profile di PSRAM (playCount, skipCount, dll)
[NEW] Batch gradient descent training tiap 5 menit
[NEW] Bobot vibe persist ke SD card (/vibe_w.bin)
[NEW] 8 kategori vibe dengan weighted sigmoid scoring
[NEW] Feedback implisit: skip cepat = wasCorrect=false
```

### v1.0.0
```
[INITIAL RELEASE]
[NEW] Bluetooth A2DP Source audio streaming
[NEW] MP3 (Helix) dan WAV decoder
[NEW] OLED 128×64 display dengan animated face
[NEW] RYNE Engine: multi-signal real-time monitoring
[NEW] 5 preset Equalizer (biquad filter 3 band)
[NEW] Crossfade 64-step antar lagu
[NEW] BT Scanner dengan RSSI display
[NEW] AVRCP remote control support
[NEW] TX Power adjustment 8 level
[NEW] Sleep timer 30 menit
[NEW] Shuffle + 3 mode Repeat
[NEW] Auto-reconnect BT
[NEW] OLED auto-dim
[NEW] Combo button support
[NEW] NVS preferences persistence
```

---

## 🏗️ Arsitektur Sistem

```
┌─────────────────────────────────────────────────────────┐
│                      ESP32 (Dual Core)                  │
│                                                          │
│  Core 0 (Protokol)          Core 1 (Aplikasi)           │
│  ┌─────────────────┐        ┌─────────────────────────┐ │
│  │ BT Stack        │        │ loop() - Main Logic     │ │
│  │ A2DP Protocol   │        │ ├─ handleButtons()       │ │
│  │ getSoundData()  │◄───────┤ ├─ processStopAndLoad() │ │
│  │  (BT Callback)  │        │ ├─ processAutoNext()    │ │
│  └────────┬────────┘        │ └─ updateOLED()         │ │
│           │                 │                           │ │
│           │ read            │ audioDecodeTask()         │ │
│           ▼                 │ (Priority 3, Core 0)      │ │
│  ┌─────────────────┐        │ ├─ baca file SD card     │ │
│  │  Ring Buffer    │◄───────┤ ├─ decode MP3/WAV        │ │
│  │  (2MB PSRAM /   │        │ └─ tulis ke ring buffer  │ │
│  │   64KB Heap)    │        │                           │ │
│  └─────────────────┘        │ ryneTask()               │ │
│                              │ (Priority 1, Core 1)     │ │
│  ┌─────────────────┐        │ ├─ analisis sinyal        │ │
│  │  SD Card        │        │ ├─ update vibe            │ │
│  │  (MP3/WAV files)│        │ ├─ batch training         │ │
│  │  (/vibe_w.bin)  │        │ └─ kirim ke OLED face    │ │
│  └─────────────────┘        └─────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

---

## 📜 Lisensi

```
MIT License

Copyright (c) 2024 ESP32 BT Music Player Project

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 🙏 Credits & Acknowledgments

| Kontribusi | Pihak |
|-----------|-------|
| ESP32-A2DP Library | [Phil Schatzmann](https://github.com/pschatzmann) |
| arduino-audio-tools | [Phil Schatzmann](https://github.com/pschatzmann) |
| Adafruit SSD1306 | [Adafruit Industries](https://github.com/adafruit) |
| Helix MP3 Decoder | RealNetworks (open source) |
| ESP32 Arduino Core | [Espressif Systems](https://github.com/espressif) |
| RYNE Engine & Vibe Learning | Proyek ini |

---

## 📞 Kontak & Kontribusi

Pull request, issue, dan saran sangat disambut!

Jika proyek ini bermanfaat, silakan ⭐ beri star di repository ini.

---

*Dibuat dengan ❤️ untuk komunitas ESP32 Indonesia*
```
