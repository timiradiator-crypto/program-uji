/* ==============================================================================
 * PROYEK     : SISTEM KONTROL & KALKULATOR RADIASI IRADIATOR
 * UNIT       : TIM IRADIATOR - PROGRAMMER
 * FILE       : Program_Uji.ino
 * DESKRIPSI  : Program Arduino untuk menghitung nilai radiasi antara Nilai Target
 *              dan Nilai Energi Sumber, serta berkomunikasi dua arah dengan HMI Nextion.
 * KOMPATIBEL : Arduino Mega 2560 (Rekomendasi), Arduino Uno/Nano, ESP32
 * ==============================================================================
 * PANDUAN KONEKSI PIN (Arduino ke Nextion HMI):
 * ------------------------------------------------------------------------------
 * NEXTION HMI          ARDUINO MEGA 2560       ARDUINO UNO/NANO (SoftwareSerial)
 *   +5V                  5V                      5V (disarankan adaptor eksternal 5V/1A)
 *   GND                  GND                     GND (Wajib satu ground)
 *   TX                   RX1 (Pin 19)            Pin 10 (Soft RX)
 *   RX                   TX1 (Pin 18)            Pin 11 (Soft TX)
 * ==============================================================================
 */

#include <Arduino.h>

// ==============================================================================
// 1. KONFIGURASI HARDWARE MIKROKONTROLER
// ==============================================================================
// Pilih jenis board dengan mengaktifkan salah satu define di bawah ini:
#define BOARD_MEGA      // Untuk Arduino Mega 2560 (Hardware Serial1)
// #define BOARD_UNO    // Untuk Arduino Uno / Nano (SoftwareSerial Pin 10, 11)
// #define BOARD_ESP32  // Untuk ESP32 (Hardware Serial2 Pin 16, 17)

#if defined(BOARD_UNO)
  #include <SoftwareSerial.h>
  SoftwareSerial nextionSerial(10, 11); // RX = Pin 10, TX = Pin 11
#elif defined(BOARD_MEGA)
  #define nextionSerial Serial1        // Mega Serial1: RX1=Pin 19, TX1=Pin 18
#elif defined(BOARD_ESP32)
  #define nextionSerial Serial2        // ESP32 Serial2: RX2=Pin 16, TX2=Pin 17
#endif

// Baudrate komunikasi serial
#define NEXTION_BAUD    9600    // Standar baudrate default Nextion HMI
#define DEBUG_BAUD      115200  // Baudrate Serial Monitor PC untuk debugging

// ==============================================================================
// 2. DEFINISI MODE KALKULASI RADIASI
// ==============================================================================
enum ModeKalkulasi {
  // Mode 0: WAKTU PENYINARAN (Exposure / Irradiation Time)
  // Rumus: t = Target Dosis (kGy atau Gy) / Laju Energi Sumber (kGy/jam atau Gy/s)
  MODE_WAKTU_IRADIASI = 0,

  // Mode 1: ATENUASI / PENYERAPAN RADIASI (Hukum Lambert-Beer)
  // Rumus: Dosis Serap = Energi Sumber * (1 - e^(-mu * Tebal Target))
  MODE_ATENUASI_ABSORPSI = 1,

  // Mode 2: EFISIENSI / PERSENTASE KETERCAPAIAN TARGET
  // Rumus: Efisiensi (%) = (Target / Energi Sumber) * 100%
  MODE_EFISIENSI_RASIO = 2,

  // Mode 3: SELISIH / DEVIASI NILAI RADIASI
  // Rumus: Selisih = Target - Energi Sumber
  MODE_SELISIH_DEVIASI = 3
};

// Mode aktif default
ModeKalkulasi modeAktif = MODE_WAKTU_IRADIASI;

// Koefisien atenuasi linier bahan (mu, satuan 1/cm) untuk Mode 1
// Contoh: Air/Jaringan lunak untuk Gamma Co-60 (~1.25 MeV) ~ 0.063 cm^-1
float koefisienMu = 0.063;

// ==============================================================================
// 3. VARIABEL DATA
// ==============================================================================
float nilaiTarget      = 25.0;  // Nilai Target (cth: Dosis Target 25.0 kGy)
float nilaiEnergiSumber = 5.0;   // Nilai Energi Sumber (cth: Laju Dosis 5.0 kGy/jam)
float nilaiHasilRadiasi = 0.0;  // Hasil perhitungan nilai radiasi
String satuanHasil     = "jam"; // Satuan hasil perhitungan
String statusSistem    = "SIAP";

