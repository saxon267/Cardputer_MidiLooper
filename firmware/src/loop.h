#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "config.h"

enum LoopEventType : uint8_t {
    LE_NOTE_ON  = 1,
    LE_NOTE_OFF = 2,
    LE_CC64     = 3,
    LE_CC1      = 4,
    LE_PB       = 5,
};

struct LoopEvent {
    uint32_t tick;   // 0 .. loopLen-1, 480 PPQN
    uint8_t  type;
    uint8_t  d1;     // note / CC number / PB LSB
    uint8_t  d2;     // velocity / CC value / PB MSB
    uint8_t  ch;     // 0..15
};

enum class LoopState : uint8_t { STOP, ARMED, REC, PLAY };

class LoopEngine {
public:
    void begin();

    void     setBars(uint16_t bars);
    uint16_t bars() const { return _bars; }
    void     setBpm(uint16_t bpmX10);
    uint16_t bpmX10() const { return _bpmX10; }
    uint32_t loopLenTicks() const { return (uint32_t)_bars * LOOP_BEATS * LOOP_PPQN; }

    bool startFirst();
    bool startOverdub();
    void stopRecord();
    bool play();
    void stop();
    void clear();
    void service();                 // main task: finish auto-stop / arm timeout

    void onMidi(uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs);
    uint8_t pump(uint32_t frames);  // audio task: advance clock + dispatch

    LoopState state() const { return _state; }
    bool      hasLoop() const { return _activeN > 0; }
    bool      isOverdub() const { return _overdub; }
    bool      takeFull() const { return _takeFull; }
    bool      overflow() const { return _overflow; }
    int       count() const { return _activeN; }
    int       takeCount() const { return _takeN; }
    uint16_t  laneCount(uint8_t lane) const;
    void      laneCounts(uint16_t out[4]) const;
    uint32_t  positionTicks() const { return _phaseTick; }
    uint32_t  phasePermille() const;
    uint8_t   beatFlags() const { return _beatFlags; }
    const LoopEvent* events() const { return _active; }

    void setSink(void (*sink)(const LoopEvent&)) { _sink = sink; }

    static int  laneOfChannel(uint8_t ch);
    static bool channelAllowed(uint8_t ch);

private:
    static int cmpEvent(const void* a, const void* b);
    void finishFirst();
    void finishOverdub();
    void appendTake(const LoopEvent& e);
    void swapBuffers();
    void emit(const LoopEvent& e);
    int  lowerBound(const LoopEvent* a, int n, uint32_t tick) const;

    LoopEvent _bufA[MAX_LOOP_EVENTS];
    LoopEvent _bufB[MAX_LOOP_EVENTS];
    LoopEvent _take[MAX_TAKE_EVENTS];

    LoopEvent* _active   = _bufA;
    LoopEvent* _inactive = _bufB;

    volatile int      _activeN   = 0;
    volatile int      _inactiveN = 0;
    volatile int      _takeN     = 0;
    volatile uint32_t _phaseQ16  = 0;
    volatile uint32_t _phaseTick = 0;
    volatile int      _idx       = 0;
    volatile uint32_t _activeGen = 0;
    volatile uint32_t _seenGen   = 0;
    volatile LoopState _state    = LoopState::STOP;
    volatile bool     _overdub   = false;
    volatile bool     _armed     = false;
    volatile bool     _autoStop  = false;
    volatile bool     _takeFull  = false;
    volatile bool     _overflow  = false;
    volatile bool     _startPulse = false;
    volatile uint32_t _originUs  = 0;
    volatile uint32_t _armMs     = 0;
    volatile uint8_t  _beatFlags = 0;

    uint16_t _bars   = 4;
    uint16_t _bpmX10 = 1200;
    void   (*_sink)(const LoopEvent&) = nullptr;

    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

extern LoopEngine looper;
