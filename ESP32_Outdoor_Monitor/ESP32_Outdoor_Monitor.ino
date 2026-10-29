/* ==============================================================================
 * PROYEK     : SISTEM MONITOR RADIASI AREA
 * UNIT       : TIM IRADIATOR - PROGRAMMER
 * FILE       : ESP32_Outdoor_Monitor.ino
 * BOARD      : ESP32 Dev Module
 * DESKRIPSI  : Node Outdoor (Luar Ruangan). Membaca GM Kit dan DHT22.
 *              Karena terhalang dinding tebal tanpa sinyal WiFi, data dikirim
 *              langsung ke Master Display menggunakan MODUL LORA (SX1278).
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
 * GM Kit (Pulse / Out)   GPIO 12           Interrupt pulsa radiasi (Active LOW)
 * Sensor DHT22 (Data)    GPIO 15           Sensor suhu & kelembapan luar
 * VCC                    3.3V / 5V         LoRa wajib 3.3V!
 * GND                    GND               Wajib satu common ground
 * ==============================================================================
 */

#include <SPI.h>
#include <LoRa.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ==============================================================================
// 1. KONFIGURASI LORA & SAMPLING
// ==============================================================================
#define LORA_BAND      433E6   // Frekuensi LoRa: 433E6 (433MHz) atau 915E6 (915MHz)
#define LORA_SS        5       // GPIO 5 (NSS/CS)
#define LORA_RST       14      // GPIO 14 (RST)
#define LORA_DIO0      2       // GPIO 2 (DIO0)

#define GM_PIN         12      // Pin pulsa GM Kit
#define DHTPIN         15      // Pin data DHT22
#define DHTTYPE        DHT22

#define LOG_PERIOD     30000   // Sampling laju dosis (30 detik)
#define MAX_PERIOD     60000   // Skala 1 menit untuk konversi CPM
#define SEND_INTERVAL  5000    // Interval kirim via LoRa (5 detik)

DHT dht(DHTPIN, DHTTYPE);

// Variabel data radiasi GM Kit
volatile unsigned long counts = 0;
unsigned long previousMillis  = 0;
unsigned long lastSendTime    = 0;
unsigned int multiplier;
double doseRate               = 0.0;

// Interrupt Service Routine (ISR) GM Kit
void IRAM_ATTR impulse() {
  counts++;
}

// Konversi CPM ke laju dosis (uSv/jam)
double Sieverts(double cpm) {
  return cpm * 0.00571;
}

double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

void setup() {
  Serial.begin(115200);
  multiplier = MAX_PERIOD / LOG_PERIOD;

  dht.begin();
  pinMode(DHTPIN, INPUT);
  pinMode(GM_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(GM_PIN), impulse, FALLING);

  // Inisialisasi Modul LoRa Transceiver
  Serial.println("\n[ESP32 Outdoor Monitor via LoRa]");
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("Error: Modul LoRa gagal diinisialisasi! Periksa sambungan SPI.");
    while (true) delay(1000);
  }

  // Pengaturan parameter LoRa untuk penetrasi dinding tebal
  LoRa.setTxPower(20);          // Daya transmisi maksimal (20 dBm)
  LoRa.setSpreadingFactor(10);  // SF10: Jangkauan jauh dan tembus halangan
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("Modul LoRa Outdoor Siap Memancar!");
}

void loop() {
  // 1. Baca Sensor Suhu & Kelembapan DHT22
  double kelembaban = dht.readHumidity();
  double suhu       = dht.readTemperature();

  if (isnan(kelembaban) || isnan(suhu)) {
    kelembaban = 0.0;
    suhu       = 0.0;
  }

  // 2. Kalkulasi Laju Dosis Radiasi GM Kit
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > LOG_PERIOD) {
    previousMillis = currentMillis;
    doseRate = Sieverts(counts * multiplier);
    counts = 0;
  }

  // 3. Kirim Paket Data ke Master Display melalui LoRa
  if ((millis() - lastSendTime) > SEND_INTERVAL) {
    lastSendTime = millis();

    // Susun payload JSON
    StaticJsonDocument<200> doc;
    doc["node"]       = "OUTDOOR";
    doc["cps"]        = counts;
    doc["usvh"]       = round2(doseRate);
    doc["suhu"]       = round2(suhu);
    doc["kelembapan"] = round2(kelembaban);

    String payload;
    serializeJson(doc, payload);

    // Pancarkan paket LoRa
    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();

    Serial.printf("[LORA TX OUTDOOR] Dosis: %.2f uSv/h | Suhu: %.1f C | RH: %.1f %% -> Terkirim ke Master\n",
                  doseRate, suhu, kelembaban);
  }
}
