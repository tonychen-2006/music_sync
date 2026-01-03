#include <Arduino.h>
#include "event_log.h"
#include "xml_export.h"
#include "song_clk_ble.h"

static String activeFile = "";

static void print_help() {
  Serial.println("\nCommands:");
  Serial.println("  s                 -> log mock song");
  Serial.println("  t<number>          -> set song time ms manually (e.g. t62351)  [BLE overwrites too]");
  Serial.println("  a<filename>         -> clip start (e.g. aGOPR0001.MP4)");
  Serial.println("  b                  -> clip end");
  Serial.println("  r                  -> print events.log");
  Serial.println("  x                  -> export + print XML");
  Serial.println("  c                  -> clear events.log");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!event_log_begin()) {
    Serial.println("event_log_begin() failed");
  }

  song_clock_begin();   // BLE advertising starts here

  Serial.println("ESP32 ready.");
  print_help();
}

void loop() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "s") {
    log_song("spotify:track:TEST", "Test Song", 210000);
    Serial.println("Song logged.");
  }
  else if (cmd.startsWith("t")) {
    uint32_t ms = (uint32_t)cmd.substring(1).toInt();
    song_clock_set_time(ms);
    Serial.print("songTimeMs = ");
    Serial.println(song_clock_get_time());
  }
  else if (cmd.startsWith("a")) {
    activeFile = cmd.substring(1);
    activeFile.trim();
    if (activeFile.length() == 0) {
      Serial.println("Provide a filename: aGOPR0001.MP4");
    } else {
      log_clip_start(activeFile, song_clock_get_time());
      Serial.println("CLIP START logged.");
    }
  }
  else if (cmd == "b") {
    if (activeFile.length() == 0) {
      Serial.println("No active clip. Use a<filename> first.");
    } else {
      log_clip_end(activeFile, song_clock_get_time());
      Serial.println("CLIP END logged.");
      activeFile = "";
    }
  }
  else if (cmd == "r") {
    Serial.println("\n--- events.log ---");
    Serial.println(read_events());
  }
  else if (cmd == "x") {
    export_xml_from_events(read_events());
    Serial.println("\n--- project.xml ---");
    Serial.println(read_project_xml());
  }
  else if (cmd == "c") {
    clear_events();
    activeFile = "";
    Serial.println("events.log cleared.");
  }
  else {
    print_help();
  }
}
