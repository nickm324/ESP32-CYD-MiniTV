/*
  ESP32 Mini TV Player - See config.h for device config settings
*/

/*
 * Built for the ESP32 Arduino 2.x API. ESP32 core 2.0.17 is recommended.
 * Required libraries:
 * https://github.com/moononournation/Arduino_GFX.git (Used v1.6.0)
 * https://github.com/pschatzmann/arduino-libhelix/releases/tag/v0.8.1 (Used 0.8.1)
 * https://github.com/bitbank2/JPEGDEC.git (Used 1.8.4)
 * https://github.com/vshymanskyy/Preferences (Used v2.2.2)
 */


#define AUDIOASSIGNCORE 1
#define DECODEASSIGNCORE 0
#define DRAWASSIGNCORE 1

#include "config.h"

// Leave enough internal RAM for the Helix audio decoder on non-PSRAM CYD boards.
// The MJPEG reader has an explicit overflow guard in mjpeg_decode_draw_task.h.
#define MJPEG_BUFFER_SIZE (32 * 1024)

#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_sleep.h>

Preferences preferences;
#define APP_NAME "video_player"
#define MINITV_VERSION "1.0.0"
#define K_VIDEO_INDEX "video_index"
#define BASE_PATH "/Videos/"
static int video_count = 0;
static bool has_random = false;
static volatile bool web_stop_requested = false;
static volatile bool playback_active = false;
static volatile bool playback_paused = false;
// Must outlive setup(): the web task continues using the SD card after
// playback is stopped for uploads and deletions.
static SPIClass sdSpi(SD_SPI_HOST);

/* Arduino_GFX */
#include <Arduino_GFX_Library.h>
Arduino_DataBus *bus = new Arduino_ESP32SPI(DC, CS, SCK, MOSI, MISO, DISPLAY_SPI_HOST);

#ifdef BOARD_MINI_TV
Arduino_GFX *gfx = new Arduino_ST7789(bus, RST, 1, true, ST7789_TFTWIDTH, ST7789_TFTHEIGHT, 0, 20, 0, 20);
#else
// CYD or other boards using ILI9341
Arduino_GFX *gfx = new Arduino_ILI9341(bus, RST, SCREEN_ROTATION, SCREEN_IPS);
#endif

/* Audio */
#include "esp32_audio_task.h"

/* MJPEG Video */
#include "mjpeg_decode_draw_task.h"

/* Variables */
#define K_MUTE "is_muted"
static bool is_showing_message = false;
#if defined(LAUNCHER_RESET) && (LAUNCHER_RESET > 0)
static volatile bool request_launcher_reset = false;
void resetToLauncher();
#endif
static int next_frame = 0;
static int skipped_frames = 0;
static unsigned long start_ms, curr_ms, next_frame_ms;
static int video_idx = 1;
static int screen_brightness = SCREEN_BRIGHTNESS;
static String current_video_title = "None";
static volatile uint32_t sleep_deadline_ms = 0;
static volatile bool screen_sleeping = false;

#include "web_control.h"

// Forward declarations
void buttonTask(void *parameter);
void playVideoWithAudio(int channel);
void videoController(int next);

// Direct hardware check for the CYD DAC2 (GPIO 26), onboard amplifier,
// speaker connector, and speaker. This bypasses I2S and media decoding.
static void playHardwareTestTone() {
#ifdef USE_INTERNAL_DAC
  const uint32_t started = millis();
  while (millis() - started < 1000) {
    dacWrite(26, 220);
    delayMicroseconds(625);
    dacWrite(26, 36);
    delayMicroseconds(625);
  }
  dacWrite(26, 128);
#endif
}