// Buffer pembacaan data serial dari Nextion
String rxBuffer = "";
unsigned long lastCalcTime = 0;
const unsigned long AUTO_UPDATE_INTERVAL = 1000; // Update otomatis setiap 1 detik

// ==============================================================================
// 4. PROTOKOL PENGIRIMAN DATA KE NEXTION (Akhiran 3x 0xFF)
// ==============================================================================
void kirimPerintahNextion(const String &cmd) {
  nextionSerial.print(cmd);
  nextionSerial.write(0xFF);
  nextionSerial.write(0xFF);
  nextionSerial.write(0xFF);
}

// Fungsi mengubah isi teks pada komponen Nextion
void setNextionText(const String &komponen, const String &isiTeks) {
  String cmd = komponen + ".txt=\"" + isiTeks + "\"";
  kirimPerintahNextion(cmd);
}

// Fungsi mengubah isi angka (val) pada komponen Nextion
void setNextionNumber(const String &komponen, long nilaiAngka) {
  String cmd = komponen + ".val=" + String(nilaiAngka);
  kirimPerintahNextion(cmd);
}

// ==============================================================================
// 5. ENGINE PERHITUNGAN RADIASI
// ==============================================================================
void hitungNilaiRadiasi() {
  // Proteksi nilai input tidak valid
  if (nilaiTarget < 0.0) nilaiTarget = 0.0;
  if (nilaiEnergiSumber < 0.0) nilaiEnergiSumber = 0.0;

  switch (modeAktif) {
    case MODE_WAKTU_IRADIASI:
      // Waktu Iradiasi = Target Dosis / Laju Dosis Energi Sumber
      if (nilaiEnergiSumber > 0.00001) {
        nilaiHasilRadiasi = nilaiTarget / nilaiEnergiSumber;
        satuanHasil = "Jam";
        statusSistem = "SUKSES: Dihitung";
      } else {
        nilaiHasilRadiasi = 0.0;
        satuanHasil = "N/A";
        statusSistem = "ERR: Sumber = 0";
      }
      break;

    case MODE_ATENUASI_ABSORPSI:
      // Penyerapan Dosis = I0 * (1 - exp(-mu * x))
      nilaiHasilRadiasi = nilaiEnergiSumber * (1.0 - exp(-koefisienMu * nilaiTarget));
      satuanHasil = "kGy/cm";
      statusSistem = "SUKSES: Dihitung";
      break;

    case MODE_EFISIENSI_RASIO:
      // Rasio Target thd Energi Sumber (%)
      if (nilaiEnergiSumber > 0.00001) {
        nilaiHasilRadiasi = (nilaiTarget / nilaiEnergiSumber) * 100.0;
        satuanHasil = "%";
        statusSistem = "SUKSES: Dihitung";
      } else {
        nilaiHasilRadiasi = 0.0;
        satuanHasil = "%";
        statusSistem = "ERR: Sumber = 0";
      }
      break;

    case MODE_SELISIH_DEVIASI:
      // Selisih nilai radiasi
      nilaiHasilRadiasi = nilaiTarget - nilaiEnergiSumber;
      satuanHasil = "Delta";
      statusSistem = "SUKSES: Dihitung";
      break;

    default:
      nilaiHasilRadiasi = 0.0;
      satuanHasil = "-";
      statusSistem = "ERR: Mode Salah";
      break;
  }

  // Debug ke Serial Monitor PC
  Serial.println(F("--- [HASIL KALKULASI RADIASI] ---"));
  Serial.print(F("Mode Kalkulasi : ")); Serial.println((int)modeAktif);
  Serial.print(F("Nilai Target   : ")); Serial.println(nilaiTarget, 3);
  Serial.print(F("Energi Sumber  : ")); Serial.println(nilaiEnergiSumber, 3);
  Serial.print(F("Hasil Radiasi  : ")); Serial.print(nilaiHasilRadiasi, 3);
  Serial.print(F(" ")); Serial.println(satuanHasil);
  Serial.print(F("Status Sistem  : ")); Serial.println(statusSistem);
  Serial.println(F("----------------------------------"));

  // Kirim hasil pembaruan ke tampilan HMI Nextion
  perbaruiTampilanHMI();
}

