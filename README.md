# 🎵 ESP32 Bluetooth Music Player dengan RYNE Engine

**Versi:** v1.2.1
**Platform:** ESP32 (ESP32-CAM / ESP32 dev board dengan PSRAM opsional)
**Fungsi Utama:** Bluetooth A2DP Audio Source (MP3/WAV Player) dari SD Card, dengan wajah ekspresif di OLED yang "memahami" mood pendengar.

---

## 📖 Daftar Isi

- [Tentang Proyek](#-tentang-proyek)
- [✨ Fitur Utama](#-fitur-utama)
- [🤖 RYNE Engine — Apakah Ini AI?](#-ryne-engine--apakah-ini-ai)
- [🔧 Kebutuhan Hardware](#-kebutuhan-hardware)
- [📌 Pinout](#-pinout)
- [🕹️ Kontrol Tombol](#️-kontrol-tombol)
- [📂 Struktur Menu](#-struktur-menu)
- [🏗️ Arsitektur Sistem](#️-arsitektur-sistem)
- [⚙️ Instalasi & Setup](#️-instalasi--setup)
- [💾 Penyimpanan (Preferences)](#-penyimpanan-preferences)
- [🐛 Changelog / Riwayat Perbaikan](#-changelog--riwayat-perbaikan)
- [⚠️ Catatan & Batasan](#️-catatan--batasan)

---

## 📌 Tentang Proyek

Proyek ini mengubah ESP32 menjadi **pemutar musik Bluetooth (A2DP Source)** yang membaca file MP3/WAV dari SD Card lalu mengirimkannya ke speaker/headset Bluetooth. Perangkat dilengkapi layar OLED yang menampilkan wajah animasi (mirip "companion" digital) yang bereaksi terhadap kondisi sistem dan kebiasaan mendengarkan pengguna melalui sistem bernama **RYNE**.

Selain sebagai pemutar musik biasa, proyek ini punya:
- Equalizer 3-band (bass/mid/treble) dengan beberapa preset
- Crossfade antar lagu
- Kontrol via tombol fisik & remote Bluetooth (AVRCP)
- Sistem deteksi "vibe"/mood pengguna berbasis pembelajaran mesin ringan (lihat bagian RYNE)
- Auto-reconnect Bluetooth & BT device scanner bawaan
- Sleep timer, shuffle, repeat, volume dengan overlay UI

---

## ✨ Fitur Utama

| Fitur | Deskripsi |
|---|---|
| **Bluetooth A2DP Source** | Mengirim audio ke speaker/headset BT (bukan menerima) |
| **Player MP3 & WAV** | Decode menggunakan `AudioTools` (Helix MP3 decoder & WAV decoder) |
| **Ring Buffer Anti-Putus** | Buffer 2MB (PSRAM) / 64KB (fallback) dengan decode task terpisah di Core 0 |
| **OLED Companion Face** | 8 ekspresi wajah animasi (kedip, bergoyang) yang mencerminkan status sistem/mood |
| **Equalizer** | 5 preset: Flat, Bass, Pop, Rock, Jazz (biquad filter 3-band) |
| **Crossfade** | Transisi halus antar lagu (opsional) |
| **AVRCP Remote Control** | Play/Pause/Next/Prev/Volume dari perangkat headset BT |
| **BT Device Scanner** | Scan & pilih perangkat BT langsung dari menu OLED |
| **TX Power Gain Adjuster** | 8 level daya pancar Bluetooth (-12dBm s/d +9dBm) |
| **Shuffle & Repeat** | Mode Off/All/One dengan indikator visual |
| **Sleep Timer** | Auto-stop pemutaran setelah 30 menit |
| **RYNE Engine** | Sistem monitoring real-time + AI vibe learning (dijelaskan di bawah) |
| **Auto-Reconnect BT** | Reconnect otomatis saat koneksi terputus |
| **Volume Overlay** | Indikator volume visual saat vol +/- ditekan |

---

## 🤖 RYNE Engine — Apakah Ini AI?

**Ya — RYNE mengandung komponen AI (Machine Learning) yang sebenarnya**, bukan hanya rule-based statis. Berikut penjelasan lengkapnya agar tidak membingungkan:

RYNE terdiri dari **dua lapisan** yang bekerja sama:

### 1️⃣ Lapisan Monitoring (Rule-Based, bukan AI)

Bagian ini murni logika kondisional (if-else) yang memantau kesehatan sistem secara real-time setiap 500ms (`ryneTask`, jalan di Core 1):

- **Buffer health** — apakah ring buffer sehat, kritis, atau underrun
- **Decode stuck detection** — mendeteksi decoder macet (>3 detik tidak progres)
- **Playback stuck detection** — mendeteksi audio callback macet
- **Skip storm detection** — mendeteksi user skip lagu berkali-kali dalam waktu singkat
- **BT instability detection** — mendeteksi koneksi Bluetooth sering putus-nyambung
- **Track ending detection** — mendeteksi lagu hampir habis (>90% progress)

Bagian ini **hanya logika kondisi**, tidak belajar apa pun. Fungsinya menampilkan pesan/wajah yang sesuai (misal "buffer hampir habis!", "koneksi BT tidak stabil").

### 2️⃣ Lapisan Vibe Learning (Ini yang disebut AI) 🧠

Ini adalah **model machine learning ringan** (mirip *logistic regression* / *one-vs-rest binary classifier* per kategori mood), yang **belajar dari kebiasaan mendengarkan pengguna** dan beradaptasi dari waktu ke waktu. Berikut cara kerjanya:

#### a. Ekstraksi Fitur (Feature Engineering)
Sistem mengumpulkan 8 fitur numerik dari perilaku pengguna secara real-time:

| Fitur | Sumber Data |
|---|---|
| `skipRate` | Frekuensi user melewati/skip lagu dalam window waktu tertentu |
| `volUp` / `volDown` | Frekuensi user menaikkan/menurunkan volume |
| `loudness` | Rata-rata amplitudo audio yang sedang diputar (energi suara) |
| `listenDurNorm` | Berapa lama user bertahan mendengarkan lagu saat ini |
| `repeatOneFlag` | Apakah user mengaktifkan Repeat One dalam waktu lama |
| `quietFlag` / `calmFlag` | Apakah audio sedang pelan/tenang |

#### b. Model Prediksi (Scoring Function)
Untuk setiap kategori vibe (8 kategori: **Semangat, Sendu, Bosen, Nostalgik, Fokus, Gelisah, Santai, Excited**), sistem menghitung skor menggunakan fungsi **sigmoid** atas kombinasi linear fitur dan bobot (weight) — persis seperti neuron tunggal pada logistic regression:

```
score = sigmoid( w1·skipRate + w2·volUp + w3·volDown + w4·loudness
                + w5·listenDur + w6·repeatOne + w7·quiet + w8·calm + bias )
```

Kategori dengan skor tertinggi (di atas threshold) dipilih sebagai **vibe pengguna saat ini**, lalu ditampilkan sebagai ekspresi wajah + pesan yang sesuai di OLED (contoh: mendeteksi "Bosen" → wajah frustrasi + pesan *"ga ada yang cocok?"*).

#### c. Proses Belajar (Online Learning via Gradient Descent)
Ini bagian yang membuatnya benar-benar AI, bukan sekadar aturan tetap:

1. Setiap kali sistem membuat prediksi vibe, sampel fitur + hasil (`wasCorrect`) disimpan ke **buffer riwayat di PSRAM** (kapasitas 700 sampel — `vibeHistory`).
2. Setiap **5 menit** (`VIBE_TRAIN_INTERVAL_MS`), sistem menjalankan **batch training** menggunakan **gradient descent** (5 epoch, learning rate 0.05) untuk menyesuaikan bobot (`vw[]`) setiap kategori berdasarkan data yang terkumpul.
3. Bobot yang sudah dilatih **disimpan ke SD Card** (`/vibe_w.bin`) sehingga model **tetap "ingat" preferensi pengguna** meskipun perangkat di-restart.
4. Bobot awal (`initVibeWeightsDefault()`) diberi nilai heuristik masuk akal sebagai titik awal (cold start), lalu terus disesuaikan oleh proses belajar di atas.

> ⚠️ **Catatan jujur soal skala AI ini**: Ini bukan deep learning / neural network besar — melainkan model linear sederhana (mirip 8 buah logistic regression classifier berjalan paralel) yang dioptimalkan agar ringan berjalan di MCU (ESP32) tanpa akselerator AI khusus. Namun secara definisi teknis, ini **tetap AI/ML** karena memenuhi elemen inti: representasi fitur, fungsi parametrik dengan bobot yang dapat dilatih, fungsi loss implisit (`error = prediksi - label`), dan proses optimasi iteratif (gradient descent) yang memperbarui parameter dari data pengalaman nyata pengguna — bukan aturan yang di-hardcode manusia.

#### d. Kondisi Tanpa PSRAM
Jika modul tidak memiliki PSRAM, buffer riwayat (`vibeHistory`) tidak dialokasikan dan sistem otomatis **fallback ke mode heuristik statis** (bobot default tanpa pembelajaran) — fitur tetap berjalan namun tanpa kemampuan adaptif.

---

## 🔧 Kebutuhan Hardware

| Komponen | Spesifikasi |
|---|---|
| **MCU** | ESP32 (disarankan varian dengan PSRAM 4MB, mis. ESP32-WROVER / ESP32-CAM) |
| **Layar** | OLED SSD1306 128x64, I2C (alamat `0x3C`) |
| **Penyimpanan** | MicroSD Card (via slot SD_MMC) berisi file `.mp3` / `.wav` |
| **Tombol** | 5 buah push button (BOOT + 4 tombol A/B/C/D) |
| **Output Audio** | Perangkat Bluetooth A2DP Sink (speaker/headset BT) |

---

## 📌 Pinout

| Fungsi | Pin |
|---|---|
| OLED SDA | GPIO 21 |
| OLED SCL | GPIO 22 |
| Tombol BOOT | GPIO 0 |
| Tombol A | GPIO 19 |
| Tombol B | GPIO 23 |
| Tombol C | GPIO 1 |
| Tombol D | GPIO 3 |
| SD Card | via `SD_MMC` (`/sdcard`, mode 1-bit) |

> Semua tombol menggunakan `INPUT_PULLUP` (aktif LOW).

---

## 🕹️ Kontrol Tombol

### Mode Player (Layar Utama)

| Tombol | Tekan Singkat | Tekan Lama (≥800ms) |
|---|---|---|
| **BOOT** | Toggle Shuffle | Toggle Repeat (Off→All→One) |
| **A** | Play / Pause | Next Track |
| **B** | Previous Track | Restart Track (ulang dari awal) |
| **C** | Volume Up | Mute Toggle |
| **D** | Volume Down | Reset Volume ke Default |

### Kombinasi Tombol (tekan bersamaan)

| Kombinasi | Fungsi |
|---|---|
| **A + B** | Tampilkan info SD Card (total/pakai/sisa) |
| **A + C** | Tampilkan info sistem (PSRAM, heap, suhu, uptime, vibe status) |
| **B + C** | Rescan playlist dari SD Card |
| **C + D** | Toggle Sleep Timer (30 menit) |
| **A + D** | Buka **Experimental Menu** |

### Experimental Menu

| Tombol | Fungsi |
|---|---|
| **C / D** | Navigasi atas/bawah |
| **A** | Pilih/masuk sub-menu |
| **BOOT** | Kembali ke Player |

Isi menu:
1. **BT Scan** — pindai & pilih perangkat Bluetooth baru
2. **TX Gain BT** — atur daya pancar Bluetooth
3. **AVRCP Ctrl** — aktif/nonaktifkan kontrol remote dari headset
4. **Equalizer** — pilih & terapkan preset EQ
5. **Crossfade** — aktif/nonaktifkan transisi halus antar lagu

---

## 📂 Struktur Menu

```
MODE_PLAYER (default)
 ├─ MODE_EXPERIMENTAL_MENU
 │   ├─ MODE_BT_SCAN       (scan & pilih device BT)
 │   ├─ MODE_TX_GAIN       (atur TX power)
 │   └─ MODE_EQUALIZER     (pilih preset EQ)
 └─ (overlay info: SD info, System info, dsb.)
```

---

## 🏗️ Arsitektur Sistem

### Task & Core Allocation

| Task | Core | Fungsi |
|---|---|---|
| `loop()` (Arduino main) | Core 1 | UI, tombol, state machine player |
| `audioDecodeTask` | Core 0 | Decode MP3/WAV → tulis ke ring buffer |
| `ryneTask` | Core 1 | Monitoring sinyal + update vibe learning (setiap 500ms) |
| `getSoundData()` (BT callback) | Internal BT stack | Membaca ring buffer → dikirim ke speaker BT |

### Alur Data Audio

```
SD Card (.mp3/.wav)
     │
     ▼
audioDecodeTask (Core 0)
  - Decode via MP3DecoderHelix / WAVDecoder
  - Tulis PCM ke Ring Buffer
     │
     ▼
Ring Buffer (2MB PSRAM / 64KB fallback)
     │
     ▼
getSoundData() [dipanggil A2DP stack]
  - Baca dari ring buffer
  - Terapkan EQ (jika aktif)
  - Terapkan Crossfade & Fade
  - Kirim ke Bluetooth A2DP Source
     │
     ▼
Speaker/Headset Bluetooth
```

### Silence/Track-End Guard (Fix v1.2.0)

Untuk mencegah lagu berhenti prematur, sistem menggunakan penghitung "silence frame" yang **hanya bertambah** ketika:
- Decoding file sudah selesai (`fileReadDone = true`) **DAN**
- Ring buffer benar-benar kosong (`ringAvail() == 0`)

Lagu baru dianggap **benar-benar selesai** setelah kondisi silence tersebut bertahan selama **512 frame berturut-turut** (~5.8 detik pada 44.1kHz stereo). Jika hanya underrun sementara (buffer belum penuh tapi decoding masih jalan), sistem akan mengeluarkan data parsial + zero-padding tanpa menghentikan lagu.

---

## ⚙️ Instalasi & Setup

### 1. Kebutuhan Library (Arduino IDE / PlatformIO)

```
- AudioTools
- BluetoothA2DPSource
- Adafruit GFX Library
- Adafruit SSD1306
- ESP32 Arduino Core (dengan esp_bt, esp_avrc_api)
```

### 2. Siapkan SD Card

- Format FAT32
- Masukkan file `.mp3` atau `.wav` di root folder (atau sub-folder, akan ter-scan otomatis)

### 3. Upload Firmware

- Pilih board ESP32 yang sesuai (disarankan dengan PSRAM diaktifkan di menu Tools)
- Compile & upload `ESP32CAM_MusicPlayer.ino`

### 4. Pairing Bluetooth Pertama Kali

- Saat pertama kali nyala tanpa histori BT tersimpan, perangkat otomatis masuk **BT Scan Mode**
- Pilih perangkat speaker/headset dari daftar hasil scan, tekan **A** untuk connect
- Nama perangkat akan tersimpan otomatis untuk auto-reconnect berikutnya

---

## 💾 Penyimpanan (Preferences)

Menggunakan `Preferences` (NVS) dengan namespace `"btmusic"`, menyimpan:

| Key | Isi |
|---|---|
| `volume` | Volume terakhir |
| `track` | Index lagu terakhir |
| `repeat` | Mode repeat |
| `shuffle` | Status shuffle |
| `btname` | Nama perangkat BT tersimpan |
| `txgain` | Level TX power |
| `avrcp` | Status fitur AVRCP |
| `eq` / `eqpreset` | Status & preset equalizer |
| `crossfade` | Status crossfade |

Selain itu, file `/vibe_w.bin` di SD Card menyimpan **bobot model AI vibe learning** (lihat bagian RYNE), agar preferensi mood pengguna tidak hilang saat restart.

---

## 🐛 Changelog / Riwayat Perbaikan

### v1.2.1
- **[COMPILE FIX]** Variabel lokal `free` pada `audioDecodeTask()` mengganti nama fungsi `free()` bawaan (variable shadowing), menyebabkan error kompilasi `'free' cannot be used as a function`. Diganti menjadi `freeSpace`.

### v1.2.0 (Critical Fix)
- **[BUG FIX]** Lagu berhenti prematur sebelum selesai, disebabkan oleh:
  1. `TRACK_END_SILENCE_FRAMES` terlalu kecil (172 → dinaikkan ke 512)
  2. `getSoundData()` mengeluarkan silence saat buffer kurang padahal decoding belum selesai (underrun sementara) → diperbaiki menjadi output data parsial + zero-padding tanpa menaikkan silence counter
  3. Silence counter tidak di-reset saat buffer terisi kembali
  4. Ditambahkan guard: `trackEnded` hanya di-set setelah silence berturut-turut sesuai `TRACK_END_SILENCE_FRAMES` **dan** decoding benar-benar selesai
- **[BUG FIX]** `BT_PREFILL_BYTES` terlalu besar menyebabkan delay panjang sebelum audio mulai diputar → dikurangi menjadi 16KB (non-PSRAM) / 32KB (PSRAM)
- **[BUG FIX]** Decode task ditambah mekanisme retry & timeout agar tidak stuck saat ring buffer penuh

---

## ⚠️ Catatan & Batasan

- Perangkat ini bertindak sebagai **A2DP Source** (mengirim audio), bukan Sink — jadi **tidak bisa** menerima audio dari HP untuk diputar melalui speaker eksternal.
- Model AI vibe learning bersifat **ringan (lightweight)** dan dioptimalkan untuk mikrokontroler — bukan pengganti sistem rekomendasi musik berskala besar.
- Tanpa PSRAM, fitur pembelajaran vibe (riwayat & training) otomatis dinonaktifkan, hanya menyisakan heuristik statis.
- Sensitivitas deteksi vibe bergantung pada kalibrasi threshold (`VIBE_ENERGETIC_THRESHOLD`, dll.) yang bisa disesuaikan sesuai preferensi di bagian konfigurasi kode.

---

*Dibuat dengan ❤️ menggunakan ESP32, AudioTools, dan sedikit sentuhan Machine Learning ringan bernama RYNE.*
