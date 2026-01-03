#include "song_clk_ble.h"

#include <NimBLEDevice.h>

static volatile uint32_t g_song_ms = 0;

// Pick fixed UUIDs so your iOS app can find them later.
// (These are random UUIDs; keep them consistent once you start using them.)
static const char* SERVICE_UUID   = "b2b7c7b6-77f0-4df0-9b2d-9f7c8e3a3b21";
static const char* TIME_CHAR_UUID = "c4b6bdb5-5b8b-4f62-8bbf-7f2d3f0b6d11";

void song_clock_set_time(uint32_t ms) {
  g_song_ms = ms;
}

uint32_t song_clock_get_time() {
  return (uint32_t)g_song_ms;
}

class SongTimeCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    std::string v = pCharacteristic->getValue();
    if (v.empty()) return;

    uint32_t ms = 0;

    // Support BOTH:
    // 1) ASCII text like "12345" (easy with nRF Connect)
    // 2) 4-byte little-endian uint32 (more efficient later)
    if (v.size() == 4) {
      const uint8_t* b = (const uint8_t*)v.data();
      ms = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    } else {
      // ASCII parse (trim spaces/newlines)
      while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ' || v.back() == '\t')) v.pop_back();
      size_t i = 0;
      while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) i++;
      ms = (uint32_t)strtoul(v.c_str() + i, nullptr, 10);
    }

    song_clock_set_time(ms);
    Serial.print("[BLE] songTimeMs = ");
    Serial.println(song_clock_get_time());
  }
};

void song_clock_begin() {
  NimBLEDevice::init("SongSync-ESP32"); // shows up as this name in nRF Connect
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // strong but reasonable
  NimBLEDevice::setMTU(64);

  NimBLEServer* server = NimBLEDevice::createServer();

  NimBLEService* service = server->createService(SERVICE_UUID);

  NimBLECharacteristic* timeChar =
      service->createCharacteristic(TIME_CHAR_UUID,
                                    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);

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