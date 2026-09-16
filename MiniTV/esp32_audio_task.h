#include "driver/i2s.h"

#include "AACDecoderHelix.h"
#include "MP3DecoderHelix.h"

static unsigned long total_read_audio_ms = 0;
static unsigned long total_decode_audio_ms = 0;
static unsigned long total_play_audio_ms = 0;

static i2s_port_t _i2s_num;
static bool _dac_enabled = false;
static volatile bool audio_stop_requested = false;
static volatile bool audio_task_running = false;
static TaskHandle_t audio_task_handle = NULL;
static volatile int audio_volume = AUDIO_VOLUME;

static inline int16_t applyAudioGain(int16_t sample)
{
    int32_t amplified = ((int32_t)sample * audio_volume) / 100;
    if (amplified > 32767) amplified = 32767;
    if (amplified < -32768) amplified = -32768;
    return (int16_t)amplified;
}

static void i2s_silence()
{
    size_t written;
    int16_t samples[160];
#ifdef USE_INTERNAL_DAC
    for (int i = 0; i < 160; i++) samples[i] = 0x8000;
#else
    memset(samples, 0, sizeof(samples));
#endif
    for (int i = 0; i < 8; i++) {
        i2s_write(_i2s_num, samples, sizeof(samples), &written, portMAX_DELAY);
    }
}

static esp_err_t i2s_init(i2s_port_t i2s_num, uint32_t sample_rate,
                          int mck_io_num,   /*!< MCK in out pin. Note that ESP32 supports setting MCK on GPIO0/GPIO1/GPIO3 only*/
                          int bck_io_num,   /*!< BCK in out pin*/
                          int ws_io_num,    /*!< WS in out pin*/
                          int data_out_num, /*!< DATA out pin*/
                          int data_in_num   /*!< DATA in pin*/
)
{
    _i2s_num = i2s_num;
    _dac_enabled = false;

    esp_err_t ret_val = ESP_OK;

    i2s_config_t i2s_config;
#ifdef USE_INTERNAL_DAC
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
#else
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
#endif
    i2s_config.sample_rate = sample_rate;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
#ifdef USE_INTERNAL_DAC
    i2s_config.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB;
#else
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#endif
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 8;
    i2s_config.dma_buf_len = 160;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;
    i2s_config.fixed_mclk = 0;
    i2s_config.mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT;
    i2s_config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

    ret_val |= i2s_driver_install(i2s_num, &i2s_config, 0, NULL);

    i2s_silence();

#ifndef USE_INTERNAL_DAC
    i2s_pin_config_t pin_config;
    pin_config.mck_io_num = mck_io_num;
    pin_config.bck_io_num = bck_io_num;
    pin_config.ws_io_num = ws_io_num;
    pin_config.data_out_num = data_out_num;
    pin_config.data_in_num = data_in_num;
    ret_val |= i2s_set_pin(i2s_num, &pin_config);
#endif

    return ret_val;
}

static void i2s_deinit()
{
    i2s_silence(); 
    i2s_driver_uninstall(_i2s_num);
    _dac_enabled = false;
}

static int _samprate = 0;
bool is_muted = false;

static void aacAudioDataCallback(AACFrameInfo &info, int16_t *pwm_buffer, size_t len)
{
    unsigned long s = millis();
    if (_samprate != info.sampRateOut)
    {
        i2s_set_clk(_i2s_num, info.sampRateOut, I2S_BITS_PER_SAMPLE_16BIT, (info.nChans == 2) ? I2S_CHANNEL_STEREO : I2S_CHANNEL_MONO);
#ifdef USE_INTERNAL_DAC
        i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
        _dac_enabled = true;
#endif
        _samprate = info.sampRateOut;
    }

#ifdef USE_INTERNAL_DAC
    if (!_dac_enabled) {
        i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
        _dac_enabled = true;
    }
#endif

    if (is_muted)
    {
#ifdef USE_INTERNAL_DAC
        for (int i = 0; i < len; i++) pwm_buffer[i] = 0x8000;
#else
        memset(pwm_buffer, 0, len * 2);
#endif
    }
    else {
        if (audio_volume != 100) {
            for (int i = 0; i < len; i++) {
                pwm_buffer[i] = applyAudioGain(pwm_buffer[i]);
            }
        }

#ifdef USE_INTERNAL_DAC
        for (int i = 0; i < len; i++) {
            pwm_buffer[i] = (uint16_t)pwm_buffer[i] + 0x8000;
        }
#endif
    }

    size_t i2s_bytes_written = 0;
    i2s_write(_i2s_num, pwm_buffer, len * 2, &i2s_bytes_written, portMAX_DELAY);
    total_play_audio_ms += millis() - s;
}

