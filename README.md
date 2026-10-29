# Sistem Kontrol & Kalkulator Radiasi Iradiator

**Unit:** Tim Iradiator - Programmer  
**Kompatibilitas:** Arduino Mega 2560 (Rekomendasi), Arduino Uno / Nano, ESP32

---

## Deskripsi
Program Arduino untuk menghitung nilai radiasi antara Nilai Target dan Nilai Energi Sumber, serta berkomunikasi dua arah dengan HMI Nextion.

## Panduan Koneksi Pin (Arduino ke Nextion HMI)
| Nextion HMI | Arduino Mega 2560 | Arduino Uno/Nano (SoftwareSerial) |
|-------------|-------------------|-----------------------------------|
| +5V         | 5V                | 5V (Adaptor eksternal 5V/1A)       |
| GND         | GND               | GND                               |
| TX          | RX1 (Pin 19)      | Pin 10 (Soft RX)                  |
| RX          | TX1 (Pin 18)      | Pin 11 (Soft TX)                  |

## Konfigurasi
Pilih jenis mikrokontroler di dalam `Program Uji.ino`:
```cpp
#define BOARD_MEGA      // Arduino Mega 2560
// #define BOARD_UNO    // Arduino Uno / Nano
// #define BOARD_ESP32  // ESP32
```
