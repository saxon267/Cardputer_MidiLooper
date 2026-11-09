#include "loop.h"

#include <stdlib.h>
#include <string.h>

LoopEngine looper;

void LoopEngine::begin()
{
    _active   = _bufA;
    _inactive = _bufB;
    _activeN = _inactiveN = _takeN = 0;
    _phaseQ16 = _phaseTick = 0;
    _idx = 0;
    _activeGen = _seenGen = 0;
    _state = LoopState::STOP;
    _overdub = _armed = _autoStop = _takeFull = _overflow = false;
    _startPulse = false;
    _originUs = _armMs = 0;
    _beatFlags = 0;
}

void LoopEngine::setBars(uint16_t bars)
{
    if (bars < 1) bars = 1;
    if (bars > LOOP_MAX_BARS) bars = LOOP_MAX_BARS;
    _bars = bars;
    portENTER_CRITICAL(&_mux);
    _phaseQ16 = 0;
    _phaseTick = 0;
    _idx = 0;
    portEXIT_CRITICAL(&_mux);
}

void LoopEngine::setBpm(uint16_t bpmX10)
{
    if (bpmX10 < 400) bpmX10 = 400;      // 40 BPM
    if (bpmX10 > 4000) bpmX10 = 4000;    // 400 BPM
    _bpmX10 = bpmX10;
}

bool LoopEngine::startFirst()
{
    if (_state == LoopState::REC || _state == LoopState::ARMED) return false;
    portENTER_CRITICAL(&_mux);
    _state      = LoopState::ARMED;
    _overdub    = false;
    _armed      = true;
    _inactiveN  = 0;
    _takeN      = 0;
    _phaseQ16   = 0;
    _phaseTick  = 0;
    _idx        = 0;
    _autoStop   = false;
    _takeFull   = false;
    _overflow   = false;
    _armMs      = millis();
    portEXIT_CRITICAL(&_mux);
    return true;
}

bool LoopEngine::startOverdub()
{
    if (!hasLoop()) return startFirst();
    if (_state == LoopState::REC || _state == LoopState::ARMED) return false;

    portENTER_CRITICAL(&_mux);
    if (_state != LoopState::PLAY) {
        _phaseQ16  = 0;
        _phaseTick = 0;
        _idx       = 0;
    }
    _state    = LoopState::REC;
    _overdub  = true;
    _armed    = false;
    _takeN    = 0;
    _autoStop = false;
    _takeFull = false;
    _overflow = false;
    _startPulse = (_phaseTick == 0);
    portEXIT_CRITICAL(&_mux);
    return true;
}

void LoopEngine::stopRecord()
{
    bool over = false;

    portENTER_CRITICAL(&_mux);
    if (_state == LoopState::ARMED) {
        _state = LoopState::STOP;
        _armed = false;
        _phaseQ16 = _phaseTick = 0;
        _idx = 0;
        portEXIT_CRITICAL(&_mux);
        return;
    }
    if (_state != LoopState::REC) {
        portEXIT_CRITICAL(&_mux);
        return;
    }
    over = _overdub;
    _state = LoopState::STOP;   // block any further input appends
    portEXIT_CRITICAL(&_mux);

    if (over) finishOverdub();
    else      finishFirst();
}

bool LoopEngine::play()
{
    if (!hasLoop()) return false;
    portENTER_CRITICAL(&_mux);
    _state     = LoopState::PLAY;
    _overdub   = false;
    _phaseQ16  = 0;
    _phaseTick = 0;
    _idx       = 0;
    _startPulse = true;
    portEXIT_CRITICAL(&_mux);
    return true;
}

void LoopEngine::stop()
{
    portENTER_CRITICAL(&_mux);
    _state     = LoopState::STOP;
    _overdub   = false;
    _phaseQ16  = 0;
    _phaseTick = 0;
    _idx       = 0;
    portEXIT_CRITICAL(&_mux);
}

void LoopEngine::clear()
{
    portENTER_CRITICAL(&_mux);
    _state     = LoopState::STOP;
    _overdub   = false;
    _activeN   = 0;
    _inactiveN = 0;
    _takeN     = 0;
    _phaseQ16  = 0;
    _phaseTick = 0;
    _idx       = 0;
    _overflow  = false;
    ++_activeGen;
    portEXIT_CRITICAL(&_mux);
}

