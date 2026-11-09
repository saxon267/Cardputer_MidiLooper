#pragma once
#include <stdint.h>
#include "config.h"

class AudioEngine {
public:
    bool begin();
    void start();

    void setClick(bool on) { _clickOn = on; }
    bool click() const { return _clickOn; }
    void setClickVolume(uint8_t pct);
    void triggerClick(bool accent);

    bool     codecOk() const { return _codecOk; }
    uint8_t  cpuPct() const { return _cpu; }

private:
    static void taskEntry(void* arg);
    void        taskLoop();
    void        initClick();
    void        renderClick(int16_t* out, int frames);

    bool     _codecOk = false;
    volatile bool _clickOn = true;
    volatile uint8_t _cpu  = 0;

    volatile bool     _active = false;
    volatile int      _pos    = 0;
    const int16_t*    _buf    = nullptr;

    int16_t  _accent[1600] = {};
    int16_t  _normal[1600] = {};
    int      _len = 0;
    uint8_t  _clickVol = 80;
    void*    _task = nullptr;
};

extern AudioEngine audio;
