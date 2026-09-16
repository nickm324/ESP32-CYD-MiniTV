#ifndef CONFIG_H
#define CONFIG_H

// --- Local Wi-Fi web control ---
// Enter the same Wi-Fi network used by your phone/computer.
#define WIFI_SSID "CHANGE_ME"
#define WIFI_PASSWORD "CHANGE_ME"
#define WIFI_HOSTNAME "minitv"
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_STARTUP_TEST_MS 0 // Set to 20000 for a web-only diagnostic window
#define PREFER_MP3_AUDIO 1 // MP3 uses substantially less CPU/RAM than AAC/SBR
#define MP3_ONLY_WITH_WIFI 1 // AAC/SBR aborts when Wi-Fi and the video buffers are active
#define WIFI_DIAGNOSTIC_AUDIO_ONLY 0 // Normal combined audio/video playback

// --- Hardware Profile Selection (Uncomment only one) ---
//#define BOARD_MINI_TV
#define BOARD_CYD
//#define BOARD_CYD_RETROTV
// ------------------------------------------------------

#ifdef BOARD_MINI_TV
  #define MULTI_BUTTON 21
  #define CS 5
  #define SCK 18
  #define MOSI 23
  #define MISO -1
  #define DC 27
  #define RST 33
  #define BLK 22
  #define I2S_MCLK -1
  #define I2S_SCLK 25
  #define I2S_LRCLK 26
  #define I2S_DOUT 32
  #define I2S_DIN -1
  #define SD_SCK 14
  #define SD_MOSI 15
  #define SD_MISO 4
  #define SD_CS 13

  // Core settings
  #define MAX_WIDTH 288
  #define DISPLAY_SPI_HOST VSPI
  #define SD_SPI_HOST HSPI
  #define VIDEO_FPS 30
  #define SCREEN_BRIGHTNESS 128 // 0-255
  #define AUDIO_VOLUME 100 // 0-200; values above 100 apply digital boost
  #define LAUNCHER_RESET 0 // 0 = Disabled, 1 = Retro-Go, 2 = CYD-Launcher
#endif

#ifdef BOARD_CYD
  #define MULTI_BUTTON 0 
  #define CS 15
  #define SCK 14
  #define MOSI 13
  #define MISO 12
  #define DC 2
  #define RST -1
  #define BLK 21
  #define USE_INTERNAL_DAC
  #define AUDIO_ENABLE_PIN 4 // Active-low enable for the onboard audio amplifier
  #define I2S_MCLK -1
  #define I2S_SCLK -1
  #define I2S_LRCLK -1
  #define I2S_DOUT 26 
  #define I2S_DIN -1
  #define SD_SCK 18
  #define SD_MOSI 23
  #define SD_MISO 19
  #define SD_CS 5

  // Display Settings (CYD ILI9341)
  #define SCREEN_WIDTH 320
  #define SCREEN_HEIGHT 240
  #define SCREEN_ROTATION 1 // 1 & 3 are both landscape, 0 & 2 if you want portrait
  #define SCREEN_IPS false 
  #define SCREEN_INVERT false // Likely true for CYD2USB
  #define SCREEN_OFFSET_X 0
  #define SCREEN_OFFSET_Y 0
  #define MAX_WIDTH 320
  #define DISPLAY_SPI_HOST HSPI
  #define SD_SPI_HOST VSPI
  #define VIDEO_FPS 24 // Must match the converter's 320x240 output for A/V sync
  #define VIDEO_DRAW_SKIP_INTERVAL 1 // Draw every frame
  #define DIAGNOSTIC_DISABLE_AUDIO 0 // Normal audio playback enabled
  #define SCREEN_BRIGHTNESS 96 // Reduced peak-load diagnostic setting
  #define AUDIO_VOLUME 100 // 0-200; values above 100 apply digital boost
  #define LAUNCHER_RESET 2 // 0 = Disabled, 1 = Retro-Go, 2 = CYD-Launcher
#endif

#ifdef BOARD_CYD_RETROTV
  #define MULTI_BUTTON 3 
  #define CS 15
  #define SCK 14
  #define MOSI 13
  #define MISO 12
  #define DC 2
  #define RST -1
  #define BLK 21
  #define USE_INTERNAL_DAC
  #define AUDIO_ENABLE_PIN 4 // Active-low enable for the onboard audio amplifier
  #define I2S_MCLK -1
  #define I2S_SCLK -1
  #define I2S_LRCLK -1
  #define I2S_DOUT 26 
  #define I2S_DIN -1
  #define SD_SCK 18
  #define SD_MOSI 23
  #define SD_MISO 19
  #define SD_CS 5

  // Display Settings (CYD ILI9341)
  #define SCREEN_WIDTH 320
  #define SCREEN_HEIGHT 240
  #define SCREEN_ROTATION 1 // 1 & 3 are both landscape, 0 & 2 if you want portrait
  #define SCREEN_IPS false 
  #define SCREEN_INVERT true // Likely true for CYD2USB
  #define SCREEN_OFFSET_X 0
  #define SCREEN_OFFSET_Y 0
  #define MAX_WIDTH 320
  #define DISPLAY_SPI_HOST HSPI
  #define SD_SPI_HOST VSPI
  #define VIDEO_FPS 24 // CYD 320x240 limit is around 24fps else audio loses sync
  #define SCREEN_BRIGHTNESS 128 // 0-255
  #define AUDIO_VOLUME 100 // 0-200; values above 100 apply digital boost
  #define LAUNCHER_RESET 2 // 0 = Disabled, 1 = Retro-Go, 2 = CYD-Launcher
#endif

#endif