static void mp3AudioDataCallback(MP3FrameInfo &info, int16_t *pwm_buffer, size_t len)
{
    unsigned long s = millis();
    if (_samprate != info.samprate)
    {
        i2s_set_clk(_i2s_num, info.samprate, I2S_BITS_PER_SAMPLE_16BIT, (info.nChans == 2) ? I2S_CHANNEL_STEREO : I2S_CHANNEL_MONO);
#ifdef USE_INTERNAL_DAC
        i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
        _dac_enabled = true;
#endif
        _samprate = info.samprate;
    }

#ifdef USE_INTERNAL_DAC
    if (!_dac_enabled) {
        i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
        _dac_enabled = true;
    }
#endif

    if (is_muted)
    {
#ifdef USE_INTERNAL_DAC
        for (int i = 0; i < len; i++) pwm_buffer[i] = 0x8000;
#else
        memset(pwm_buffer, 0, len * 2);
#endif
    }
    else {
        if (audio_volume != 100) {
            for (int i = 0; i < len; i++) {
                pwm_buffer[i] = applyAudioGain(pwm_buffer[i]);
            }
        }

#ifdef USE_INTERNAL_DAC
        for (int i = 0; i < len; i++) {
            pwm_buffer[i] = (uint16_t)pwm_buffer[i] + 0x8000;
        }
#endif
    }

    size_t i2s_bytes_written = 0;
    i2s_write(_i2s_num, pwm_buffer, len * 2, &i2s_bytes_written, portMAX_DELAY);
    total_play_audio_ms += millis() - s;
}

static uint8_t _frame[MP3_MAX_FRAME_SIZE]; 

static libhelix::AACDecoderHelix _aac(aacAudioDataCallback);
static void aac_player_task(void *pvParam)
{
    audio_task_running = true;
    Stream *input = (Stream *)pvParam;
    int r, w;
    unsigned long ms = millis();
    while (!audio_stop_requested && (r = input->readBytes(_frame, MP3_MAX_FRAME_SIZE)))
    {
        total_read_audio_ms += millis() - ms;
        ms = millis();
        while (!audio_stop_requested && r > 0)
        {
            w = _aac.write(_frame, r);
            r -= w;
        }
        total_decode_audio_ms += millis() - ms;
        ms = millis();
    }
    audio_task_running = false;
    vTaskSuspend(NULL); // Owner deletes the retained handle after SD access stops
}

static libhelix::MP3DecoderHelix _mp3(mp3AudioDataCallback);
static void mp3_player_task(void *pvParam)
{
    audio_task_running = true;
    Stream *input = (Stream *)pvParam;
    int r, w;
    unsigned long ms = millis();
    while (!audio_stop_requested && (r = input->readBytes(_frame, MP3_MAX_FRAME_SIZE)))
    {
        total_read_audio_ms += millis() - ms;
        ms = millis();
        while (!audio_stop_requested && r > 0)
        {
            w = _mp3.write(_frame, r);
            r -= w;
        }
        total_decode_audio_ms += millis() - ms;
        ms = millis();
    }
    audio_task_running = false;
    vTaskSuspend(NULL); // Owner deletes the retained handle after SD access stops
}

static BaseType_t aac_player_task_start(Stream *input, BaseType_t audioAssignCore)
{
    if (audio_task_handle != NULL) {
        vTaskDelete(audio_task_handle);
        audio_task_handle = NULL;
    }
    audio_stop_requested = false;
    _aac.begin();
    // A normal priority keeps audio flowing without monopolizing the scheduler
    // and disrupting Wi-Fi during simultaneous video playback.
    return xTaskCreatePinnedToCore((TaskFunction_t)aac_player_task, "AAC Player Task", 3072, (void *const)input, 2, &audio_task_handle, audioAssignCore);
}

static BaseType_t mp3_player_task_start(Stream *input, BaseType_t audioAssignCore)
{
    if (audio_task_handle != NULL) {
        vTaskDelete(audio_task_handle);
        audio_task_handle = NULL;
    }
    audio_stop_requested = false;
    _mp3.begin();
    return xTaskCreatePinnedToCore((TaskFunction_t)mp3_player_task, "MP3 Player Task", 3072, (void *const)input, 2, &audio_task_handle, audioAssignCore);
}
