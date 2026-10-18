# HiPhi Dial

Custom shower Dial release [v1.3.0-shower.3](docs/operations/release-v1.3.0.md) is published and installed; exact CI flash/readback and operator-visible history are recorded in [installation evidence](docs/operations/shower-history.md). Physical acceptance and the documented BLE reconnect limits remain separate.

Companion Pi release: [v1.3.1](https://github.com/danikeuc/rpi-freez-protect/releases/tag/v1.3.1).

Planned extension: [independent light, UniFi webhook and manual reminder](docs/operations/independent-lighting.md). Documentation only; no light control is present in the installed release.

Custom firmware and a companion service that turn a [Waveshare ESP32-S3 Knob](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) into a dedicated hi-fi controller.

See what's playing, adjust volume, skip tracks, and switch zones—all from a physical knob on your desk.

> **Firmware maturity:** HiPhi Dial is **Beta** and has direct hardware-test
> coverage. HiPhi Frame is **Alpha**: builds are available for early testing,
> but its current shared-stack firmware has not completed equivalent hardware
> regression coverage.

**Now supports multiple music sources:**
- **Roon** - Full zone control with album artwork
- **Lyrion Music Server (LMS)** - Squeezebox/LMS player control
- **OpenHome/UPnP** - Control OpenHome-compatible renderers

[![HiPhi Dial demo](docs/images/roon-knob-photo.jpg)](https://photos.app.goo.gl/s5LpWqBTaXRihmjh7)
*Click to watch video demo*

## What You Need

1. **Hardware**: [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm) (~$50) - buy it at [amazon](https://amzn.to/4pYZdiC) to support my work.
2. **Music source**: Roon Core, Lyrion Music Server, or OpenHome renderer on your network
3. **Docker host** (NAS, Raspberry Pi, always-on computer) to run the control service

## Quick Start

> **New to this?** See the [Getting Started from Scratch](docs/usage/GETTING_STARTED.md) guide for detailed step-by-step instructions.

### 1. Flash the Firmware (one-time)

Use the [Web Flasher](https://roon-knob.muness.com/flash.html) in Chrome or Edge—no tools to install. Just plug in the knob via USB-C and click "Flash HiPhi Dial".

> **Prefer command line?** See [Firmware Flashing](docs/usage/FIRMWARE_FLASHING.md) for esptool instructions.

Stable firmware updates are available over Wi-Fi. Beta and Alpha prereleases
remain opt-in through the [Beta Web Flasher](https://roon-knob.muness.com/beta/flash.html)
and are never pushed through the stable OTA feed.

### 2. Run the Control Service

The control service (Unified Hi-Fi Control) connects your music source to the knob.

**Docker (recommended)**

On any Docker host (NAS, Raspberry Pi, etc.):

```yaml
# docker-compose.yml
services:
  unified-hifi-control:
    image: muness/unified-hifi-control:latest
    restart: unless-stopped
    network_mode: host
    volumes:
      - unified-hifi-control-data:/data

volumes:
  unified-hifi-control-data:
```

```bash
docker compose up -d
```

> **Note:** The legacy image name `muness/roon-extension-knob` still works and receives the same updates.

**Already have the [Roon Extension Manager](https://github.com/TheAppgineer/roon-extension-manager)?**

Find "Unified Hi-Fi Control" in the extension list and install it from there (Roon-only mode).

### 3. Authorize in Roon

Go to **Roon → Settings → Extensions** and enable **"Unified Hi-Fi Control"**.

### 4. Set Up the Knob

Power on the dial. It creates a WiFi network called **"hiphi-dial-setup"**. Connect to it, enter your WiFi credentials, and you're done.

The knob finds the extension automatically via mDNS.

## Controls

| Action | What it does |
|--------|--------------|
| **Turn the knob** | Volume up/down |
| **Press the knob** | Open zone picker |
| **Tap the screen** | Play/pause |
| **Swipe up** | Art mode (hide controls, show album art) |
| **Swipe down** | Exit art mode |
| **Long-press zone name** | Settings |

## Features

- Real-time now playing with album artwork
- Velocity-sensitive volume control (turn slow for fine adjustment, fast for quick jumps)
- Multi-zone support
- Automatic display dimming and sleep
- Over-the-air firmware updates
- WiFi setup via captive portal

## Troubleshooting

| Display shows | Meaning |
|---------------|---------|
| "WiFi: Setup Mode" | Connect to "hiphi-dial-setup" network to configure WiFi |
| "Extension: Searching..." | Looking for the extension—make sure it's running |
| "Extension: Connected" | Ready to use |

If mDNS doesn't work on your network, enter the extension URL manually in Settings (long-press zone name).

## Development

See [DEVELOPMENT.md](docs/dev/DEVELOPMENT.md) for building firmware, running the PC simulator, and contributing.

## Roadmap

See [PROJECT_AIMS.md](docs/meta/PROJECT_AIMS.md) for project goals and decision framework, and [ROADMAP_IDEAS.md](docs/meta/ROADMAP_IDEAS.md) for user feedback and planned improvements.

## Support

Questions or issues? [Open an issue](https://github.com/muness/roon-knob/issues), join the [Roon Community discussion](https://community.roonlabs.com/t/50-diy-roon-desk-controller/311363), or [buy me a coffee](https://www.buymeacoffee.com/muness).

## License

This project is licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE).

Versions up to and including v2.2.2 were released under the [MIT License](LICENSE-MIT).

For commercial licensing inquiries, see [COMMERCIAL-LICENSE.md](COMMERCIAL-LICENSE.md).

## Acknowledgments

Thanks to **gTunes** from the Roon community for alpha testing, detailed feedback, and help with the velocity-sensitive volume control implementation.

## Independent lighting in the custom Dial firmware

The Waveshare ESP32-S3 custom build supports a third **Luč** page after a configured controller returns the `independent_light_v1` capability. Horizontal swipes navigate Roon, Tuš and Luč in both directions; an older or unconfigured Pi retains the two existing pages. Light actions use only `/api/v1/lights`, independent of shower and Roon credentials. The separate light worker polls with bounded requests and never automatically retries an action. Config, Wi-Fi and admin session fences cancel expired or obsolete work before it sends a body and discard late replies.

PIN admin includes a **Luč** section for the separate base URL and distinct display/settings tokens, confirmed status, timed ON/OFF in AUTO, explicit manual ON/OFF and return to AUTO, whole minute durations (1–120), and history. Tokens are write only, stored together in the independent `light_private` NVS namespace, and never returned to the browser. Saving configuration/durations and reading history do not energize the light. Blank browser token inputs preserve the current connection; a new URL requires both credentials again. These credentials must be provisioned on the separate Pi controller/gateway first.

Manual mode displays **ROČNO — kamera začasno izključena**, a countdown and **Nazaj na AUTO**. A small vector bulb and M remain on Roon and Tuš during manual mode; offline or elapsed local timers show unknown until a fresh controller response confirms AUTO. Every manual action renews the full configured override duration. GPIO readback describes the controller output, not lamp illumination.

Software verification: `IDF_PATH=<ESP-IDF-5.5.5> sh scripts/test_light_dial.sh`, the existing admin/valve/playlist/weather suites, browser tests in `tests/admin_dial`, and the exact Waveshare ESP32-S3 build. Physical flashing, lamp/wiring behavior, power/reset timing and the authenticated real Protect event profile remain commissioning gates. Dial communication loss/crash/reboot/power loss cannot extend a server deadline or replay a queued action; the Pi controller/executor owns OFF and lease enforcement. Executor hang, boot and power behavior require hardware evidence. The light never owns or changes shower GPIO26/20.
