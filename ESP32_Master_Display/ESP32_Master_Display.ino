/* ==============================================================================
 * PROYEK     : SISTEM MONITOR RADIASI AREA
 * UNIT       : TIM IRADIATOR - PROGRAMMER
 * FILE       : ESP32_Master_Display.ino
 * BOARD      : ESP32 Dev Module
 * DESKRIPSI  : Master Gateway & Display HMI Nextion TFT LCD.
 *              - Menerima data LoRa dari Node Outdoor & Indoor (tembus tembok tebal).
 *              - Membaca sensor lokal GM Kit (Area Master).
 *              - Memperbarui layar Nextion HMI secara real-time.
 *              - Mengirim data ke Database MySQL (via Server Laravel API).
 *              - Meneruskan data ke Google Spreadsheet untuk penyimpanan cloud.
 * ==============================================================================
 * PANDUAN WIRING PIN (ESP32):
 * ------------------------------------------------------------------------------
 * KOMPONEN               PIN ESP32         KETERANGAN
 * Modul LoRa SCK         GPIO 18           SPI Clock
 * Modul LoRa MISO        GPIO 19           SPI MISO
 * Modul LoRa MOSI        GPIO 23           SPI MOSI
 * Modul LoRa NSS / CS    GPIO 5            SPI Chip Select
 * Modul LoRa RST         GPIO 14           Reset Pin
 * Modul LoRa DIO0        GPIO 2            Interrupt Pin LoRa
 * ------------------------------------------------------------------------------
 * Nextion HMI TX         GPIO 16 (RX2)     Serial2 Receiver ESP32
 * Nextion HMI RX         GPIO 17 (TX2)     Serial2 Transmitter ESP32
 * GM Kit (Pulse / Out)   GPIO 12           Interrupt pulsa radiasi lokal
 * VCC                    3.3V / 5V         LoRa wajib 3.3V, Nextion 5V
 * GND                    GND               Wajib satu common ground
 * ==============================================================================
 */

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ==============================================================================
// 1. KONFIGURASI JARINGAN & SERVER / SPREADSHEET
// ==============================================================================
const char* WIFI_SSID         = "NAMA_WIFI_ANDA";
const char* WIFI_PASSWORD     = "PASSWORD_WIFI_ANDA";

// Endpoint API Server Laravel (Menyimpan data ke MySQL)
const char* SERVER_INDOOR     = "http://192.168.1.100:8000/api/store-data-indoor-monitor";
const char* SERVER_OUTDOOR    = "http://192.168.1.100:8000/api/store-data-outdoor-monitor";
const char* API_KEY           = "radiation_monitor_secret_key";

// Webhook Google Apps Script untuk Google Spreadsheet (Opsional kirim langsung)
// Ganti dengan Web App URL dari Google Apps Script Anda jika ingin direct upload
const char* GOOGLE_SHEET_URL  = ""; 

// ==============================================================================
// 2. KONFIGURASI LORA, GM KIT & NEXTION
// ==============================================================================
#define LORA_BAND        433E6   // 433E6 atau 915E6 (Wajib sama dengan transmitter)
#define LORA_SS          5
#define LORA_RST         14
#define LORA_DIO0        2

#define GM_PIN           12      // Pin interrupt GM Kit lokal
#define DOSE_LIMIT_WARN  1.0     // Batas peringatan radiasi layar (uSv/jam)

#define LOG_PERIOD       30000   // Sampling GM Kit (30 detik)
#define MAX_PERIOD       60000

HardwareSerial nextionSerial(2); // Serial2: Pin 16 (RX2) dan Pin 17 (TX2)

// Variabel GM Kit Lokal Master
volatile unsigned long countsLocal = 0;
unsigned long previousMillis       = 0;
unsigned int multiplier;
double doseRateMaster              = 0.0;

// Variabel data tersimpan dari LoRa Outdoor
double suhuLuar         = 0.0;
double kelembapanLuar   = 0.0;
double doseRateLuar     = 0.0;
int cpsLuar             = 0;

// Variabel data tersimpan dari LoRa Indoor
double suhuDalam        = 0.0;
double tekananDalam     = 0.0;
double doseRateDalam    = 0.0;
int cpsDalam            = 0;
unsigned long seqIndoor = 0;
String relayIndoor      = "ON";

void IRAM_ATTR impulseLocal() {
  countsLocal++;
}

double Sieverts(double cpm) {
  return cpm * 0.00571;
}

// Fungsi bantu kirim perintah teks ke Layar Nextion HMI
void lcdStr(String cmd, String str) {
  nextionSerial.print(cmd);
  nextionSerial.print("\"" + str + "\"");
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
}

// Fungsi bantu kirim nilai angka ke Layar Nextion HMI
void lcdVal(String cmd, double val) {
  String valStr = String(val, 2);
  nextionSerial.print(cmd);
  nextionSerial.print("\"" + valStr + "\"");
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
}

