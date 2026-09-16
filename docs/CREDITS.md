# Credits and project history

ESP32 CYD MiniTV is a continuation of several open-source miniature television projects. The repository keeps this history visible so that the people and libraries that made the project possible receive clear credit.

## Upstream projects

1. **moononournation** created the original Mini TV code and maintains [Arduino_GFX](https://github.com/moononournation/Arduino_GFX), the display library used by this firmware.
2. **Eric N. / ThatProject** created the [Mini Lego TV](https://youtu.be/2TOVohmUqOE) and documented the related [Mini Retro TV](https://www.instructables.com/Mini-Retro-TV/) project.
3. **DynaMight1124** created [ESP32-MiniTV-Player](https://github.com/DynaMight1124/ESP32-MiniTV-Player), adding button control, dynamic channels, random playback, CYD support, and multi-device configuration. That repository was the direct starting point for this edition.

## Libraries

- [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) by moononournation
- [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix) by Phil Schatzmann
- [JPEGDEC](https://github.com/bitbank2/JPEGDEC) by Larry Bank / bitbank2
- Espressif Arduino core for ESP32

## This Wi-Fi edition

This edition was developed through hands-on testing on an ESP32-2432S028 CYD. It adds the browser-based control panel, resilient SD uploads, channel and file management, settings persistence, Wi-Fi provisioning, administrator protection, OTA firmware updates, sleep controls, backup/restore, diagnostics, SD health tools, CYD audio fixes, and playback stability work.

The product photograph in `assets/minitv-hero.png` depicts the tested physical build and is included for this repository's documentation.

## License continuity

The inherited project is dedicated to the public domain under The Unlicense. This repository retains that license. See the root [LICENSE](../LICENSE) file.