void LoopEngine::service()
{
    if (_state == LoopState::ARMED) {
        if ((uint32_t)(millis() - _armMs) > 20000u) {
            portENTER_CRITICAL(&_mux);
            _state = LoopState::STOP;
            _armed = false;
            portEXIT_CRITICAL(&_mux);
        }
        return;
    }

    if (_state == LoopState::REC && _autoStop) {
        bool over;
        portENTER_CRITICAL(&_mux);
        over = _overdub;
        _state = LoopState::STOP;   // block any further input appends
        portEXIT_CRITICAL(&_mux);
        if (over) finishOverdub();
        else      finishFirst();
    }
}

void LoopEngine::onMidi(uint8_t status, uint8_t d1, uint8_t d2, uint32_t nowUs)
{
    const uint8_t hi = status & 0xF0;
    const uint8_t ch = status & 0x0F;
    if (!channelAllowed(ch)) return;

    LoopEvent e{};
    e.ch = ch;

    if (hi == 0x90) {
        if (d2 == 0) { e.type = LE_NOTE_OFF; e.d1 = d1; e.d2 = 0; }
        else         { e.type = LE_NOTE_ON;  e.d1 = d1; e.d2 = d2; }
    } else if (hi == 0x80) {
        e.type = LE_NOTE_OFF; e.d1 = d1; e.d2 = 0;
    } else if (hi == 0xB0 && d1 == 64) {
        e.type = LE_CC64; e.d1 = 64; e.d2 = d2;
    } else if (hi == 0xB0 && d1 == 1) {
        e.type = LE_CC1; e.d1 = 1; e.d2 = d2;
    } else if (hi == 0xE0) {
        e.type = LE_PB; e.d1 = d1; e.d2 = d2;
    } else {
        return;
    }

    portENTER_CRITICAL(&_mux);
    if (_state == LoopState::ARMED) {
        _state = LoopState::REC;
        _armed = false;
        _overdub = false;
        _originUs = nowUs;
        _phaseQ16 = 0;
        _phaseTick = 0;
        _idx = 0;
        _inactiveN = 0;
        _autoStop = false;
        _takeFull = false;
        _overflow = false;
        _startPulse = true;
        e.tick = 0;
        if (_inactiveN < MAX_LOOP_EVENTS) _inactive[_inactiveN++] = e;
    } else if (_state == LoopState::REC) {
        if (_overdub) {
            const uint32_t len = loopLenTicks();
            e.tick = len ? (_phaseTick % len) : 0;
            appendTake(e);
        } else {
            const uint32_t t = (uint32_t)(((uint64_t)(nowUs - _originUs) * _bpmX10 * 8ull) / 10000000ull);
            if (t >= loopLenTicks()) {
                _autoStop = true;
            } else {
                e.tick = t;
                if (_inactiveN < MAX_LOOP_EVENTS) {
                    _inactive[_inactiveN++] = e;
                } else {
                    _overflow = true;
                    _autoStop = true;
                }
            }
        }
    }
    portEXIT_CRITICAL(&_mux);
}

uint8_t LoopEngine::pump(uint32_t frames)
{
    _beatFlags = 0;
    if (_state != LoopState::REC && _state != LoopState::PLAY) return 0;

    if (_startPulse) {
        _startPulse = false;
        _beatFlags |= 0x03;   // downbeat + accent
    }

    const uint32_t len = loopLenTicks();
    if (!len) return 0;

    LoopEvent* act;
    int n;
    uint32_t gen;
    portENTER_CRITICAL(&_mux);
    act = _active;
    n   = _activeN;
    gen = _activeGen;
    portEXIT_CRITICAL(&_mux);

    if (_seenGen != gen) {
        _seenGen = gen;
        _idx = lowerBound(act, n, _phaseTick);
    }

    const uint64_t inc = (uint64_t)_bpmX10 * 8ull * frames * 65536ull / (10ull * AUDIO_RATE);
    uint64_t q = (uint64_t)_phaseQ16 + inc;
    const uint64_t lenQ = (uint64_t)len << 16;

    const uint32_t prevTick = _phaseTick;
    if (q >= lenQ) {
        q %= lenQ;
        _idx = 0;
        if (_state == LoopState::REC) _autoStop = true;
    }
    _phaseQ16 = (uint32_t)q;
    const uint32_t tick = (uint32_t)(q >> 16);
    _phaseTick = tick;

    const uint32_t prevBeat = prevTick / LOOP_PPQN;
    const uint32_t beat     = tick / LOOP_PPQN;
    if (beat != prevBeat) {
        _beatFlags |= 0x01;
        if ((beat % LOOP_BEATS) == 0) _beatFlags |= 0x02;
    }

    while (_idx < n && act[_idx].tick <= tick) {
        emit(act[_idx]);
        ++_idx;
    }
    return _beatFlags;
}