// ==============================================================================
// 6. UPDATE TAMPILAN KE NEXTION HMI
// ==============================================================================
void perbaruiTampilanHMI() {
  String strHasil = String(nilaiHasilRadiasi, 2) + " " + satuanHasil;
  String strTarget = String(nilaiTarget, 2);
  String strEnergi = String(nilaiEnergiSumber, 2);

  // 1. Dukungan format nama deskriptif (disarankan di Nextion Editor)
  setNextionText("tTarget", strTarget);
  setNextionText("tEnergy", strEnergi);
  setNextionText("tHasil",  strHasil);
  setNextionText("tStatus", statusSistem);

  // 2. Dukungan format nama default bawaan Nextion (t0, t1, t2, t3)
  // Supaya langsung tampil jika user memakai nama komponen default di HMI
  setNextionText("t0", strTarget);
  setNextionText("t1", strEnergi);
  setNextionText("t2", strHasil);
  setNextionText("t3", statusSistem);

  // 3. Update komponen angka numeric (n0, n1, n2) dalam bilangan bulat
  setNextionNumber("n0", (long)(nilaiTarget * 100));       // Disimpan x100 (2 desimal)
  setNextionNumber("n1", (long)(nilaiEnergiSumber * 100)); // Disimpan x100
  setNextionNumber("n2", (long)(nilaiHasilRadiasi * 100)); // Disimpan x100
}

// ==============================================================================
// 7. PARSER DATA DARI NEXTION
// ==============================================================================
// Memproses perintah string yang dikirim oleh Nextion atau Serial Monitor
void eksekusiPerintah(String baris) {
  baris.trim();
  if (baris.length() == 0) return;

  Serial.print(F("[RX PARSER] Menerima: "));
  Serial.println(baris);

  // Format 1: Perintah Hitung manual dari tombol HMI
  // Contoh Nextion Button Touch Release: print "CALC\n" atau print "HITUNG\n"
  if (baris.equalsIgnoreCase("CALC") || baris.equalsIgnoreCase("HITUNG")) {
    hitungNilaiRadiasi();
  }
  // Format 2: Pengaturan Nilai Target
  // Contoh: TARGET=30.5 atau T=30.5 atau SET:TARGET=30.5
  else if (baris.startsWith("TARGET=") || baris.startsWith("T=") || baris.indexOf("TARGET=") >= 0) {
    int idx = baris.indexOf('=');
    nilaiTarget = baris.substring(idx + 1).toFloat();
    hitungNilaiRadiasi();
  }
  // Format 3: Pengaturan Nilai Energi Sumber
  // Contoh: ENERGY=7.2 atau E=7.2 atau SUMBER=7.2
  else if (baris.startsWith("ENERGY=") || baris.startsWith("E=") || baris.startsWith("SUMBER=") || baris.indexOf("ENERGY=") >= 0) {
    int idx = baris.indexOf('=');
    nilaiEnergiSumber = baris.substring(idx + 1).toFloat();
    hitungNilaiRadiasi();
  }
  // Format 4: Penggantian Mode Perhitungan
  // Contoh: MODE=0 (Waktu), MODE=1 (Atenuasi), MODE=2 (Efisiensi)
  else if (baris.startsWith("MODE=") || baris.startsWith("M=")) {
    int idx = baris.indexOf('=');
    int m = baris.substring(idx + 1).toInt();
    if (m >= 0 && m <= 3) {
      modeAktif = (ModeKalkulasi)m;
      hitungNilaiRadiasi();
    }
  }
  // Format 5: Perintah Bantuan (Help)
  else if (baris.equalsIgnoreCase("HELP") || baris.equalsIgnoreCase("?")) {
    tampilkanBantuan();
  }
}

