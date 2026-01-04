#include <WiFi.h>

#include "go_pro.h"

bool goproBegin(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

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

bool goproShutter(bool on) {
  char path[64];
  snprintf(path, sizeof(path),
           "/gp/gpControl/command/shutter?p=%d",
           on ? 1 : 0);

  char body[64];
  return httpGETtoBuf(path, body, sizeof(body));
}
