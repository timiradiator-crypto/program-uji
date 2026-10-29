/* ==============================================================================
 * PROYEK     : SISTEM MONITOR RADIASI AREA
 * UNIT       : TIM IRADIATOR - PROGRAMMER
 * FILE       : ESP32_Indoor_Monitor.ino
 * BOARD      : ESP32 Dev Module
 * DESKRIPSI  : Membaca sensor Detektor Geiger-Müller (GM) & DHT22 di dalam ruang
 *              radiasi instalasi, mengontrol Relay Alarm keselamatan (Fail-Safe),
 *              dan mengirim data terurut (Sequence) ke Server Web Laravel.
 * ==============================================================================
 * PANDUAN WIRING PIN (ESP32):
 * ------------------------------------------------------------------------------
 * KOMPONEN               PIN ESP32         KETERANGAN
 * Detektor GM (Pulse)    GPIO 12           Interrupt pulsa radiasi (Active LOW)
 * Sensor DHT22 (Data)    GPIO 15           Sensor suhu & kelembapan
 * Modul Relay Alarm      GPIO 26           Kontrol sirene / interlock keselamatan
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

// Endpoint API server Laravel untuk monitor dalam
const char* SERVER_POST   = "http://192.168.1.100:8000/api/store-data-indoor-monitor";

// API Key wajib sama dengan konfigurasi di file .env server
const char* API_KEY       = "radiation_monitor_secret_key";

// ==============================================================================
// 2. KONFIGURASI HARDWARE & AMBANG BATAS SAFETY
// ==============================================================================
#define GM_PIN         12     // Pin interrupt pulsa GM
#define DHTPIN         15     // Pin data DHT22
#define DHTTYPE        DHT22
#define RELAY_PIN      26     // Pin modul relay alarm / sirene

// Batas aman radiasi ruang instalasi (10 uSv/jam)
#define THRESHOLD_USV  10.0   

#define LOG_PERIOD     30000  // Periode hitung dosis (30 detik)
#define MAX_PERIOD     60000  // Skala 1 menit (60 detik)
#define SEND_INTERVAL  5000   // Interval kirim data (5 detik)

DHT dht(DHTPIN, DHTTYPE);

// Variabel data radiasi & transmisi
volatile unsigned long counts = 0;
unsigned long previousMillis  = 0;
unsigned long lastSendTime    = 0;
unsigned int multiplier;
unsigned long seqNumber       = 1;   // Nomor urut paket data

double doseRate               = 0.0;
String relayStatus            = "ON"; // ON = Kondisi Aman, OFF = Terpicu Alarm Bahaya

// Interrupt Service Routine (ISR) pulsa GM
void IRAM_ATTR impulse() {
  counts++;
}

// Konversi CPM ke laju dosis (uSv/jam)
double Sieverts(double cpm) {
  return cpm * 0.00571;
}

// Pembulatan 2 desimal
double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

void setup() {
  Serial.begin(115200);
  multiplier = MAX_PERIOD / LOG_PERIOD;

  // Inisialisasi Sensor & Relay
  dht.begin();
  pinMode(DHTPIN, INPUT);
  pinMode(GM_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  // Kondisi awal normal (Relay aktif / tertutup)
  digitalWrite(RELAY_PIN, HIGH);

  attachInterrupt(digitalPinToInterrupt(GM_PIN), impulse, FALLING);

  // Sambungkan ke WiFi
  Serial.println("\n[ESP32 Indoor Monitor]");
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Indoor Berhasil Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  // 1. Pembacaan Sensor Lingkungan DHT22
  double kelembaban = dht.readHumidity();
  double suhu       = dht.readTemperature();

  if (isnan(kelembaban) || isnan(suhu)) {
    Serial.println("Peringatan: Gagal membaca sensor DHT22 Indoor!");
    delay(1000);
    return;
  }

  // 2. Kalkulasi Laju Dosis & Logika Proteksi Safety Alarm
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > LOG_PERIOD) {
    previousMillis = currentMillis;
    double currentCPM = counts * multiplier;
    doseRate = Sieverts(currentCPM);
    counts = 0;

    // Logika Alarm Fail-Safe:
    // Jika dosis melebihi ambang batas (10 uSv/h), lepaskan relay (LOW / OFF)
    // untuk memicu sirene bahaya / lampu rotary warning.
    if (doseRate > THRESHOLD_USV) {
      relayStatus = "OFF";          // Bahaya terdeteksi!
      digitalWrite(RELAY_PIN, LOW); // Trigger sirene alarm
      Serial.printf("[ALARM BAHAYA] Laju dosis tinggi: %.2f uSv/h!\n", doseRate);
    } else {
      relayStatus = "ON";           // Kondisi aman normal
      digitalWrite(RELAY_PIN, HIGH);
    }
  }

  // 3. Pengiriman Data ke Server Laravel via HTTP POST
  if ((millis() - lastSendTime) > SEND_INTERVAL) {
    lastSendTime = millis();

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(SERVER_POST);
      http.addHeader("Content-Type", "application/json");
      http.addHeader("Api-Key", API_KEY);

      // Siapkan payload JSON sesuai dokumentasi esp32-api.md
      StaticJsonDocument<384> doc;
      doc["seq"]        = seqNumber++;
      doc["cps"]        = counts;
      doc["usvh"]       = round2(doseRate);
      doc["suhu"]       = round2(suhu);
      doc["kelembapan"] = round2(kelembaban);
      doc["relay"]      = relayStatus;
      doc["jaringan"]   = "WiFi"; // Ubah jadi "LoRa" jika melalui modul LoRa
      doc["rssi"]       = WiFi.RSSI();

      String requestBody;
      serializeJson(doc, requestBody);

      int httpResponseCode = http.POST(requestBody);

      if (httpResponseCode > 0) {
        Serial.printf("[POST INDOOR #%lu] HTTP %d | Dosis: %.2f uSv/h | Suhu: %.1f C | Relay: %s | RSSI: %d dBm\n",
                      seqNumber - 1, httpResponseCode, doseRate, suhu, relayStatus.c_str(), WiFi.RSSI());
      } else {
        Serial.printf("[POST INDOOR ERROR] %s\n", http.errorToString(httpResponseCode).c_str());
      }

      http.end();
    } else {
      Serial.println("[WIFI INDOOR] Mencoba menghubungkan ulang...");
      WiFi.reconnect();
    }
  }
}
