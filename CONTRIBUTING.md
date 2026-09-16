# Contributing

Thanks for helping improve ESP32 CYD MiniTV.

## Reporting a problem

Please include:

- Firmware version
- Exact board model and selected profile
- ESP32 boards package version
- Arduino_GFX, JPEGDEC, and arduino-libhelix versions
- Whether the media is 320×240 at 24 FPS with MP3 audio
- Relevant Serial Monitor output
- A diagnostic report from the web interface, after removing any information you consider private

Do not upload copyrighted media, Wi-Fi passwords, or private network configuration.

## Pull requests

1. Keep Arduino source files together in the `MiniTV` folder.
2. Preserve compatibility with ESP32 core 2.0.17 unless the change explicitly migrates and tests the complete project.
3. Test video, audio, Wi-Fi controls, SD access, and OTA behavior on hardware when the change touches shared resources.
4. Update the README and changelog for user-visible behavior.
5. Keep upstream credit and The Unlicense intact.