// Membaca data serial stream dari Nextion HMI
void prosesSerialNextion() {
  while (nextionSerial.available() > 0) {
    uint8_t b = nextionSerial.read();

    // Deteksi frame Nextion Event 0x65 (Touch Event)
    // 0x65 [page_id] [comp_id] [event_press/release] 0xFF 0xFF 0xFF
    if (b == 0x65) {
      delay(10); // Tunggu sisa byte tiba
      if (nextionSerial.available() >= 5) {
        uint8_t pageId = nextionSerial.read();
        uint8_t compId = nextionSerial.read();
        uint8_t eventType = nextionSerial.read();
        // Lewati 3 byte 0xFF
        nextionSerial.read(); nextionSerial.read(); nextionSerial.read();

        Serial.print(F("[HMI TOUCH] Page: ")); Serial.print(pageId);
        Serial.print(F(" | Comp ID: ")); Serial.print(compId);
        Serial.print(F(" | Event: ")); Serial.println(eventType);

        // Jika tombol hitung ditekan (sesuaikan ID komponen jika perlu)
        hitungNilaiRadiasi();
        return;
      }
    }

    // Deteksi pembatas baris ('\n' atau '\r') atau akhir paket 0xFF
    if (b == '\n' || b == '\r' || b == 0xFF) {
      if (rxBuffer.length() > 0) {
        eksekusiPerintah(rxBuffer);
        rxBuffer = "";
      }
    } else {
      // Masukkan karakter ke dalam buffer
      if (rxBuffer.length() < 64) {
        rxBuffer += (char)b;
      }
    }
  }
}

// Membaca input dari Serial Monitor PC untuk mempermudah uji coba tanpa HMI
void prosesSerialMonitor() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (rxBuffer.length() > 0) {
        eksekusiPerintah(rxBuffer);
        rxBuffer = "";
      }
    } else {
      rxBuffer += c;
    }
  }
}

// ==============================================================================
// 8. TAMPILAN BANTUAN DI SERIAL MONITOR PC
// ==============================================================================
void tampilkanBantuan() {
  Serial.println(F("\n========================================================"));
  Serial.println(F("     SISTEM KALKULATOR RADIASI HMI NEXTION (TIM IRADIATOR)"));
  Serial.println(F("========================================================"));
  Serial.println(F("Perintah Uji via Serial Monitor PC / Nextion:"));
  Serial.println(F("  TARGET=<nilai>  -> Ubah nilai target (cth: TARGET=25.0)"));
  Serial.println(F("  ENERGY=<nilai>  -> Ubah nilai energi sumber (cth: ENERGY=5.0)"));
  Serial.println(F("  MODE=<0-3>      -> 0: Waktu Iradiasi (t = Dosis / Laju)"));
  Serial.println(F("                     1: Atenuasi Lambert-Beer"));
  Serial.println(F("                     2: Efisiensi Target thd Sumber (%)"));
  Serial.println(F("                     3: Selisih Target vs Sumber"));
  Serial.println(F("  CALC            -> Picu perhitungan ulang"));
  Serial.println(F("  HELP            -> Tampilkan menu bantuan"));
  Serial.println(F("========================================================\n"));
}

// ==============================================================================
// 9. SETUP & LOOP UTAMA
// ==============================================================================
void setup() {
  // Inisialisasi Serial Debug ke PC
  Serial.begin(DEBUG_BAUD);
  delay(500);

  // Inisialisasi Serial ke Nextion
  #if defined(BOARD_UNO)
    nextionSerial.begin(NEXTION_BAUD);
  #elif defined(BOARD_MEGA)
    nextionSerial.begin(NEXTION_BAUD);
  #elif defined(BOARD_ESP32)
    nextionSerial.begin(NEXTION_BAUD, SERIAL_8N1, 16, 17); // RX=16, TX=17
  #endif

  Serial.println(F("[SYSTEM] Arduino Radiation Calculator Initialized."));
  tampilkanBantuan();

  // Reset komunikasi Nextion & sinkronisasi awal
  kirimPerintahNextion("");
  delay(100);

  // Lakukan perhitungan awal dan tampilkan di HMI Nextion
  hitungNilaiRadiasi();
}

void loop() {
  // 1. Tangani input data dari HMI Nextion
  prosesSerialNextion();

  // 2. Tangani input perintah dari Serial Monitor PC (opsional / debugging)
  prosesSerialMonitor();

  // 3. Heartbeat / update periodik jika diperlukan
  unsigned long skrg = millis();
  if (skrg - lastCalcTime >= AUTO_UPDATE_INTERVAL) {
    lastCalcTime = skrg;
    // Dapat diaktifkan jika menginginkan sinkronisasi berkala
  }
}
