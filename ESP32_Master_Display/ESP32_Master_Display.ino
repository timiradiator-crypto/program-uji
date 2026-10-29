/* ==============================================================================
 * PROYEK     : SISTEM MONITOR RADIASI AREA
 * UNIT       : TIM IRADIATOR - PROGRAMMER
 * FILE       : ESP32_Master_Display.ino
 * BOARD      : ESP32 Dev Module
 * DESKRIPSI  : Master Gateway & Display HMI Nextion TFT LCD.
 *              Mengambil data monitoring area dalam dari server via HTTP GET,
 *              mengirimkan data outdoor via HTTP POST, dan memperbarui seluruh
 *              parameter pembacaan (Suhu, Kelembapan, Laju Dosis, Status Bahaya)
 *              ke layar LCD Nextion secara real-time.
 * ==============================================================================
 * PANDUAN WIRING PIN (ESP32 ke Nextion HMI):
 * ------------------------------------------------------------------------------
 * NEXTION HMI          ESP32               KETERANGAN
 * +5V                  5V (VIN)            Daya modul (Disarankan adaptor eksternal)
 * GND                  GND                 Wajib satu ground
 * TX                   RX0 (GPIO 3)        Atau Serial2 RX2 (GPIO 16)
 * RX                   TX0 (GPIO 1)        Atau Serial2 TX2 (GPIO 17)
 * ------------------------------------------------------------------------------
 * SENSOR LOKAL (Opsional):
 * Detektor GM (Pulse)  GPIO 12             Interrupt pulsa radiasi luar
 * Sensor DHT22 (Data)  GPIO 15             Suhu & kelembapan ruang kontrol
 * ==============================================================================
 */

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ==============================================================================
// 1. KONFIGURASI JARINGAN & SERVER API
// ==============================================================================
const char* WIFI_SSID       = "NAMA_WIFI_ANDA";
const char* WIFI_PASSWORD   = "PASSWORD_WIFI_ANDA";

// Endpoint API Server Laravel
const char* SERVER_POST     = "http://192.168.1.100:8000/api/store-data-outdoor-monitor";
const char* SERVER_GET      = "http://192.168.1.100:8000/api/get-latest-indoor-data";

// API Key wajib sama dengan file .env server
const char* API_KEY         = "radiation_monitor_secret_key";

// ==============================================================================
// 2. KONFIGURASI HARDWARE & DISPLAY
// ==============================================================================
#define GM_PIN           12     // Pin GM Tube luar
#define DHTPIN           15     // Pin DHT22 ruang kontrol
#define DHTTYPE          DHT22

// Batas ambang peringatan display (uSv/jam)
#define DOSE_LIMIT_WARN  1.0    

#define LOG_PERIOD       30000  // Periode hitung radiasi (30 detik)
#define MAX_PERIOD       60000
#define UPDATE_INTERVAL  5000   // Interval update display & sinkronisasi (5 detik)

DHT dht(DHTPIN, DHTTYPE);

// Variabel sensor lokal (Luar)
volatile unsigned long counts = 0;
unsigned long previousMillis  = 0;
unsigned long lastUpdateTime  = 0;
unsigned int multiplier;
double doseRateLuar           = 0.0;

// Variabel data ruang dalam (Diambil dari Server via HTTP GET)
double suhuDalam       = 0.0;
double kelembapanDalam = 0.0;
double doseRateDalam   = 0.0;

// Interrupt Service Routine pulsa GM
void IRAM_ATTR impulse() {
  counts++;
}

double Sieverts(double cpm) {
  return cpm * 0.00571;
}

double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

// ==============================================================================
// 3. FUNGSI PROTOKOL KOMUNIKASI NEXTION HMI
// ==============================================================================
// Mengirim teks String ke komponen Nextion (diakhiri 0xFF 0xFF 0xFF)
void lcdStr(String cmd, String str) {
  Serial.print(cmd);
  Serial.print("\"" + str + "\"");
  Serial.write(0xff);
  Serial.write(0xff);
  Serial.write(0xff);
}