// pixel drawing callback
static int drawMCU(JPEGDRAW *pDraw) {
  if (is_showing_message) return 1;
  unsigned long s = millis();
#ifdef BOARD_MINI_TV
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
#else
  gfx->draw16bitRGBBitmap(pDraw->x + SCREEN_OFFSET_X, pDraw->y + SCREEN_OFFSET_Y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
#endif
  total_show_video_ms += millis() - s;
  return 1;
} /* drawMCU() */

void setup() {
  disableCore0WDT();

  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);
  Serial.printf("MiniTV firmware %s\n", MINITV_VERSION);
  logEvent("Booted firmware %s, reset reason %d", MINITV_VERSION, (int)esp_reset_reason());

  // Init Display
  // 40 MHz is stable on this CYD and avoids visibly slow scene redraws.
  gfx->begin(40000000);
  gfx->fillScreen(BLACK);
#ifdef SCREEN_INVERT
  gfx->invertDisplay(SCREEN_INVERT);
#endif
  
  // Brightness, see config.h for control settings
  ledcSetup(0, 5000, 8); // Channel 0, 5KHz, 8-bit resolution
  ledcAttachPin(BLK, 0);
  ledcWrite(0, SCREEN_BRIGHTNESS);

  pinMode(MULTI_BUTTON, INPUT_PULLUP);
  // Recovery: holding the board button for three seconds during power-on clears
  // a forgotten web administrator password without altering media or settings.
  if (digitalRead(MULTI_BUTTON) == LOW) {
    Serial.println("Hold button for 3 seconds to clear the web administrator password...");
    delay(3000);
    if (digitalRead(MULTI_BUTTON) == LOW) {
      preferences.begin(APP_NAME, false);
      preferences.remove("admin_pass");
      preferences.end();
      Serial.println("Web administrator password cleared");
      gfx->setCursor(8, 8);
      gfx->setTextColor(WHITE);
      gfx->println("Web password cleared");
      delay(1000);
      gfx->fillScreen(BLACK);
    }
  }

#ifdef AUDIO_ENABLE_PIN
  pinMode(AUDIO_ENABLE_PIN, OUTPUT);
  digitalWrite(AUDIO_ENABLE_PIN, LOW); // Enable the CYD onboard amplifier
  delay(10);
#endif

  xTaskCreate(
    buttonTask,
    "buttonTask",
    2000,
    NULL,
    1,
    NULL);

  Serial.println("Init I2S");

  esp_err_t ret_val = i2s_init(I2S_NUM_0, 44100, I2S_MCLK, I2S_SCLK, I2S_LRCLK, I2S_DOUT, I2S_DIN);
  if (ret_val != ESP_OK) {
    Serial.printf("i2s_init failed: %d\n", ret_val);
    gfx->println("i2s_init failed");
    return;
  }
  Serial.println("Init FS");

  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  // Give SD card time to stabilise after reboot
  delay(500); 
  
  bool mounted = false;
  for(int i = 0; i < 5; i++) {
    // A lower SPI clock is substantially more reliable for sustained writes
    // on inexpensive microSD cards while remaining fast enough for MJPEG.
    if (SD.begin(SD_CS, sdSpi, 10000000)) {
      mounted = true;
      break;
    }
    Serial.printf("Mount failed, retry %d/5...\n", i + 1);
    delay(200);
  }

  if (!mounted) {
    Serial.println("ERROR: File system mount failed!");
    gfx->println("ERROR: File system mount failed!");
    return;
  }

  uint64_t sdTotal = SD.totalBytes();
  uint64_t sdUsed = SD.usedBytes();
  Serial.printf("SD card: %u MB total, %u MB used, %u MB free\n",
                (unsigned)(sdTotal / 1048576ULL),
                (unsigned)(sdUsed / 1048576ULL),
                (unsigned)((sdTotal > sdUsed ? sdTotal - sdUsed : 0) / 1048576ULL));

  // Dynamic channel count: Check folders 1, 2, 3... in /Videos/
  has_random = SD.exists("/Videos/random");
  video_count = 0;
  while (true) {
    char path[32];
    sprintf(path, "%s%d", BASE_PATH, video_count + 1);
    if (SD.exists(path)) {
      video_count++;
    } else {
      break;
    }
  }
  Serial.printf("Found %d channels, Random: %d\n", video_count, has_random);
  if (video_count == 0 && !has_random) {
    gfx->println("No videos found!");
    playback_paused = true;
  }

  preferences.begin(APP_NAME, false);
  video_idx = preferences.getInt(K_VIDEO_INDEX, has_random ? 0 : 1);

  // Sanity checks for video_idx
  if (video_idx == 0 && !has_random) video_idx = 1;
  if (video_idx > video_count) video_idx = has_random ? 0 : 1;
  if (video_idx < 0) video_idx = has_random ? 0 : video_count;

  is_muted = preferences.getBool(K_MUTE, false);
  audio_volume = constrain(preferences.getInt("volume", AUDIO_VOLUME), 0, 200);
  screen_brightness = constrain(preferences.getInt("brightness", SCREEN_BRIGHTNESS), 0, 255);
  preferences.end();
  ledcWrite(0, screen_brightness);
  Serial.printf("videoIndex: %d, muted: %d, volume: %d, brightness: %d\n",
                video_idx, (int)is_muted, audio_volume, screen_brightness);

  startWebControl();
  Serial.printf("Free heap after Wi-Fi setup: %u bytes\n", ESP.getFreeHeap());

#if defined(WIFI_STARTUP_TEST_MS) && (WIFI_STARTUP_TEST_MS > 0)
  Serial.printf("Wi-Fi diagnostic window: open the web page now; playback starts in %u seconds\n",
                WIFI_STARTUP_TEST_MS / 1000);
  gfx->fillScreen(BLACK);
  gfx->setCursor(12, 75);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(2);
  gfx->printf("WEB TEST\n%s", WiFi.localIP().toString().c_str());
  delay(WIFI_STARTUP_TEST_MS);
  Serial.println("Wi-Fi diagnostic window ended; starting playback now");
#endif

  if (!setupModeActive) {
    gfx->setCursor(20, 20);
    gfx->setTextColor(GREEN);
    gfx->setTextSize(3, 3, 0);
    if (video_idx == 0) {
      gfx->printf("CH RANDOM");
    } else {
      gfx->printf("CH %d", video_idx);
    }
    delay(1000);
  }

  // Use hardware RNG for seeding
  randomSeed(esp_random());
  // loop() owns playback so videos/channels can change without rebooting.
}