uint16_t LoopEngine::laneCount(uint8_t lane) const
{
    uint16_t counts[4] = {0, 0, 0, 0};
    laneCounts(counts);
    return lane < 4 ? counts[lane] : 0;
}

void LoopEngine::laneCounts(uint16_t out[4]) const
{
    out[0] = out[1] = out[2] = out[3] = 0;

    const LoopEvent* a = _active;
    int n = _activeN;
    if (_state == LoopState::REC && !_overdub) {
        a = _inactive;
        n = _inactiveN;
    }
    for (int i = 0; i < n; ++i) {
        if (a[i].type != LE_NOTE_ON) continue;
        const int l = laneOfChannel(a[i].ch);
        if (l >= 0 && l < 4) ++out[l];
    }
}

uint32_t LoopEngine::phasePermille() const
{
    const uint32_t len = loopLenTicks();
    if (!len) return 0;
    return (uint32_t)((uint64_t)_phaseTick * 1000ull / len);
}

int LoopEngine::laneOfChannel(uint8_t ch)
{
    switch (ch) {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 9: return 3;
        default: return -1;
    }
}

bool LoopEngine::channelAllowed(uint8_t ch)
{
    return laneOfChannel(ch) >= 0;
}

int LoopEngine::cmpEvent(const void* a, const void* b)
{
    const LoopEvent* x = static_cast<const LoopEvent*>(a);
    const LoopEvent* y = static_cast<const LoopEvent*>(b);
    if (x->tick < y->tick) return -1;
    if (x->tick > y->tick) return  1;
    return 0;
}

void LoopEngine::appendTake(const LoopEvent& e)
{
    if (_takeN < MAX_TAKE_EVENTS) {
        _take[_takeN++] = e;
    } else {
        _takeFull = true;
        _autoStop = true;
    }
}

void LoopEngine::finishFirst()
{
    const int n = _inactiveN;
    if (n <= 0) {
        _state = LoopState::STOP;
        _overdub = false;
        _phaseQ16 = _phaseTick = 0;
        _idx = 0;
        return;
    }
    qsort(_inactive, (size_t)n, sizeof(LoopEvent), cmpEvent);
    swapBuffers();
    __sync_synchronize();
    ++_activeGen;
    _phaseQ16 = 0;
    _phaseTick = 0;
    _idx = 0;
    _state = LoopState::PLAY;
    _overdub = false;
    _startPulse = true;
}

void LoopEngine::finishOverdub()
{
    // Resume playback immediately: the caller stopped the clock to block input,
    // but the loop itself should keep running while the merge happens.
    _state = LoopState::PLAY;
    _overdub = false;

    const int tn = _takeN;
    if (tn <= 0) {
        return;
    }
    qsort(_take, (size_t)tn, sizeof(LoopEvent), cmpEvent);

    int i = 0, j = 0, k = 0;
    while (i < _activeN || j < tn) {
        if (k >= MAX_LOOP_EVENTS) { _overflow = true; break; }
        if (j >= tn || (i < _activeN && _active[i].tick <= _take[j].tick)) {
            _inactive[k++] = _active[i++];
        } else {
            _inactive[k++] = _take[j++];
        }
    }
    _inactiveN = k;
    swapBuffers();
    __sync_synchronize();
    ++_activeGen;
    _idx = lowerBound(_active, _activeN, _phaseTick);
    _state = LoopState::PLAY;
    _overdub = false;
}

void LoopEngine::swapBuffers()
{
    LoopEvent* p = _active;
    _active = _inactive;
    _inactive = p;
    const int n = _activeN;
    _activeN = _inactiveN;
    _inactiveN = n;
}

void LoopEngine::emit(const LoopEvent& e)
{
    if (_sink) _sink(e);
}

int LoopEngine::lowerBound(const LoopEvent* a, int n, uint32_t tick) const
{
    int lo = 0, hi = n;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (a[mid].tick < tick) lo = mid + 1;
        else                    hi = mid;
    }
    return lo;
}
