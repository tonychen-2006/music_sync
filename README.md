# Music Sync

A synchronized music-video recording system that automatically logs and synchronizes GoPro video clips with Apple Music playback for seamless editing workflows.

## Overview

Music Sync is a two-part system that bridges Apple Music playback on iOS with GoPro camera recording via an ESP32 microcontroller. The system tracks song playback timing, controls GoPro recording, and generates Final Cut Pro-compatible XML timelines for synchronized video editing.

### System Architecture

```
┌─────────────────┐          ┌──────────────────┐          ┌─────────────────┐
│   iOS App       │   BLE    │   ESP32          │   WiFi   │   GoPro         │
│ (SpotifyBridge) │ ◄──────► │   Firmware       │ ◄──────► │   Camera        │
│                 │          │   (MusicSync)    │          │                 │
└─────────────────┘          └──────────────────┘          └─────────────────┘
     │                              │                              │
     │                              │                              │
     ▼                              ▼                              ▼
Apple Music              Event Log Storage               Video Clips
Playback State           + XML Export                    (MP4 files)
Time Sync                (LittleFS)                      
Metadata                                                 
```

## Features

### iOS App (SpotifyBridge)
- **Apple Music Integration**: Real-time playback state and position tracking
- **BLE Communication**: Sends song metadata, playback state, and timing updates to ESP32
- **Time Synchronization**: 150ms polling for accurate time sync
- **XML Export**: Retrieves and shares project XML files from ESP32
- **Live Status Display**: Shows BLE connection, playback state, and song info

### ESP32 Firmware (MusicSync)
- **BLE Server**: Nordic UART Service (NUS) for iOS communication
- **Event Logging**: Records song metadata and clip start/end events to LittleFS
- **GoPro Control**: WiFi-based HTTP API control for recording start/stop
- **Automatic Recording**: "Whole song mode" - records during playback, stops when paused
- **XML Generation**: Creates Final Cut Pro-compatible timeline XML
- **Chunk Transfer**: Splits large XML files into BLE-friendly chunks for transmission

### Integration Points
- **Song Tracking**: Logs URI, title, and duration when tracks change
- **Clip Synchronization**: Records exact song timestamps (ms) for clip start/end
- **Timeline Export**: Maps video clips to song timeline for post-production

## Project Structure

```
music_sync/
├── firmware/                    # ESP32 C++ firmware (PlatformIO)
│   ├── platformio.ini          # ESP32 build configuration
│   ├── include/                # Header files
│   │   ├── app_state.h         # Application state management
│   │   ├── event_log.h         # Event logging interface
│   │   ├── go_pro.h            # GoPro WiFi control
│   │   └── xml_export.h        # XML generation
│   └── src/                    # Source files
│       ├── main.cpp            # Main BLE server & command processing
│       ├── event_log.cpp       # LittleFS-based event logging
│       ├── go_pro.cpp          # GoPro HTTP API client
│       └── xml_export.cpp      # XML timeline generation
│
└── ios_app/                    # iOS Swift app (Xcode)
    └── SpotifyBridge/
        ├── SpotifyBridge.xcodeproj/
        └── SpotifyBridge/
            ├── SpotifyBridgeApp.swift      # App entry point
            ├── ContentView.swift           # Main UI
            ├── BLEBridge.swift             # BLE client (CoreBluetooth)
            ├── AppleMusicManager.swift     # Music playback tracking
            ├── SpotifyManager.swift        # Spotify SDK (unused/future)
            └── ShareSheet.swift            # XML file sharing
```

## Hardware Requirements

- **iOS Device**: iPhone with Bluetooth LE and Apple Music
- **ESP32 Board**: Any ESP32 dev board with WiFi and BLE
- **GoPro Camera**: WiFi-enabled model (tested with GoPro Hero series)
- **Power**: USB power for ESP32 (portable battery recommended)

## Software Requirements