void loop() {
  if (!playback_active && !playback_paused && !restartAfterResponse) {
    playVideoWithAudio(video_idx);
  }
#if defined(LAUNCHER_RESET) && (LAUNCHER_RESET > 0)
  if (request_launcher_reset) {
    resetToLauncher();
  }
#endif
  vTaskDelay(pdMS_TO_TICKS(2));
}

void buttonTask(void *parameter) {
  unsigned long last_release_time = 0;
  int click_count = 0;
  bool last_state = HIGH;
  unsigned long press_start_time = 0;
  bool long_press_cue_shown = false;
  bool reset_press_handled = false;

  while (1) {
    if (setupModeActive) {
      last_state = digitalRead(MULTI_BUTTON);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    bool current_state = digitalRead(MULTI_BUTTON);

    if (current_state == LOW && last_state == HIGH) {
      // Button Pressed
      press_start_time = millis();
      long_press_cue_shown = false;
      reset_press_handled = false;
    } else if (current_state == LOW && last_state == LOW) {
      // Button Held
      if (!long_press_cue_shown && (millis() - press_start_time >= 1000)) {
        // Visual cue at 1 second mark (Preview target mute state)
        long_press_cue_shown = true;
        is_showing_message = true;
        gfx->setCursor(20, 20);
        gfx->setTextColor(GREEN);
        gfx->setTextSize(3, 3, 0);
        gfx->printf("%s", is_muted ? "UNMUTING" : "MUTING");
      }

#if defined(LAUNCHER_RESET) && (LAUNCHER_RESET > 0)
      if (!reset_press_handled && (millis() - press_start_time >= 3000)) {
        // 3-Second Press detected (Reset to launcher / bootloader)
        Serial.println("3S_PRESS (Resetting to Launcher)");
        reset_press_handled = true;

        // Unpause video for 50ms so live video frame overdraws green text naturally
        is_showing_message = false;
        vTaskDelay(pdMS_TO_TICKS(50));

        resetToLauncher();
      }
#endif
    } else if (current_state == HIGH && last_state == LOW) {
      // Button Released
      if (!reset_press_handled) {
        if (long_press_cue_shown) {
          // Long Press (1s - 3s): Execute Mute Toggle on release
          Serial.println("LONG_PRESS (Mute Toggle)");
          is_muted = !is_muted;
          
          preferences.begin(APP_NAME, false);
          preferences.putBool(K_MUTE, is_muted);
          preferences.end();
          
          vTaskDelay(pdMS_TO_TICKS(500));
          is_showing_message = false;
        } else {
          // Short Press (< 1s): Register Click
          is_showing_message = false;
          unsigned long now = millis();
          if (now - last_release_time > 300) {
            click_count = 1;
          } else {
            click_count++;
          }
          last_release_time = now;
        }
      } else {
        is_showing_message = false;
      }
    }

    if (click_count > 0 && (millis() - last_release_time > 300)) {
      if (click_count == 1) {
        Serial.println("SINGLE_CLICK (Next)");
        videoController(1);
      } else if (click_count >= 2) {
        Serial.println("DOUBLE_CLICK (Back)");
        videoController(-1);
      }
      click_count = 0;
    }

    last_state = current_state;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void playVideoWithAudio(int channel) {
  current_video_title = "Loading...";
  web_stop_requested = false;
  playback_active = true;
  char dirPath[64];
  if (channel == 0) {
    strcpy(dirPath, "/Videos/random");
  } else {
    sprintf(dirPath, "%s%d", BASE_PATH, channel);
  }

  File root = SD.open(dirPath);
  if (!root || !root.isDirectory()) {
    Serial.printf("Directory %s not found\n", dirPath);
    videoController(1);
    playback_active = false;
    return;
  }

  // Select target video
  int mjpeg_count = 0;
  String found_base = "";
  File f = root.openNextFile();
  
  if (channel == 0) {
    // Single-pass random selection (Reservoir Sampling) to handle large folders quickly
    while (f) {
      const char* name = f.name();
      int len = strlen(name);
      if (len > 6 && strcmp(name + len - 6, ".mjpeg") == 0) {
        mjpeg_count++;
        if (esp_random() % mjpeg_count == 0) {
          found_base = String(name);
        }
      }
      f.close();
      f = root.openNextFile();
    }
  } else {
    // Numbered channels: just take the first .mjpeg found
    while (f) {
      String name = String(f.name());
      if (name.endsWith(".mjpeg")) {
        found_base = name;
        f.close();
        break;
      }
      f.close();
      f = root.openNextFile();
    }
  }
  root.close();

  if (found_base == "") {
    videoController(1);
    playback_active = false;
    return;
  }

  // Clean up the found filename
  int lastSlash = found_base.lastIndexOf('/');
  if (lastSlash != -1) found_base = found_base.substring(lastSlash + 1);
  found_base = found_base.substring(0, found_base.lastIndexOf(".mjpeg"));
  current_video_title = found_base;

  // Find matching audio
  String vPath = String(dirPath) + "/" + found_base + ".mjpeg";
  String aPath;
  bool is_mp3;
  bool has_audio = true;

#if defined(PREFER_MP3_AUDIO) && PREFER_MP3_AUDIO
  aPath = String(dirPath) + "/" + found_base + ".mp3";
  is_mp3 = true;
  if (!SD.exists(aPath.c_str())) {
#if !defined(MP3_ONLY_WITH_WIFI) || !MP3_ONLY_WITH_WIFI
    aPath = String(dirPath) + "/" + found_base + ".aac";
    is_mp3 = false;
#endif
#else
  aPath = String(dirPath) + "/" + found_base + ".aac";
  is_mp3 = false;
  if (!SD.exists(aPath.c_str())) {
    aPath = String(dirPath) + "/" + found_base + ".mp3";
    is_mp3 = true;
#endif
    if (!SD.exists(aPath.c_str())) {
      if (channel != 0) {
        // For numbered channels, fallback to ANY audio file in the folder
        Serial.println("Matching audio not found in numbered channel, searching for fallback...");
        File searchRoot = SD.open(dirPath);
        File audioFile = searchRoot.openNextFile();
        bool found_any = false;
        while (audioFile) {
          String aName = String(audioFile.name());
          if (
#if defined(MP3_ONLY_WITH_WIFI) && MP3_ONLY_WITH_WIFI
              false
#else
              aName.endsWith(".aac")
#endif
          ) {
            aPath = (aName.startsWith("/") ? "" : String(dirPath) + "/") + aName;
            is_mp3 = false;
            found_any = true;
            audioFile.close();
            break;
          } else if (aName.endsWith(".mp3")) {
            aPath = (aName.startsWith("/") ? "" : String(dirPath) + "/") + aName;
            is_mp3 = true;
            found_any = true;
            audioFile.close();
            break;
          }
          audioFile.close();
          audioFile = searchRoot.openNextFile();
        }
        searchRoot.close();
        if (!found_any) {
          has_audio = false;
          Serial.println("No audio files found at all, playing silent.");
        }
      } else {
        Serial.printf("No matching audio for %s in random, playing silent.\n", vPath.c_str());
        has_audio = false;
      }
    }
  }

#if defined(DIAGNOSTIC_DISABLE_AUDIO) && DIAGNOSTIC_DISABLE_AUDIO
  has_audio = false;
  Serial.println("Diagnostic mode: audio playback disabled");
#endif

  File vFile = SD.open(vPath.c_str());
  if (!vFile || vFile.isDirectory()) {
    Serial.println("ERROR: Failed to open video file");
    videoController(1);
    playback_active = false;
    return;
  }

  File aFile;
  if (has_audio) {
    aFile = SD.open(aPath.c_str());
    if (!aFile || aFile.isDirectory()) {
      has_audio = false;
      Serial.println("ERROR: Failed to open audio file, playing silent.");
    }
  }

  if (has_audio) {
    Serial.printf("Playing Video: %s, Audio: %s (%s)\n", vPath.c_str(), aPath.c_str(), is_mp3 ? "MP3" : "AAC");
    logEvent("Playing channel %d: %s", channel, current_video_title.c_str());
  } else {
    Serial.printf("Playing Video: %s (No Audio)\n", vPath.c_str());
    logEvent("Playing channel %d without audio: %s", channel, current_video_title.c_str());
  }

  mjpeg_setup(&vFile, MJPEG_BUFFER_SIZE, drawMCU, false, DECODEASSIGNCORE, DRAWASSIGNCORE);

  if (has_audio) {
    BaseType_t ret;
    if (is_mp3) {
      ret = mp3_player_task_start(&aFile, AUDIOASSIGNCORE);
    } else {
      ret = aac_player_task_start(&aFile, AUDIOASSIGNCORE);
    }

    if (ret != pdPASS) {
      Serial.printf("Audio player task start failed: %d\n", ret);
    }
  }

#if defined(WIFI_DIAGNOSTIC_AUDIO_ONLY) && WIFI_DIAGNOSTIC_AUDIO_ONLY
  Serial.println("AUDIO-ONLY DIAGNOSTIC: MJPEG decoding and display drawing are disabled");
  gfx->fillScreen(BLACK);
  gfx->setCursor(20, 95);
  gfx->setTextColor(GREEN);
  gfx->setTextSize(2);
  gfx->println("AUDIO + WIFI TEST");
  vFile.close();
  vTaskDelay(pdMS_TO_TICKS(50));
  while (audio_task_running && !web_stop_requested) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  audio_stop_requested = true;
  if (!web_stop_requested && audio_task_handle != NULL) {
    vTaskDelete(audio_task_handle);
    audio_task_handle = NULL;
    audio_task_running = false;
  }
  if (has_audio) aFile.close();
  playback_active = false;
  Serial.println("Audio-only diagnostic finished; web control remains active");
  return;
#endif

  Serial.println("Start play video");

  next_frame = 0;
  skipped_frames = 0;
  start_ms = millis();
  curr_ms = millis();
  while (!web_stop_requested && vFile.available() && mjpeg_read_frame())  // Read video
  {
#if defined(LAUNCHER_RESET) && (LAUNCHER_RESET > 0)
    if (request_launcher_reset) {
      vFile.close();
      if (has_audio) aFile.close();
      vTaskDelay(pdMS_TO_TICKS(100)); // Allow background audio task to terminate
      resetToLauncher();
    }
#endif

    total_read_video_ms += millis() - curr_ms;
    curr_ms = millis();

    next_frame_ms = start_ms + (++next_frame * 1000 / VIDEO_FPS);

    bool scheduledDraw = true;
#if defined(VIDEO_DRAW_SKIP_INTERVAL) && (VIDEO_DRAW_SKIP_INTERVAL > 1)
    scheduledDraw = (next_frame % VIDEO_DRAW_SKIP_INTERVAL) != 0;
#endif

    if (millis() < next_frame_ms && scheduledDraw)  // check show frame or skip frame
    {
      // Play video
      mjpeg_draw_frame();
      total_decode_video_ms += millis() - curr_ms;
      curr_ms = millis();
    } else {
      ++skipped_frames;
    }

    while (millis() < next_frame_ms) {
      if (next_frame_ms - millis() > 1) {
        vTaskDelay(pdMS_TO_TICKS(1));
      } else {
        yield();
      }
    }

    curr_ms = millis();
  }
  int time_used = millis() - start_ms;
  int total_frames = next_frame - 1;
  Serial.println("AV end");
  audio_stop_requested = true;
  uint32_t audioWaitStarted = millis();
  while (audio_task_running && millis() - audioWaitStarted < 3000) vTaskDelay(pdMS_TO_TICKS(10));
  if (!web_stop_requested && audio_task_handle != NULL) {
    vTaskDelete(audio_task_handle);
    audio_task_handle = NULL;
    audio_task_running = false;
  }
  vFile.close();
  if (has_audio) aFile.close();
  playback_active = false;

  if (web_stop_requested) {
    Serial.println("Playback stopped by web control");
    return;
  }

  if (channel == 0) {
    videoController(0); // Stay in random mode to pick next file
  } else {
    videoController(1);
  }
}

void videoController(int next) {

  video_idx += next;
  int min_idx = has_random ? 0 : 1;
  if (video_idx < min_idx) {
    video_idx = video_count;
  } else if (video_idx > video_count) {
    video_idx = min_idx;
  }
  Serial.printf("video_idx : %d\n", video_idx);
  preferences.begin(APP_NAME, false);
  preferences.putInt(K_VIDEO_INDEX, video_idx);
  preferences.end();
  playback_paused = false;
  web_stop_requested = true;
}

#if defined(LAUNCHER_RESET) && (LAUNCHER_RESET > 0)
void resetToLauncher() {
  Serial.println("Resetting to Launcher...");
  is_showing_message = true;
  gfx->setCursor(20, 20);
  gfx->setTextColor(RED);
  gfx->setTextSize(3, 3, 0);
  gfx->printf("EXITING...");

#if (LAUNCHER_RESET == 1)
  // --- Mode 1: Retro-Go Launcher ---
  // Find launcher partition (label "launcher" or first OTA app partition)
  const esp_partition_t *launcher_partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "launcher");
  if (!launcher_partition) {
    launcher_partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
  }

  if (launcher_partition) {
    esp_err_t err = esp_ota_set_boot_partition(launcher_partition);
    if (err != ESP_OK) {
      Serial.printf("esp_ota_set_boot_partition failed: 0x%X\n", err);
    } else {
      Serial.println("Boot partition set to launcher.");
    }
  } else {
    Serial.println("Launcher partition not found!");
  }

  vTaskDelay(pdMS_TO_TICKS(300));
  Serial.flush();
  esp_restart();

#elif (LAUNCHER_RESET == 2)
  // --- Mode 2: CYD-Launcher ---
  vTaskDelay(pdMS_TO_TICKS(300));
  Serial.flush();

  // bmorcelli bootloader boots Launcher menu when hardware reset reason is POWERON (1) or DEEPSLEEP (5)
  esp_sleep_enable_timer_wakeup(10000); // 10ms
  esp_deep_sleep_start();
#endif
}
#endif
