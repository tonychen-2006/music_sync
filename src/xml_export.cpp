#include "xml_export.h"
#include <LittleFS.h>

static const char* XML_PATH = "/project.xml";

bool export_xml_from_events(const String& eventsText) {
  // Minimal parser: one SONG, and CLIP_START/CLIP_END pairs.
  String songUri = "", songTitle = "";
  uint32_t songDur = 0;

  struct Clip { 
    String file; 
    uint32_t a, b;
    Clip() : a(0), b(0) {}
    Clip(String f, uint32_t start, uint32_t end) : file(f), a(start), b(end) {}
  };
  Clip clips[32];
  int clipCount = 0;

  String curFile = "";
  uint32_t curStart = 0;

  int i = 0;
  while (i < (int)eventsText.length()) {
    int j = eventsText.indexOf('\n', i);
    if (j < 0) j = eventsText.length();
    String line = eventsText.substring(i, j);
    line.trim();
    i = j + 1;
    if (!line.length()) continue;

    if (line.startsWith("SONG ")) {
      int u = line.indexOf("uri=\"");
      int t = line.indexOf("title=\"");
      int d = line.indexOf("durationMs=");

      if (u >= 0) { int e = line.indexOf("\"", u+5); songUri = line.substring(u+5, e); }
      if (t >= 0) { int e = line.indexOf("\"", t+7); songTitle = line.substring(t+7, e); }
      if (d >= 0) { songDur = (uint32_t)line.substring(d+11).toInt(); }
    }

    if (line.startsWith("CLIP_START")) {
      int f = line.indexOf("file=\"");
      int s = line.indexOf("songMs=");
      if (f >= 0) { int e = line.indexOf("\"", f+6); curFile = line.substring(f+6, e); }
      if (s >= 0) { curStart = (uint32_t)line.substring(s+7).toInt(); }
    }

    if (line.startsWith("CLIP_END")) {
      int s = line.indexOf("songMs=");
      uint32_t endMs = (s >= 0) ? (uint32_t)line.substring(s+7).toInt() : 0;

      if (clipCount < 32 && curFile.length()) {
        clips[clipCount++] = Clip{curFile, curStart, endMs};
      }
      curFile = "";
      curStart = 0;
    }
  }

  File f = LittleFS.open(XML_PATH, "w");
  if (!f) return false;

  f.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  f.println("<Project name=\"Session1\">");
  f.print("  <Song uri=\""); f.print(songUri);
  f.print("\" title=\""); f.print(songTitle);
  f.print("\" durationMs=\""); f.print(songDur);
  f.println("\"/>");

  for (int k = 0; k < clipCount; k++) {
    f.print("  <Clip file=\""); f.print(clips[k].file);
    f.print("\" startSongMs=\""); f.print(clips[k].a);
    f.print("\" endSongMs=\""); f.print(clips[k].b);
    f.println("\"/>");
  }

  f.println("</Project>");
  f.close();
  return true;
}

String read_project_xml() {
  File f = LittleFS.open(XML_PATH, "r");
  if (!f) return "";
  String out;
  while (f.available()) out += char(f.read());
  f.close();
  return out;
}
