/* ==============================================================================
 * PROYEK     : SISTEM MONITOR RADIASI AREA
 * UNIT       : TIM IRADIATOR - PROGRAMMER
 * FILE       : ESP32_Outdoor_Monitor.ino
 * BOARD      : ESP32 Dev Module
 * DESKRIPSI  : Membaca sensor Detektor Geiger-Müller (GM) dan DHT22 di area luar
 *              (outdoor/ruang operator), menghitung laju dosis radiasi latar,
 *              dan mengirim data via HTTP POST ke Server Web Laravel.
 * ==============================================================================
 * PANDUAN WIRING PIN (ESP32):
 * ------------------------------------------------------------------------------
 * KOMPONEN               PIN ESP32         KETERANGAN
 * Detektor GM (Pulse)    GPIO 12           Interrupt pulsa radiasi (Active LOW)
 * Sensor DHT22 (Data)    GPIO 15           Sensor suhu & kelembapan
 * VCC Komponen           5V / 3.3V         Sesuai spesifikasi modul
 * GND                    GND               Wajib satu common ground
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
const char* WIFI_SSID     = "NAMA_WIFI_ANDA";
const char* WIFI_PASSWORD = "PASSWORD_WIFI_ANDA";

// Endpoint API server Laravel untuk monitor luar
const char* SERVER_POST   = "http://192.168.1.100:8000/api/store-data-outdoor-monitor";

// API Key wajib sama dengan konfigurasi di file .env server
const char* API_KEY       = "radiation_monitor_secret_key";

// ==============================================================================
// 2. KONFIGURASI PERANGKAT KERAS & SAMPLING
// ==============================================================================
#define GM_PIN         12     // Pin interrupt pulsa tabung GM
#define DHTPIN         15     // Pin data DHT22
#define DHTTYPE        DHT22

#define LOG_PERIOD     30000  // Periode hitung laju dosis (30 detik)
#define MAX_PERIOD     60000  // Skala 1 menit (60 detik) untuk konversi CPM
#define SEND_INTERVAL  5000   // Interval pengiriman HTTP POST (5 detik)

DHT dht(DHTPIN, DHTTYPE);

// Variabel kalkulasi radiasi
volatile unsigned long counts = 0;
unsigned long previousMillis  = 0;
unsigned long lastSendTime    = 0;
unsigned int multiplier;

double rerataCPM;
int currentCPM = 0;
double doseRate = 0.0;
double pencacahanArray[100];

// Interrupt Service Routine (ISR) untuk mencacah pulsa GM
void IRAM_ATTR impulse() {
  counts++;
}

// Konversi CPM ke laju dosis (uSv/jam)
double Sieverts(double cpm) {
  // Faktor kalibrasi standar tabung GM J305 / M4011
  return cpm * 0.00571;
}

// Pembulatan 2 desimal
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

void setup() {
  Serial.begin(115200);
  multiplier = MAX_PERIOD / LOG_PERIOD;

  // Inisialisasi Sensor & Interrupt
  dht.begin();
  pinMode(DHTPIN, INPUT);
  pinMode(GM_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(GM_PIN), impulse, FALLING);

  // Sambungkan ke WiFi
  Serial.println("\n[ESP32 Outdoor Monitor]");
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Berhasil Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // 1. Pembacaan Sensor Lingkungan DHT22
  double kelembaban = dht.readHumidity();
  double suhu       = dht.readTemperature();

  if (isnan(kelembaban) || isnan(suhu)) {
    Serial.println("Peringatan: Gagal membaca sensor DHT22. Periksa kabel!");
    delay(1000);
    return;
  }

  // 2. Kalkulasi CPM & Laju Dosis Radiasi Tiap 30 Detik
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > LOG_PERIOD) {
    previousMillis = currentMillis;
    pencacahanArray[currentCPM] = counts * multiplier;
    doseRate = Sieverts(pencacahanArray[currentCPM]);
    counts = 0;

    // Moving average untuk stabilitas
    rerataCPM = 0;
    for (int x = 0; x <= currentCPM; x++) {
      rerataCPM += pencacahanArray[x];
    }
    rerataCPM /= (currentCPM + 1);

    Serial.printf("[UPDATE RADIASI] CPM: %.1f | Laju Dosis: %.4f uSv/h\n", rerataCPM, doseRate);
    currentCPM++;
    if (currentCPM >= 100) currentCPM = 0;
  }

  // 3. Pengiriman Data ke Server Laravel via HTTP POST
  if ((millis() - lastSendTime) > SEND_INTERVAL) {
    lastSendTime = millis();

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(SERVER_POST);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Api-Key", API_KEY);

      // Siapkan payload JSON
      StaticJsonDocument<256> doc;
      doc["cps"]        = counts;
      doc["usvh"]       = round2(doseRate);
      doc["suhu"]       = round2(suhu);
      doc["kelembapan"] = round2(kelembaban);

      String requestBody;
      serializeJson(doc, requestBody);

      int httpResponseCode = http.POST(requestBody);

      if (httpResponseCode > 0) {
        Serial.printf("[POST OUTDOOR] HTTP %d | Dosis: %.2f uSv/h | Suhu: %.1f C | RH: %.1f %%\n",
                      httpResponseCode, doseRate, suhu, kelembaban);
      } else {
        Serial.printf("[POST OUTDOOR ERROR] %s\n", http.errorToString(httpResponseCode).c_str());
      }

      http.end();
    } else {
      Serial.println("[WIFI] Koneksi terputus, mencoba menghubungkan ulang...");
      WiFi.reconnect();
    }
  }
}
