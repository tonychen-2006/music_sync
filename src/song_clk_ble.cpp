#include "song_clk_ble.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

// =======================
// Global song clock state
// =======================
static volatile uint32_t g_song_ms = 0;

void song_clock_set_time(uint32_t ms) {
  g_song_ms = ms;
}

uint32_t song_clock_get_time() {
  return (uint32_t)g_song_ms;
}

// =======================
// BLE UUIDs (keep stable)
// =======================
static const char* SERVICE_UUID   = "b2b7c7b6-77f0-4df0-9b2d-9f7c8e3a3b21";
static const char* TIME_CHAR_UUID = "c4b6bdb5-5b8b-4f62-8bbf-7f2d3f0b6d11";

// =======================
// BLE write handler
// =======================
class SongTimeCallbacks : public NimBLECharacteristicCallbacks {

private:
  void handleWrite(NimBLECharacteristic* ch) {
    std::string v = ch->getValue();

    Serial.print("[BLE] onWrite len=");
    Serial.println((int)v.size());

    Serial.print("[BLE] raw hex: ");
    for (size_t i = 0; i < v.size(); i++) {
      uint8_t b = (uint8_t)v[i];
      if (b < 16) Serial.print("0");
      Serial.print(b, HEX);
      Serial.print(" ");
    }
    Serial.println();

    Serial.print("[BLE] raw printable: '");
    for (size_t i = 0; i < v.size(); i++) {
      char c = v[i];
      if (c >= 32 && c <= 126) Serial.print(c);
      else Serial.print('.');
    }
    Serial.println("'");

    // =======================
    // Robust parsing strategy
    // =======================
    uint32_t ms = 0;
    bool sawDigit = false;

    // 1) Extract digits anywhere in payload (TEXT mode safe)
    for (size_t i = 0; i < v.size(); i++) {
      char c = v[i];
      if (c >= '0' && c <= '9') {
        sawDigit = true;
        ms = ms * 10 + (uint32_t)(c - '0');
      }
    }

    // 2) Fallback: exactly 4 bytes → little-endian uint32
    if (!sawDigit && v.size() == 4) {
      const uint8_t* b = (const uint8_t*)v.data();
      ms = (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
    }

    song_clock_set_time(ms);
    Serial.print("[BLE] songTimeMs = ");
    Serial.println(song_clock_get_time());
  }

public:
  // NimBLE-Arduino signature (variant A)
  void onWrite(NimBLECharacteristic* ch) {
    handleWrite(ch);
  }

  // NimBLE-Arduino signature (variant B)
  void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& connInfo) {
    (void)connInfo;
    handleWrite(ch);
  }
};

// =======================
// BLE init
// =======================
void song_clock_begin() {
  NimBLEDevice::init("SongSync-ESP32");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(64);

  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* service = server->createService(SERVICE_UUID);

  NimBLECharacteristic* timeChar =
      service->createCharacteristic(
        TIME_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
      );

  timeChar->setCallbacks(new SongTimeCallbacks());
  timeChar->setValue("0");

  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  Serial.println("[BLE] Advertising started (SongSync-ESP32)");
  Serial.print("[BLE] Service UUID: ");
  Serial.println(SERVICE_UUID);
  Serial.print("[BLE] Char UUID (song_time_ms): ");
  Serial.println(TIME_CHAR_UUID);
}