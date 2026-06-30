# ESP3- Bluetooth Music Player — RYNE Edition

Firmware Bluetooth A2DP music player berbasis **ESP32-CAM (AI Thinker)** dengan layar OLED interaktif yang punya "wajah" emosional. Mainin file MP3/WAV dari SD card langsung ke speaker/headset Bluetooth, lengkap dengan equalizer, crossfade, kontrol AVRCP, dan sistem monitoring pintar bernama **RYNE** yang nebak vibe/mood lagu yang lagi diputar secara real-time.

![status](https://img.shields.io/badge/status-active-success)
![platform](https://img.shields.io/badge/platform-ESP32--CAM-blue)
![license](https://img.shields.io/badge/license-MIT-lightgrey)

---

## ✨ Fitur Utama

- **Bluetooth A2DP Source** — streaming audio ke speaker/headset BT manapun, plus dukungan **AVRCP** (play/pause/next/prev/volume lewat remote BT).
- **Player MP3 & WAV** dari SD card (SD_MMC), dengan ring buffer besar (PSRAM-aware) buat playback yang stabil tanpa putus-putus.
- **5-band-ish Equalizer** (low shelf, peaking, high shelf) dengan preset Flat / Bass / Pop / Rock / Jazz.
- **Crossfade** antar lagu, opsional.
- **Shuffle & Repeat** (off / all / one).
- **Sleep timer** otomatis fade-out.
- **BT Device Scanner** bawaan — scan & connect ke speaker/headset langsung dari menu, tanpa perlu hardcode nama device.
- **TX Power Gain control** — atur kekuatan sinyal BT (-12dBm s/d +9dBm).
- **OLED 128x64 dengan wajah emosional** yang hidup: kedip mata otomatis, breathing animation, dan auto-scroll ticker buat pesan panjang.
- **RYNE Engine** — sistem monitoring background yang:
  - Mendeteksi masalah teknis (buffer kritis, decode macet, BT putus-nyambung) dan nampilinnya sebagai ekspresi wajah + pesan.
  - **Multi-signal vibe detection**: nebak "mood" pengguna dari kombinasi loudness audio real-time, pola skip lagu, perubahan volume, dan penggunaan repeat — lalu nampilin kategori vibe (semangat, sendu, bosen, nostalgik, fokus, gelisah, santai, excited) lewat wajah & pesan yang variatif (bukan satu pesan monoton).
- **Kontrol tombol fisik** (5 tombol + kombinasi) — play/pause, next/prev, volume, shuffle, repeat, sampai shortcut info sistem & SD card.
- **Preferences tersimpan** ke flash (volume, track terakhir, repeat/shuffle, nama BT, EQ, dst) — lanjut dari kondisi terakhir saat reboot.

---

## 🛠️ Hardware

| Komponen | Keterangan |
|---|---|
| **MCU** | ESP32-CAM (AI Thinker) |
| **Display** | OLED SSD1306 128x64, I2C |
| **Storage** | MicroSD card (lewat slot bawaan ESP32-CAM, mode SD_MMC) |
| **Audio out** | Bluetooth A2DP (ke speaker/headset eksternal) |
| **Input** | 5 tombol fisik (Boot + 4 tombol custom) |

### Pin Mapping

| Fungsi | Pin |
|---|---|
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| Tombol BOOT | GPIO 0 |
| Tombol A | GPIO 19 |
| Tombol B | GPIO 23 |
| Tombol C | GPIO 1 |
| Tombol D | GPIO 3 |

> ⚠️ GPIO 1 & 3 itu pin UART (TX0/RX0). Pastikan dilepas/jangan ganggu seperti iseng nekan tombol  pas upload firmware lewat serial.

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
| **A + B** | Tampilkan info SD card (total/pakai/sisa) |
| **A + C** | Tampilkan info sistem (heap, suhu, uptime, vibe saat ini) |
| **B + C** | Rescan playlist dari SD card |
| **C + D** | Toggle sleep timer (30 menit) |
| **A + D** | Buka menu eksperimental |

### Menu Eksperimental
Diakses lewat kombo **A+D**, isinya:
1. **BT Scan** — cari & connect ke device Bluetooth baru
2. **TX Gain BT** — atur kekuatan sinyal Bluetooth
3. **AVRCP Ctrl** — toggle kontrol remote dari speaker/headset
4. **Equalizer** — pilih preset EQ
5. **Crossfade** — toggle crossfade antar lagu

---

## 🧠 RYNE Engine

**RYNE** — singkatan dari **R**eactive sYstem for **N**otification & **E**ngagement — adalah task FreeRTOS terpisah (Core 1) yang jalan tiap 500ms, memantau kondisi sistem dan "menerjemahkannya" jadi ekspresi wajah + pesan di OLED. Dua lapisan kerjanya:

**1. Monitoring teknis** — buffer sehat/kritis, decode macet, playback stuck, BT putus-nyambung. Pesan-pesan ini prioritas tertinggi karena menandakan masalah nyata yang perlu perhatian user.

**2. Multi-signal vibe detection** — RYNE menganalisis beberapa sinyal sekaligus buat nebak mood:
- **Loudness audio real-time** (dihitung langsung dari PCM yang lagi diputar, di-smooth biar nggak gampang lompat-lompat)
- **Pola skip** dalam 2 menit terakhir
- **Perubahan volume** (naik/turun) dalam 30 detik terakhir
- **Penggunaan repeat one** dalam durasi lama
- **Durasi mendengarkan** tanpa skip

Dari kombinasi sinyal itu, RYNE mengklasifikasikan ke salah satu dari 8 kategori vibe (`UVIBE_SEMANGAT`, `UVIBE_SENDU`, `UVIBE_BOSEN`, `UVIBE_NOSTALGIK`, `UVIBE_FOKUS`, `UVIBE_GELISAH`, `UVIBE_SANTAI`, `UVIBE_EXCITED`), masing-masing dengan beberapa variasi pesan biar nggak monoton.

---

## 📦 Dependencies (Arduino Library)

Install lewat Arduino Library Manager atau PlatformIO:

- [`AudioTools`](https://github.com/pschatzmann/arduino-audio-tools)
- [`ESP32-A2DP`](https://github.com/pschatzmann/ESP32-A2DP) (untuk `BluetoothA2DPSource`)
- `Adafruit GFX Library`
- `Adafruit SSD1306`
- ESP32 board core (esp32 by Espressif Systems) — pastikan support `esp_avrc_api.h`, `esp_bt.h`, dkk.
intsall library nya lalu add ke arduino ide secara manual ya ,karena library esp32-a2dp tidak tersedia di library manager arduino ide

---

## ⚙️ Build & Upload

1. Install **Arduino IDE** + **ESP32 board package**.
2. Install semua library di atas.
3. Pilih board: **AI Thinker ESP32-CAM**.
4. Partition Scheme: pilih yang punya cukup ruang untuk Bluetooth + OTA (misal `Huge APP (3MB No OTA/1MB SPIFFS)` kalau nggak butuh OTA).
5. **PSRAM**: aktifkan kalau board kamu support — ring buffer otomatis pakai 768KB di PSRAM, fallback ke 64KB heap kalau nggak ada.
6. Siapkan microSD card berisi file `.mp3` / `.wav` di root folder.
7. Flash via USB-TTL programmer (ESP32-CAM nggak punya USB onboard), pastikan GPIO0 di-ground saat masuk mode flashing.
8. Setelah upload, lepas jumper GPIO0, reset board.

---

## 📝 Catatan Teknis

- **Anti early track-end**: ada silence guard (`TRACK_END_SILENCE_FRAMES`) supaya lagu nggak dianggap "selesai" cuma karena ring buffer sempat kosong sesaat (mencegah lompat track prematur).
- **Decode-stuck detection** cuma aktif kalau sedang *playing* dan ring buffer punya ruang kosong — jadi nggak salah deteksi "macet" pas lagu lagi di-pause.
- **Tombol fisik pakai debounce non-blocking** (timestamp-based, bukan `delay()`) supaya loop utama & OLED tetap responsif walau user spam tombol.
- Volume bar & progress bar di OLED pakai **easing/interpolasi** supaya animasinya meluncur halus, bukan lompat instan.
- I2C OLED jalan di 800kHz untuk transfer frame yang lebih cepat (bisa dicoba naikin ke 1MHz kalau wiring pendek & rapi).

---

## 📋 Lisensi

GPLv3 — bebas dipakai, dimodifikasi, dan disebarluaskan. Atribusi wajib, kode sumber harus disertakan, dan karya turunan harus pakai lisensi yang sama

---

## 🤝 Kontribusi

Issue dan pull request terbuka. Kalau nemu bug atau ada ide fitur baru (terutama soal RYNE / vibe detection), silakan dibuka di tab Issues.
