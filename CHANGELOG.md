# Changelog

All notable changes to ESP32 CYD MiniTV are documented here.

## 1.0.0 - 2026-09-16

First stable release of the Wi-Fi-managed CYD edition.

### Added

- Responsive local web interface with dedicated navigation pages
- First-run Wi-Fi setup portal and saved network configuration
- Playback, channel, volume, mute, brightness, restart, and sleep controls
- Reliable multi-file, chunked SD uploads with progress, retries, cancellation, and validation
- Channel creation, selection, reordering, rename, pair deletion, and folder deletion
- Settings backup and restore
- Optional administrator authentication with hardware recovery
- Browser-based OTA firmware updates
- Device naming, hostname settings, daily sleep scheduling, and countdown status
- Diagnostics dashboard, event log, downloadable report, SD health scan, and cleanup tools

### Changed

- Stabilized simultaneous video, MP3 audio, Wi-Fi, and SD access on the classic CYD
- Corrected CYD display profile, colors, frame drawing, onboard amplifier enable, and DAC routing
- Improved audio/video synchronization and volume range
- Switched Wi-Fi builds to prefer MP3 because it uses less memory than AAC/SBR
- Advanced to semantic versioning for public releases