// ==============================================================================
// 4. PENGIRIMAN DATA KE MYSQL (SERVER LARAVEL) & SPREADSHEET
// ==============================================================================
void kirimDataKeMySQL(const char* url, String jsonBody) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Api-Key", API_KEY);

  int httpCode = http.POST(jsonBody);
  if (httpCode > 0) {
    Serial.printf("[MYSQL UPLOAD] Sukses disimpan ke Database (HTTP %d)\n", httpCode);
  } else {
    Serial.printf("[MYSQL ERROR] Gagal simpan: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void kirimKeGoogleSheet(String jsonBody) {
  if (WiFi.status() != WL_CONNECTED || strlen(GOOGLE_SHEET_URL) == 0) return;

  HTTPClient http;
  http.begin(GOOGLE_SHEET_URL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(jsonBody);
  Serial.printf("[SPREADSHEET] Response HTTP: %d\n", httpCode);
  http.end();
}

void updateNextionDisplay() {
  // Area 1 (Luar Ruangan)
  lcdVal("suhu1.txt=", suhuLuar);
  lcdVal("kelembapan1.txt=", kelembapanLuar);
  lcdVal("doseRate1.txt=", doseRateLuar);

  // Area 2 (Dalam Ruangan)
  lcdVal("suhu2.txt=", suhuDalam);
  lcdVal("kelembapan2.txt=", tekananDalam); // Menampilkan Tekanan (hPa)
  lcdVal("doseRate2.txt=", doseRateDalam);

  // Status Banner Keselamatan
  if (doseRateDalam > DOSE_LIMIT_WARN || doseRateLuar > DOSE_LIMIT_WARN) {
    lcdStr("status.txt=", "Peringatan, laju dosis tinggi!");
  } else {
    lcdStr("status.txt=", "Status: Aman");
  }
}

void setup() {
  Serial.begin(115200);
  nextionSerial.begin(9600, SERIAL_8N1, 16, 17); // Nextion pada GPIO 16 & 17
  multiplier = MAX_PERIOD / LOG_PERIOD;

  // GM Kit Lokal
  pinMode(GM_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(GM_PIN), impulseLocal, FALLING);

  // Inisialisasi Layar Nextion
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
  lcdStr("wifiConnect.txt=", "Connecting ...");

  // Hubungkan WiFi Ruang Operator
  Serial.println("\n[ESP32 Master Gateway & Display]");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Gateway Terhubung! IP: " + WiFi.localIP().toString());
    lcdStr("wifiConnect.txt=", "Wifi Connected");
  } else {
    Serial.println("\nWiFi belum terhubung, mode offline.");
    lcdStr("wifiConnect.txt=", "Offline Mode");
  }

  // Inisialisasi LoRa Receiver
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("Error: Modul LoRa Receiver Gagal!");
    while (true) delay(1000);
  }

  LoRa.setSpreadingFactor(10);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("LoRa Receiver Aktif. Menunggu transmisi Indoor & Outdoor...");
}

void loop() {
  // 1. Kalkulasi GM Kit Lokal Master
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > LOG_PERIOD) {
    previousMillis = currentMillis;
    doseRateMaster = Sieverts(countsLocal * multiplier);
    countsLocal = 0;
  }

  // 2. Cek Apakah Ada Paket LoRa Masuk dari Node
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }

    int rssi = LoRa.packetRssi();
    Serial.printf("\n[LORA RX] Paket Diterima (%d bytes | RSSI: %d dBm): %s\n", packetSize, rssi, incoming.c_str());

    // Parsing payload JSON dari transmitter
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, incoming);

    if (!error) {
      const char* node = doc["node"];

      if (strcmp(node, "INDOOR") == 0) {
        // --- DATA DARI RUANG RADIASI (INDOOR) ---
        seqIndoor       = doc["seq"] | 0;
        cpsDalam        = doc["cps"] | 0;
        doseRateDalam   = doc["usvh"] | 0.0;
        suhuDalam       = doc["suhu"] | 0.0;
        tekananDalam    = doc["tekanan"] | 0.0;
        relayIndoor     = doc["relay"] | "ON";

        Serial.printf("-> Node INDOOR: Dosis: %.2f uSv/h | Suhu: %.1f C | Relay: %s\n",
                      doseRateDalam, suhuDalam, relayIndoor.c_str());

        // Format data untuk MySQL Laravel
        StaticJsonDocument<384> dbDoc;
        dbDoc["seq"]        = seqIndoor;
        dbDoc["cps"]        = cpsDalam;
        dbDoc["usvh"]       = doseRateDalam;
        dbDoc["suhu"]       = suhuDalam;
        dbDoc["kelembapan"] = tekananDalam; // Mengisi kolom kelembapan/tekanan di DB
        dbDoc["relay"]      = relayIndoor;
        dbDoc["jaringan"]   = "LoRa";
        dbDoc["rssi"]       = rssi;

        String dbPayload;
        serializeJson(dbDoc, dbPayload);

        // Simpan ke MySQL & Spreadsheet
        kirimDataKeMySQL(SERVER_INDOOR, dbPayload);
        kirimKeGoogleSheet(dbPayload);

      } else if (strcmp(node, "OUTDOOR") == 0) {
        // --- DATA DARI AREA LUAR (OUTDOOR) ---
        cpsLuar         = doc["cps"] | 0;
        doseRateLuar    = doc["usvh"] | 0.0;
        suhuLuar        = doc["suhu"] | 0.0;
        kelembapanLuar  = doc["kelembapan"] | 0.0;

        Serial.printf("-> Node OUTDOOR: Dosis: %.2f uSv/h | Suhu: %.1f C | RH: %.1f %%\n",
                      doseRateLuar, suhuLuar, kelembapanLuar);

        // Format data untuk MySQL Laravel
        StaticJsonDocument<256> dbDoc;
        dbDoc["cps"]        = cpsLuar;
        dbDoc["usvh"]       = doseRateLuar;
        dbDoc["suhu"]       = suhuLuar;
        dbDoc["kelembapan"] = kelembapanLuar;

        String dbPayload;
        serializeJson(dbDoc, dbPayload);

        // Simpan ke MySQL & Spreadsheet
        kirimDataKeMySQL(SERVER_OUTDOOR, dbPayload);
        kirimKeGoogleSheet(dbPayload);
      }

      // Update Tampilan Layar Nextion HMI
      updateNextionDisplay();
    } else {
      Serial.println("Peringatan: Gagal mem-parse JSON dari paket LoRa.");
    }
  }
}
