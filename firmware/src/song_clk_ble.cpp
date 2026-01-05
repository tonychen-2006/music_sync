#include <Arduino.h>
#include <NimBLEDevice.h>

#include "song_clk_ble.h"

// Global song clock state
static volatile uint32_t g_song_ms = 0;

/**
 * Set the current song playback time.
 * @param ms Playback time in milliseconds
 * @brief Updates the global song clock state, typically called from BLE write handler.
 */
void song_clock_set_time(uint32_t ms) {
  g_song_ms = ms;
}

/**
 * Get the current song playback time.
 * @return Current playback time in milliseconds
 * @brief Thread-safe read of global song clock state.
 */
uint32_t song_clock_get_time() {
  return (uint32_t)g_song_ms;
}

// BLE UUIDs
static const char* SERVICE_UUID   = "b2b7c7b6-77f0-4df0-9b2d-9f7c8e3a3b21";
static const char* TIME_CHAR_UUID = "c4b6bdb5-5b8b-4f62-8bbf-7f2d3f0b6d11";

/**
 * BLE characteristic callbacks for song time updates.
 * @brief Handles incoming BLE writes with robust parsing for both text and binary formats.
 */
class SongTimeCallbacks : public NimBLECharacteristicCallbacks {

private:
  /**
   * Parse and process incoming song time data from BLE.
   * @param ch BLE characteristic that received the write
   * @brief Supports two formats:
   *        1. Text mode: extracts digits from ASCII string
   *        2. Binary mode: 4-byte little-endian uint32
   *        Logs raw data in hex and ASCII for debugging.
   */
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

    uint32_t ms = 0;
    bool sawDigit = false;

    // Extract digits anywhere in payload (TEXT mode safe)
    for (size_t i = 0; i < v.size(); i++) {
      char c = v[i];
      if (c >= '0' && c <= '9') {
        sawDigit = true;
        ms = ms * 10 + (uint32_t)(c - '0');
      }
    }

    // Fallback: exactly 4 bytes → little-endian uint32
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
  /**
   * NimBLE callback variant A (single parameter).
   * @param ch BLE characteristic that received the write
   * @brief Routes write events to handleWrite for processing.
   */
  void onWrite(NimBLECharacteristic* ch) {
    handleWrite(ch);
  }

  /**
   * NimBLE callback variant B (with connection info).
   * @param ch BLE characteristic that received the write
   * @param connInfo Connection information (unused)
   * @brief Routes write events to handleWrite for processing.
   */
  void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& connInfo) {
    (void)connInfo;
    handleWrite(ch);
  }
};

/**
 * Initialize BLE server for song clock synchronization.
 * @brief Sets up BLE device, service, and characteristic for receiving song time updates.
 *        Starts advertising as "SongSync-ESP32" with custom service UUID.
 *        Configures write characteristic for song playback time in milliseconds.
 */
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