// Mengirim angka desimal ke komponen Nextion
void lcdVal(String cmd, double val) {
  String valStr = String(val, 2);
  Serial.print(cmd);
  Serial.print("\"" + valStr + "\"");
  Serial.write(0xff);
  Serial.write(0xff);
  Serial.write(0xff);
}

void setup() {
  // Serial baudrate 9600 untuk koneksi ke LCD Nextion
  Serial.begin(9600);
  multiplier = MAX_PERIOD / LOG_PERIOD;

  dht.begin();
  pinMode(DHTPIN, INPUT);
  pinMode(GM_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(GM_PIN), impulse, FALLING);

  // Inisialisasi layar Nextion
  Serial.write(0xff);
  Serial.write(0xff);
  Serial.write(0xff);
  lcdStr("wifiConnect.txt=", "Connecting ...");

  // Hubungkan WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  lcdStr("wifiConnect.txt=", "Wifi Connected");
}

void loop() {
  // 1. Baca Sensor Lokal (Ruang Luar / Kontrol)
  double kelembapanLuar = dht.readHumidity();
  double suhuLuar       = dht.readTemperature();

  if (isnan(kelembapanLuar) || isnan(suhuLuar)) {
    kelembapanLuar = 0.0;
    suhuLuar       = 0.0;
  }

  // 2. Hitung Laju Radiasi Lokal Berkala
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > LOG_PERIOD) {
    previousMillis = currentMillis;
    doseRateLuar = Sieverts(counts * multiplier);
    counts = 0;
  }

  // 3. Siklus Komunikasi Data & Update Tampilan Nextion HMI
  if ((millis() - lastUpdateTime) > UPDATE_INTERVAL) {
    lastUpdateTime = millis();

    if (WiFi.status() == WL_CONNECTED) {
      lcdStr("wifiConnect.txt=", "Wifi Connected");
      HTTPClient http;

      // --- A. POST DATA OUTDOOR KE SERVER ---
      http.begin(SERVER_POST);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Api-Key", API_KEY);

      StaticJsonDocument<200> docPost;
      docPost["cps"]        = counts;
      docPost["usvh"]       = round2(doseRateLuar);
      docPost["suhu"]       = round2(suhuLuar);
      docPost["kelembapan"] = round2(kelembapanLuar);

      String requestBody;
      serializeJson(docPost, requestBody);
      http.POST(requestBody);
      http.end();

      // --- B. GET DATA INDOOR TERKINI DARI SERVER ---
      http.begin(SERVER_GET);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Api-Key", API_KEY);

      int httpGetCode = http.GET();
      if (httpGetCode == HTTP_CODE_OK) {
        String payload = http.getString();
        StaticJsonDocument<512> docGet;
        DeserializationError err = deserializeJson(docGet, payload);

        if (!err) {
          suhuDalam       = docGet["suhu"] | 0.0;
          kelembapanDalam = docGet["kelembapan"] | 0.0;
          doseRateDalam   = docGet["usvh"] | 0.0;
        }
      }
      http.end();
    } else {
      lcdStr("wifiConnect.txt=", "Not Connected");
      WiFi.reconnect();
    }

    // --- C. PERBARUI SELURUH FIELD LAYAR NEXTION ---
    // Area 1 (Monitor Luar)
    lcdVal("suhu1.txt=", suhuLuar);
    lcdVal("kelembapan1.txt=", kelembapanLuar);
    lcdVal("doseRate1.txt=", doseRateLuar);

    // Area 2 (Monitor Dalam)
    lcdVal("suhu2.txt=", suhuDalam);
    lcdVal("kelembapan2.txt=", kelembapanDalam);
    lcdVal("doseRate2.txt=", doseRateDalam);

    // Banner Status Peringatan
    if (doseRateDalam > DOSE_LIMIT_WARN || doseRateLuar > DOSE_LIMIT_WARN) {
      lcdStr("status.txt=", "Peringatan, laju dosis tinggi!");
    } else {
      lcdStr("status.txt=", "Status: Aman");
    }
  }
}
