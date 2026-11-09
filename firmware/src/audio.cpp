#include "audio.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>
#include "loop.h"

AudioEngine audio;

// ---------------------------------------------------------------- ES8311
static bool es8311Write(uint8_t reg, uint8_t val)
{
    return M5Cardputer.In_I2C.writeRegister8(0x18, reg, val, 400000);
}

static uint8_t dacReg(uint8_t volume)
{
    if (volume == 0) return 0x00;
    if (volume <= 50) return (uint8_t)(0x60 + (volume * (0xBF - 0x60)) / 50);
    return (uint8_t)(0xC0 + ((volume - 51) * (0xFF - 0xC0)) / 49);
}

static void es8311Init(uint8_t volume)
{
    es8311Write(0x00, 0x80);
    es8311Write(0x01, 0xB5);
    es8311Write(0x02, 0x18);
    es8311Write(0x09, 0x0C);
    es8311Write(0x0A, 0x0C);
    es8311Write(0x0D, 0x01);
    es8311Write(0x12, 0x00);
    es8311Write(0x13, 0x10);
    es8311Write(0x32, dacReg(volume));
    es8311Write(0x37, 0x08);
    delay(30);
}

static bool installI2s()
{
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = AUDIO_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 6;
    cfg.dma_buf_len          = AUDIO_BLOCK;
    cfg.use_apll             = false;
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = 0;
    cfg.mclk_multiple        = I2S_MCLK_MULTIPLE_256;
    cfg.bits_per_chan        = I2S_BITS_PER_CHAN_16BIT;

    if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PIN_I2S_BCLK;
    pins.ws_io_num    = PIN_I2S_LRCK;
    pins.data_out_num = PIN_I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    return i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
}

// ---------------------------------------------------------------- click
void AudioEngine::initClick()
{
    _len = (int)((uint64_t)AUDIO_RATE * CLICK_MS / 1000);
    if (_len > (int)(sizeof(_accent) / sizeof(_accent[0]))) {
        _len = (int)(sizeof(_accent) / sizeof(_accent[0]));
    }

    const float dur    = _len / (float)AUDIO_RATE;
    const float attack = 0.002f;
    const float tail   = 0.004f;

    auto render = [&](int16_t* dst, float freq, float gain) {
        for (int i = 0; i < _len; ++i) {
            const float t = i / (float)AUDIO_RATE;
            float env = expf(-t * 130.0f);
            if (t < attack) env *= t / attack;
            const float left = dur - t;
            if (left < tail) env *= left / tail;
            if (env < 0.0f) env = 0.0f;
            const float s = sinf(6.2831853f * freq * t);
            float v = s * env * gain;
            if (v > 20000.0f) v = 20000.0f;
            if (v < -20000.0f) v = -20000.0f;
            dst[i] = (int16_t)v;
        }
    };

    render(_accent, 2000.0f, 15000.0f);
    render(_normal, 1350.0f, 9500.0f);
}

void AudioEngine::setClickVolume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    _clickVol = pct;
}

void AudioEngine::triggerClick(bool accent)
{
    if (!_clickOn || _len <= 0) return;
    _buf = accent ? _accent : _normal;
    _pos = 0;
    _active = true;
}

void AudioEngine::renderClick(int16_t* out, int frames)
{
    if (!_active || !_buf) return;
    for (int i = 0; i < frames && _pos < _len; ++i, ++_pos) {
        int32_t v = out[i] + (int32_t)_buf[_pos] * _clickVol / 100;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out[i] = (int16_t)v;
    }
    if (_pos >= _len) _active = false;
}

// ---------------------------------------------------------------- engine
bool AudioEngine::begin()
{
    _codecOk = M5Cardputer.In_I2C.scanID(0x18, 400000);
    if (_codecOk) es8311Init(78);
    initClick();

    i2s_driver_uninstall(I2S_NUM_0);
    return installI2s();
}

void AudioEngine::start()
{
    xTaskCreatePinnedToCore(taskEntry, "looper-audio", 4096, this, 20,
                            (TaskHandle_t*)&_task, 1);
}

void AudioEngine::taskEntry(void* arg)
{
    static_cast<AudioEngine*>(arg)->taskLoop();
}

void AudioEngine::taskLoop()
{
    static int16_t mono[AUDIO_BLOCK];
    static int16_t out[AUDIO_BLOCK * 2];

    uint32_t cpuAcc = 0;
    int      cpuN   = 0;

    while (true) {
        const uint32_t t0 = micros();

        memset(mono, 0, sizeof(mono));
        const uint8_t flags = looper.pump(AUDIO_BLOCK);
        if (flags & 0x01) triggerClick((flags & 0x02) != 0);
        renderClick(mono, AUDIO_BLOCK);

        for (int i = 0; i < AUDIO_BLOCK; ++i) {
            out[i * 2]     = mono[i];
            out[i * 2 + 1] = mono[i];
        }

        size_t written = 0;
        const esp_err_t err = i2s_write(I2S_NUM_0, out, sizeof(out), &written, portMAX_DELAY);
        if (err != ESP_OK) vTaskDelay(1);

        const uint32_t spent = micros() - t0;
        cpuAcc += spent;
        if (++cpuN >= 32) {
            const uint32_t blockUs = (uint32_t)((uint64_t)AUDIO_BLOCK * 1000000ull / AUDIO_RATE);
            uint32_t pct = cpuAcc * 100u / (blockUs * (uint32_t)cpuN);
            if (pct > 100) pct = 100;
            _cpu = (uint8_t)pct;
            cpuAcc = 0;
            cpuN = 0;
        }
    }
}