### Firmware
- [PlatformIO](https://platformio.org/) - embedded development platform
- ESP32 Arduino framework
- Libraries:
  - NimBLE-Arduino (1.4.2+) - Bluetooth Low Energy
  - LittleFS - filesystem for event storage

### iOS App
- Xcode 15+
- iOS 16.0+
- Swift 5.9+
- Frameworks:
  - SwiftUI
  - CoreBluetooth (BLE communication)
  - MediaPlayer (Apple Music access)

## Setup Instructions

### 1. ESP32 Firmware Setup

```bash
cd firmware
# Install dependencies (done automatically by PlatformIO)
pio lib install

# Build firmware
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor -b 115200
```

**Configuration**: Before uploading, update GoPro WiFi credentials in your code if using automatic connection mode.

### 2. iOS App Setup

1. Open `ios_app/SpotifyBridge/SpotifyBridge.xcodeproj` in Xcode
2. Update the development team and bundle identifier
3. Build and run on your iOS device (Simulator won't work - needs real Bluetooth)

**Note**: Apple Music authorization will be requested on first launch.

### 3. GoPro Setup

1. Enable WiFi on your GoPro
2. Note the WiFi SSID and password (usually printed on camera screen)
3. Ensure GoPro is in video recording mode
4. The ESP32 will connect to GoPro WiFi when needed

## Usage Workflow

### Complete Recording Session

1. **Power On**: Turn on ESP32 and GoPro
2. **Connect BLE**: Open iOS app, tap "Start BLE" - wait for "connected"
3. **Authorize**: Tap "Auth Apple Music" (first time only)
4. **Start Sync**: Tap "Start Sync" to begin tracking
5. **Play Music**: Start playing a song in Apple Music
6. **Record**: ESP32 automatically starts GoPro recording when song plays
7. **Auto-Stop**: Recording stops when you pause or skip tracks
8. **Repeat**: Continue playing songs - each gets synchronized
9. **Export**: Tap "Export XML from ESP32" when done
10. **Share**: Tap "Share XML File" to export timeline for editing

### BLE Protocol

The iOS app sends these commands to ESP32:

| Command | Description | Example |
|---------|-------------|---------|
| `{time}` | Song position in milliseconds | `45230` |
| `muri={uri};title={title};dur={ms}` | Song metadata | `muri=apple:track:1234567890;title=Song Name;dur=240000` |
| `p1` / `p0` | Playback state (playing/paused) | `p1` |
| `x` | Export XML timeline | `x` |
| `c` | Clear event log | `c` |

The ESP32 sends back:

| Response | Description |
|----------|-------------|
| `XML_BEGIN {size}` | Start of XML transfer |
| `XML_CHUNK {seq} {data}` | XML chunk with sequence number |
| `XML_END {checksum}` | End of XML transfer |

## Output Format

### Event Log (`/events.log` on ESP32)

```
SONG uri="apple:track:1440933470" title="Mr. Brightside" durationMs=224000
CLIP_START file="GOPR0042.MP4" songMs=5230
CLIP_END file="GOPR0042.MP4" songMs=45100
```

### Project XML (`/project.xml` on ESP32)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Project name="Session1">
  <Song uri="apple:track:1440933470" title="Mr. Brightside" durationMs="224000"/>
  <Clip file="GOPR0042.MP4" startSongMs="5230" endSongMs="45100"/>
  <Clip file="GOPR0043.MP4" startSongMs="67500" endSongMs="156000"/>
</Project>
```

This XML format can be parsed by post-production tools to automatically synchronize video clips with the song timeline in Final Cut Pro or other editing software.

## Technical Details

### ESP32 Features

- **Nordic UART Service (NUS)**: Standard BLE profile for serial communication
- **LittleFS Storage**: Persistent flash storage for event logs and XML
- **Chunk Protocol**: Splits XML into 140-byte chunks for reliable BLE transmission
- **Command Parser**: Distinguishes between time updates (all digits) and text commands
- **State Machine**: Tracks "whole song" mode with automatic recording control

### iOS Features

- **150ms Polling**: Balances accuracy with battery life
- **State Change Detection**: Only sends updates when track or playback state changes
- **XML Reassembly**: Collects BLE chunks and reconstructs complete XML file
- **Share Sheet Integration**: Native iOS sharing for exporting XML

### GoPro Integration

- **WiFi HTTP API**: Standard GoPro control protocol
- **Shutter Control**: `/gp/gpControl/command/shutter?p={0|1}` endpoints
- **Connection**: ESP32 acts as WiFi client to GoPro access point (10.5.5.9)

## Development Status

**Current**: Prototype - Core functionality working

**Completed Features**:
- BLE communication between iOS and ESP32
- Apple Music playback tracking
- GoPro WiFi control
- Event logging with timestamps
- XML generation and export
- Automatic recording on playback

**Planned Enhancements**:
- Spotify SDK integration (SpotifyManager.swift ready but not active)
- Multiple song support in one session
- Advanced XML export formats (EDL, AAF)
- Camera status feedback to iOS app
- Battery monitoring and alerts