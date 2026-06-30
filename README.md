# ESP32 Bluetooth Music Player — RYNE Edition

Firmware Bluetooth A2DP music player berbasis **ESP32** dengan layar OLED interaktif yang punya "wajah" emosional. Mainin file MP3/WAV dari SD card langsung ke speaker/headset Bluetooth, lengkap dengan equalizer, crossfade, kontrol AVRCP, dan sistem monitoring pintar bernama **RYNE** yang nebak vibe/mood pengguna secara real-time dari kombinasi sinyal audio + perilaku.

![status](https://img.shields.io/badge/status-active-success)
![version](https://img.shields.io/badge/version-v1.0.0-brightgreen)
![platform](https://img.shields.io/badge/platform-ESP32-blue)
![license](https://img.shields.io/badge/license-GPLv3-lightgrey)

---

## ✨ Fitur Utama

- **Bluetooth A2DP Source** — streaming audio ke speaker/headset BT manapun, plus dukungan **AVRCP** (play/pause/next/prev/volume lewat remote BT).
- **Player MP3 & WAV** dari SD card (SD_MMC), dengan ring buffer besar (PSRAM-aware) buat playback yang stabil tanpa putus-putus.
- **Equalizer** (low shelf + peaking + high shelf) dengan preset Flat / Bass / Pop / Rock / Jazz.
- **Crossfade** antar lagu, opsional.
- **Shuffle & Repeat** (off / all / one).
- **Sleep timer** otomatis fade-out.
- **BT Device Scanner** bawaan — scan & connect ke speaker/headset langsung dari menu, tanpa perlu hardcode nama device.
- **TX Power Gain control** — atur kekuatan sinyal BT (-12dBm s/d +9dBm).
- **OLED 128x64 dengan wajah emosional** yang hidup: kedip mata otomatis, breathing animation, dan auto-scroll ticker buat pesan panjang.
- **RYNE Engine** — sistem monitoring background yang:
  - Mendeteksi masalah teknis (buffer kritis, decode macet, BT putus-nyambung) dan nampilinnya sebagai ekspresi wajah + pesan.
  - **Multi-signal vibe detection**: nebak "mood" pengguna dari kombinasi loudness audio real-time, pola skip lagu, perubahan volume, dan penggunaan repeat — lalu nampilin kategori vibe lewat wajah & pesan yang variatif.
- **Kontrol tombol fisik** (5 tombol + kombinasi) — play/pause, next/prev, volume, shuffle, repeat, sampai shortcut info sistem & SD card.
- **Preferences tersimpan** ke flash (volume, track terakhir, repeat/shuffle, nama BT, EQ, dst) — lanjut dari kondisi terakhir saat reboot.

---

## 🛠️ Hardware yang Didukung

### Board Utama yang Direkomendasikan

| Board | PSRAM | Ring Buffer | Keterangan |
|---|---|---|---|
| **ESP32-CAM (AI Thinker)** | 1MB (PSRAM) | 768 KB | Target utama pengembangan, slot SD onboard |
| **ESP32-WROVER / WROVER-B** | 4MB (PSRAM) | 768 KB | Direkomendasikan, PSRAM lebih besar |
| **ESP32-WROVER-E / IE** | 8MB (PSRAM) | 768 KB | Terbaik, headroom memori paling lega |
| **ESP32-DevKitC** (tanpa PSRAM) | ❌ | 64 KB (heap) | Bisa jalan, tapi terbatas — lihat catatan |
| **ESP32 biasa lainnya** | tergantung modul | 64 KB / 768 KB | Auto-detect via `psramFound()` |

> ✅ **Semua board ESP32 dengan Bluetooth Classic** pada dasarnya kompatibel.
> PSRAM dideteksi otomatis — board dengan PSRAM dapat ring buffer 768KB,
> tanpa PSRAM fallback ke 64KB heap.

> ⚠️ **ESP32-S2 & ESP32-S3 tidak didukung** — tidak punya Bluetooth Classic
> yang dibutuhkan untuk A2DP Source.

### Komponen Tambahan

| Komponen | Keterangan |
|---|---|
| **Display** | OLED SSD1306 128x64, I2C |
| **Storage** | MicroSD card (FAT32, file MP3/WAV di root) |
| **Audio out** | Bluetooth A2DP ke speaker/headset eksternal |
| **Input** | 5 tombol fisik (Boot + 4 tombol custom) |

### Pin Mapping (Default — ESP32-CAM AI Thinker)

| Fungsi | Pin |
|---|---|
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| Tombol BOOT | GPIO 0 |
| Tombol A | GPIO 19 |
| Tombol B | GPIO 23 |
| Tombol C | GPIO 1 |
| Tombol D | GPIO 3 |

> ⚠️ GPIO 1 & 3 adalah pin UART (TX0/RX0). Lepas koneksi tombol C & D saat upload firmware lewat serial, karena bisa konflik.

> 💡 Untuk board lain (WROVER, DevKit, dll), pin bisa disesuaikan dengan mengubah konstanta `#define` di bagian atas kode.

---

## 📊 Perbandingan Performa per Board

| Kondisi | ESP32 + PSRAM | ESP32 tanpa PSRAM |
|---|---|---|
| Ring buffer | 768 KB | 64 KB |
| MP3 128kbps | ✅ Sangat stabil | ✅ Stabil |
| MP3 320kbps | ✅ Stabil | ⚠️ Mungkin underrun sesekali |
| WAV 44.1kHz stereo | ✅ Stabil | ⚠️ Kurang disarankan |
| Crossfade | ✅ Mulus | ⚠️ Tergantung kondisi heap |
| RYNE Engine | ✅ Full | ✅ Full (logic sama) |

---

## 🎮 Kontrol Tombol

| Tombol | Tekan Pendek | Tekan Lama |
|---|---|---|
| **BOOT** | Toggle Shuffle | Toggle Repeat (Off → All → One) |
| **A** | Play / Pause | Next track |
| **B** | Previous track | Restart track dari awal |
| **C** | Volume Up | Mute toggle |
| **D** | Volume Down | Reset volume ke default |

### Kombinasi Tombol

| Kombinasi | Aksi |
|---|---|
| **A + B** | Tampilkan info SD card (total / pakai / sisa) |
| **A + C** | Tampilkan info sistem (heap, suhu, uptime, vibe saat ini) |
| **B + C** | Rescan playlist dari SD card |
| **C + D** | Toggle sleep timer (30 menit) |
| **A + D** | Buka menu eksperimental |

### Menu Eksperimental
Diakses lewat kombo **A + D**, isinya:
1. **BT Scan** — cari & connect ke device Bluetooth baru
2. **TX Gain BT** — atur kekuatan sinyal Bluetooth
3. **AVRCP Ctrl** — toggle kontrol remote dari speaker/headset
4. **Equalizer** — pilih preset EQ
5. **Crossfade** — toggle crossfade antar lagu

---

## 🧠 RYNE Engine

**RYNE** *(Reactive sYstem for Notification & Engagement)* adalah FreeRTOS task terpisah yang jalan di Core 1, polling tiap 500ms, memantau kondisi sistem dan menerjemahkannya jadi ekspresi wajah + pesan di OLED.

### Layer 1 — Monitoring Teknis (Prioritas Tinggi)

| Kondisi | Ekspresi | Contoh Pesan |
|---|---|---|
| Decode macet | 😲 SHOCK | "decode macet, coba skip!" |
| Playback stuck | 😤 FRUSTRATED | "audio stuck, buffer penuh?" |
| Buffer kritis | 😢 SAD | "buffer hampir habis tipis!" |
| BT tidak stabil | 😤 FRUSTRATED | "koneksi BT tidak stabil nih" |
| BT reconnecting | 😢 SAD | "lagi nyambungin BT sabar ya" |
| Skip storm | 😏 SMIRK | "skip mulu, ga ada yg cocok?" |

### Layer 2 — Multi-Signal Vibe Detection

RYNE menganalisis 5 sinyal sekaligus:

| Sinyal | Data yang Dibaca |
|---|---|
| **Loudness audio** | Amplitudo PCM real-time (diambil langsung dari `getSoundData`) |
| **Pola skip** | Frekuensi skip dalam 2 menit terakhir |
| **Volume behavior** | Berapa kali naik/turun dalam 30 detik |
| **Repeat pattern** | Repeat ONE aktif > 60 detik = nostalgia |
| **Listen duration** | Berapa lama dengerin tanpa skip sama sekali |

Dari kombinasi sinyal tersebut, RYNE mengklasifikasikan ke 8 kategori vibe, masing-masing dengan 4 variasi pesan berbeda (total 32 pesan unik — tidak ada lagi pesan generik monoton):

| Kategori | Kondisi | Contoh Pesan |
|---|---|---|
| `SEMANGAT` | Vol naik, audio energik | "gas terus bestie!", "lagi on fire nih~" |
| `SENDU` | Audio pelan, dengerin lama | "lagi mellow nih...", "dalem banget rasanya" |
| `BOSEN` | Sering skip | "ga ada yang cocok?", "coba shuffle deh~" |
| `NOSTALGIK` | Repeat one > 60 detik | "lagu favorit nih~", "nostalgia ya?" |
| `FOKUS` | Dengerin > 90 detik tanpa skip | "flow state detected~", "masuk banget lagunya" |
| `GELISAH` | Skip + vol naik turun | "ga bisa diem nih...", "mau gue tenangin?" |
| `SANTAI` | Audio calm, sedikit interaksi | "chill mode on", "slow living vibes" |
| `EXCITED` | Vol sering naik + audio keras | "EXCITED banget!!!", "ini bukan latihan!" |

---

## 📦 Dependencies

### Library yang Diinstall Manual

Library berikut **tidak tersedia di Arduino Library Manager** dan harus diinstall secara manual:

**1. ESP32-A2DP** (oleh pschatzmann)
```
https://github.com/pschatzmann/ESP32-A2DP
```
Download ZIP → Arduino IDE → Sketch → Include Library → Add .ZIP Library

**2. arduino-audio-tools** (oleh pschatzmann)
```
https://github.com/pschatzmann/arduino-audio-tools
```
Download ZIP → install dengan cara sama

### Library yang Tersedia di Library Manager Arduino IDE

Cari dan install langsung dari **Tools → Manage Libraries**:

- `Adafruit GFX Library`
- `Adafruit SSD1306`

### Board Package

- **ESP32 by Espressif Systems** — install lewat **Boards Manager**
  (`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`)

---

## ⚙️ Build & Upload

### ESP32-CAM (AI Thinker)

1. Install Arduino IDE + ESP32 board package
2. Install semua library di atas
3. Pilih board: **AI Thinker ESP32-CAM**
4. Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
5. Enable PSRAM: **Enabled**
6. Siapkan microSD (FAT32) dengan file `.mp3` / `.wav` di root folder
7. Hubungkan USB-TTL programmer, jumper GPIO0 ke GND untuk masuk mode flash
8. Upload, lepas jumper GPIO0, tekan reset

### ESP32-WROVER / WROVER-E

1. Pilih board: **ESP32 Wrover Module**
2. Partition Scheme: **Huge APP** atau **No OTA (2MB APP/2MB SPIFFS)**
3. Enable PSRAM: **Enabled**
4. Sesuaikan pin SD card dan tombol sesuai wiring kamu (ubah `#define` di bagian atas kode)
5. Upload via USB atau programmer

### ESP32 Biasa (tanpa PSRAM)

1. Pilih board sesuai modul kamu (ESP32 Dev Module, dll)
2. PSRAM: **Disabled** (atau biarkan — `psramFound()` akan return false otomatis)
3. Gunakan file MP3 **bitrate rendah (≤ 128kbps)** untuk stabilitas terbaik
4. Upload seperti biasa

---

## 📝 Catatan Teknis

- **Anti early track-end** — silence guard (`TRACK_END_SILENCE_FRAMES = 172`) mencegah lagu dianggap selesai hanya karena ring buffer sempat kosong sesaat sebelum audio benar-benar habis.
- **Decode-stuck detection** hanya aktif saat sedang *playing* dan ring buffer punya ruang kosong — tidak salah deteksi "macet" saat lagu di-pause atau buffer sedang penuh.
- **Tombol debounce non-blocking** — timestamp-based, bukan `delay()`, supaya loop utama & OLED tetap responsif.
- **Volume bar & progress bar** pakai easing/interpolasi — animasi meluncur halus, tidak lompat instan.
- **I2C OLED** berjalan di 800kHz untuk transfer frame lebih cepat. Bisa dinaikkan ke 1MHz jika wiring pendek dan rapi.
- **Vibe smoothing** — loudness audio dirata-rata dengan exponential smoothing (α=0.3) supaya klasifikasi vibe tidak lompat-lompat tiap detik.
- **RYNE cooldown** — pesan vibe hanya dikirim ulang jika kategori berubah, atau setiap 20 detik, supaya tidak spam OLED.

---

## 📋 Lisensi

**GPLv3** — bebas dipakai, dimodifikasi, dan disebarluaskan. Atribusi wajib, kode sumber harus disertakan, dan karya turunan harus memakai lisensi yang sama.

---

## 🤝 Kontribusi

Issue dan pull request terbuka. Kalau nemu bug atau ada ide fitur baru — terutama soal RYNE & vibe detection — silakan buka di tab **Issues**.
