/* ==============================================================================
 * PROYEK     : SISTEM MONITOR RADIASI AREA
 * UNIT       : TIM IRADIATOR - PROGRAMMER
 * FILE       : ESP32_Indoor_Monitor.ino
 * BOARD      : ESP32 Dev Module
 * DESKRIPSI  : Node Indoor (Ruang Radiasi). Membaca GM Kit dan BMP280 (I2C).
 *              Mengontrol Relay Alarm (Fail-Safe), dan mengirim data ke Master
 *              Display melewati dinding pelindung tebal menggunakan MODUL LORA.
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
 * Sensor BMP280 (SDA)    GPIO 21           I2C Serial Data
 * Sensor BMP280 (SCL)    GPIO 22           I2C Serial Clock
 * GM Kit (Pulse / Out)   GPIO 12           Interrupt pulsa radiasi (Active LOW)
 * Modul Relay Alarm      GPIO 26           Kontrol sirene / interlock keselamatan
 * VCC                    3.3V / 5V         LoRa wajib 3.3V!
 * GND                    GND               Wajib satu common ground
 * ==============================================================================
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <ArduinoJson.h>

// ==============================================================================
// 1. KONFIGURASI LORA & AMBANG BATAS SAFETY
// ==============================================================================
#define LORA_BAND      433E6   // Frekuensi LoRa: 433E6 (433MHz) atau 915E6 (915MHz)
#define LORA_SS        5       // GPIO 5 (NSS/CS)
#define LORA_RST       14      // GPIO 14 (RST)
#define LORA_DIO0      2       // GPIO 2 (DIO0)

#define GM_PIN         12      // Pin interrupt pulsa GM Kit
#define RELAY_PIN      26      // Pin modul relay alarm / sirene
#define THRESHOLD_USV  10.0    // Batas aman radiasi ruang radiasi (10 uSv/jam)

#define LOG_PERIOD     30000   // Sampling laju radiasi (30 detik)
#define MAX_PERIOD     60000   // Skala 1 menit untuk konversi CPM
#define SEND_INTERVAL  5000    // Interval kirim via LoRa (5 detik)

Adafruit_BMP280 bmp; // Sensor I2C (SDA: 21, SCL: 22)

// Variabel data radiasi GM Kit & status transmisi
volatile unsigned long counts = 0;
unsigned long previousMillis  = 0;
unsigned long lastSendTime    = 0;
unsigned int multiplier;
unsigned long seqNumber       = 1;   // Nomor urut paket data

double doseRate               = 0.0;
String relayStatus            = "ON"; // ON = Kondisi Aman, OFF = Bahaya

// Interrupt Service Routine (ISR) pulsa GM Kit
void IRAM_ATTR impulse() {
  counts++;
}

double Sieverts(double cpm) {
  return cpm * 0.00571;
}

double round2(double value) {
  return (int)(value * 100 + 0.5) / 100.0;
}

void setup() {
  Serial.begin(115200);
  multiplier = MAX_PERIOD / LOG_PERIOD;

  // Inisialisasi I2C BMP280
  Wire.begin(21, 22);
  bool bmpStatus = bmp.begin(0x76);
  if (!bmpStatus) bmpStatus = bmp.begin(0x77);

  if (!bmpStatus) {
    Serial.println("Peringatan: BMP280 tidak terdeteksi pada pin 21/22.");
  } else {
    Serial.println("BMP280 siap digunakan.");
  }

  // Inisialisasi GM Kit & Relay
  pinMode(GM_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Standar kondisi awal normal
  attachInterrupt(digitalPinToInterrupt(GM_PIN), impulse, FALLING);

  // Inisialisasi Modul LoRa Transceiver
  Serial.println("\n[ESP32 Indoor Monitor via LoRa]");
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("Error: Modul LoRa gagal diinisialisasi! Periksa kabel SPI.");
    while (true) delay(1000);
  }

  // Parameter LoRa untuk tembus beton / timbal ruang radiasi
  LoRa.setTxPower(20);          // Daya pancar maksimum (20 dBm)
  LoRa.setSpreadingFactor(10);  // SF10 tembus dinding tebal
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);

  Serial.println("Modul LoRa Indoor Siap Memancar!");
}

void loop() {
  // 1. Baca Sensor BMP280 (Suhu & Tekanan Udara)
  double suhu       = bmp.readTemperature();
  double tekananHpa = bmp.readPressure() / 100.0F;

  if (isnan(suhu)) suhu = 0.0;

  // 2. Kalkulasi Dosis GM Kit & Logika Proteksi Safety Relay
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > LOG_PERIOD) {
    previousMillis = currentMillis;
    doseRate = Sieverts(counts * multiplier);
    counts = 0;

    // Logika Alarm Fail-Safe
    if (doseRate > THRESHOLD_USV) {
      relayStatus = "OFF";          // Lepaskan relay (Bahaya)
      digitalWrite(RELAY_PIN, LOW); // Trigger sirene alarm
      Serial.printf("[ALARM BAHAYA] Laju dosis tinggi: %.2f uSv/h!\n", doseRate);
    } else {
      relayStatus = "ON";           // Kondisi aman normal
      digitalWrite(RELAY_PIN, HIGH);
    }
  }

  // 3. Kirim Paket Data ke Master Display via LoRa
  if ((millis() - lastSendTime) > SEND_INTERVAL) {
    lastSendTime = millis();

    // Susun payload JSON
    StaticJsonDocument<256> doc;
    doc["node"]     = "INDOOR";
    doc["seq"]      = seqNumber++;
    doc["cps"]      = counts;
    doc["usvh"]     = round2(doseRate);
    doc["suhu"]     = round2(suhu);
    doc["tekanan"]  = round2(tekananHpa);
    doc["relay"]    = relayStatus;

    String payload;
    serializeJson(doc, payload);

    // Pancarkan paket LoRa
    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();

    Serial.printf("[LORA TX INDOOR #%lu] Dosis: %.2f uSv/h | Suhu: %.1f C | Relay: %s -> Terkirim ke Master\n",
                  seqNumber - 1, doseRate, suhu, relayStatus.c_str());
  }
}
