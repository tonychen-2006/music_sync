#pragma once
#include <Arduino.h>

bool export_xml_from_events(const String& eventsText);
String read_project_xml();  // helper for printing later