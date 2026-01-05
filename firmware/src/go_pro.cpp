#include <WiFi.h>

#include "go_pro.h"

/**
 * Connect to GoPro WiFi network and wait for connection.
 * @param ssid GoPro WiFi SSID (e.g., "GP26354747")
 * @param pass GoPro WiFi password
 * @return true if connected successfully, false if timeout (12 seconds)
 * @brief Initiates WiFi connection to GoPro with 12-second timeout.
 */
bool goproBegin(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

/**
 * Send HTTP GET request to GoPro and receive response body.
 * @param path HTTP path (e.g., "/gp/gpControl/command/shutter?p=1")
 * @param body Output buffer for response body
 * @param cap Size of body buffer
 * @return true if HTTP 200 received and body captured, false otherwise
 * @brief Sends HTTP request to 10.5.5.9:80 (GoPro API), skips headers, captures body.
 */
static bool httpGETtoBuf(const char* path, char* body, size_t cap) {
  WiFiClient client;
  client.setTimeout(4000);

  if (!client.connect("10.5.5.9", 80)) {
    return false;
  }

  client.print(String("GET ") + path + " HTTP/1.1\r\n"
               "Host: 10.5.5.9\r\n"
               "Connection: close\r\n\r\n"); // User reliant

  String s = client.readStringUntil('\n');
  s.trim();
  if (s.indexOf(" 200") < 0) {
    client.stop();
    return false;
  }

  // Skip headers
  while (client.connected()) {
    String h = client.readStringUntil('\n');
    if (h == "\r" || h.length() == 0) break;
  }

  size_t n = 0;
  while (client.connected() || client.available()) {
    while (client.available()) {
      char c = (char)client.read();
      if (n + 1 < cap) body[n++] = c;
    }
  }

  body[n] = '\0';
  client.stop();
  return true;
}

/**
 * Send shutter command to GoPro to start or stop recording.
 * @param on true to start recording, false to stop recording
 * @return true if HTTP request succeeded, false if connection/response failed
 * @brief Sends /gp/gpControl/command/shutter API call to GoPro.
 *        Logs debug info including HTTP response body.
 */
bool goproShutter(bool on) {
  Serial.printf("[GoPro] Sending shutter command: %s\n", on ? "START" : "STOP");
  
  char path[64];
  snprintf(path, sizeof(path),
           "/gp/gpControl/command/shutter?p=%d",
           on ? 1 : 0);

  char body[64];
  bool result = httpGETtoBuf(path, body, sizeof(body));
  
  if (result) {
    Serial.printf("[GoPro] HTTP response: %s\n", body);
  } else {
    Serial.println("[GoPro] HTTP request failed");
  }
  
  return result;
}
