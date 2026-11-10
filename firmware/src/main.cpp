#include <Arduino.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "audio.h"
#include "config.h"
#include "library.h"
#include "loop.h"
#include "loop_store.h"
#include "ui.h"
#include "usb_midi_host.h"

App app;
static Preferences prefs;

// ---------------------------------------------------------------- helpers
static void setToast(const char* msg)
{
    strncpy(app.toast, msg, sizeof(app.toast) - 1);
    app.toast[sizeof(app.toast) - 1] = 0;
    app.toastUntil = millis() + 1800;
}

static void adjustScroll()
{
    if (app.sel < app.scroll) app.scroll = app.sel;
    if (app.sel >= app.scroll + 6) app.scroll = app.sel - 5;
    if (app.scroll < 0) app.scroll = 0;
}

static void refreshLibrary()
{
    if (!app.sdOk) return;
    if (!library.scan(LOOP_DIR)) {
        SD.mkdir(LOOP_DIR);
        library.scan(LOOP_DIR);
    }
    const int n = library.count();
    if (app.sel >= n) app.sel = n ? n - 1 : 0;
    if (app.sel < 0) app.sel = 0;
    adjustScroll();
}

static void emitEvent(const LoopEvent& e)
{
    switch (LoopEngine::typeOf(e)) {
        case LE_NOTE_ON:  usbMidi.noteOn(e.ch, e.d1, e.d2); break;
        case LE_NOTE_OFF: usbMidi.noteOff(e.ch, e.d1); break;
        case LE_CC64:     usbMidi.controlChange(e.ch, 64, e.d2); break;
        case LE_CC1:      usbMidi.controlChange(e.ch, 1, e.d2); break;
        case LE_PB:       usbMidi.pitchBend(e.ch, (uint16_t)e.d1 | ((uint16_t)e.d2 << 7)); break;
        default: break;
    }
}

static void onUsbConnect(bool connected, void*)
{
    app.usbConnected = connected;
    if (connected) {
        usbMidi.panic();
        setToast("FM1 CONNECTED");
    } else {
        setToast("FM1 DISCONNECTED");
    }
}

static void onUsbMidi(uint8_t status, uint8_t d1, uint8_t d2, void*)
{
    if ((status & 0xF0) == 0x90 && d2 > 0 &&
        (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED)) {
        const uint8_t ch = status & 0x0F;
        const int lane = (ch == 0) ? 0 : (ch == 1) ? 1 : (ch == 2) ? 2 : (ch == 9) ? 3 : -1;
        if (lane >= 0) app.laneFlash[lane] = millis();
    }
    looper.onMidi(status, d1, d2, (uint32_t)micros());
}

static void syncApp()
{
    app.loopState     = looper.state();
    app.hasLoop       = looper.hasLoop();
    app.overdub       = looper.isOverdub();
    app.takeFull      = looper.takeFull();
    app.overflow      = looper.overflow();
    app.loopMode      = looper.loopMode();
    app.phasePermille = (uint16_t)looper.phasePermille();

    const uint32_t barLen = (uint32_t)LOOP_PPQN * LOOP_BEATS;
    uint32_t bar = looper.positionTicks() / barLen + 1;
    if (bar > app.bars) bar = app.bars;
    if (bar < 1) bar = 1;
    app.barNow = (uint16_t)bar;

    app.fmClock = usbMidi.clockValid();
    app.fmBpmX10 = (uint16_t)(usbMidi.bpm() * 10.0f + 0.5f);
}

// ---------------------------------------------------------------- loop actions
static bool startFirstTake()
{
    if (!app.usbConnected) {
        setToast("CONNECT FM1");
        return false;
    }
    if (!usbMidi.inputEndpoint()) {
        setToast("NO MIDI IN");
        return false;
    }
    looper.startFirst();
    app.countInLeft     = app.countIn ? LOOP_BEATS : 0;
    app.countInInterval = (uint16_t)(600000u / (app.bpmX10 ? app.bpmX10 : 1200u));
    app.countInNext     = millis();
    setToast(app.countIn ? "COUNT IN" : "WAIT MIDI");
    return true;
}

static void toggleRecord()
{
    if (looper.loopMode() &&
        (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED)) {
        // In Loop mode L owns the transport; R acts as stop/exit instead of
        // cancelling or starting a second take.
        looper.setLoopMode(false);
        prefs.putBool("loop", false);
        looper.stopRecord();
        usbMidi.panic();
        app.countInLeft = 0;
        setToast("LOOP OFF");
        return;
    }

    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        looper.stopRecord();
        usbMidi.panic();
        app.countInLeft = 0;
        setToast(looper.state() == LoopState::PLAY ? "LOOP PLAY" : "STOPPED");
        return;
    }
    if (!app.usbConnected) {
        setToast("CONNECT FM1");
        return;
    }
    if (!usbMidi.inputEndpoint()) {
        setToast("NO MIDI IN");
        return;
    }
    if (looper.state() == LoopState::PLAY || looper.hasLoop()) {
        looper.startOverdub();
        setToast("OVERDUB");
        return;
    }
    startFirstTake();
}

static void togglePlay()
{
    if (looper.state() == LoopState::PLAY) {
        looper.stop();
        usbMidi.panic();
        setToast("STOPPED");
    } else if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        looper.stopRecord();
        usbMidi.panic();
        app.countInLeft = 0;
        setToast("STOPPED");
    } else if (!looper.play()) {
        setToast("REC FIRST");
    }
}

static void clearLoop()
{
    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        looper.stopRecord();
    }
    looper.clear();
    usbMidi.panic();
    app.countInLeft = 0;
    setToast("TRACK CLEARED");
}

static void saveLoop()
{
    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        looper.stopRecord();
    }
    if (!looper.hasLoop()) {
        setToast("NOTHING TO SAVE");
        return;
    }
    looper.stop();
    usbMidi.panic();

    char path[64] = {0};
    if (!loopStore.save(looper.events(), looper.count(), app.bpmX10, app.bars, path, sizeof(path))) {
        setToast(app.sdOk ? "SAVE FAIL" : "NO SD");
        return;
    }
    strncpy(app.lastSaved, path, sizeof(app.lastSaved) - 1);
    setToast("SAVED");
    refreshLibrary();
}

static void deleteSelected()
{
    app.confirmDelete = false;
    if (!app.sdOk || app.sel < 0 || app.sel >= library.count()) {
        setToast("NO FILE");
        return;
    }
    char path[96];
    strncpy(path, library.at(app.sel).path, sizeof(path) - 1);
    path[sizeof(path) - 1] = 0;
    if (!SD.remove(path)) {
        setToast("DELETE FAIL");
        return;
    }
    refreshLibrary();
    setToast("DELETED");
}

// ---------------------------------------------------------------- load / mute
static const uint8_t kLaneCh[4] = {0, 1, 2, 9};

static void clearSelectedLane()
{
    const uint8_t lane = app.selectedLane;
    if (looper.state() == LoopState::STOP && !looper.hasLoop()) {
        setToast("NO LOOP");
        return;
    }
    if (!looper.clearLane(lane)) {
        setToast("TRACK EMPTY");
        return;
    }
    usbMidi.allNotesOff(kLaneCh[lane]);
    syncApp();
    char b[20];
    snprintf(b, sizeof(b), "CH%d CLEARED", lane == 3 ? 10 : (int)lane + 1);
    setToast(b);
}

static void toggleLaneMute(uint8_t lane)
{
    if (lane > 3) return;
    const bool muted = looper.toggleMute(lane);
    if (muted) usbMidi.allNotesOff(kLaneCh[lane]);
    char b[16];
    snprintf(b, sizeof(b), "CH%d %s", lane == 3 ? 10 : (int)lane + 1,
             muted ? "MUTE" : "ON");
    setToast(b);
}

static void loadSelected()
{
    if (!app.sdOk || app.sel < 0 || app.sel >= library.count()) {
        setToast("NO FILE");
        return;
    }

    // The parser writes into the inactive buffer; finish any take first so it
    // is not being written while we parse.
    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        looper.stopRecord();
    }

    LoopEvent* buf = looper.loadBuffer();
    int n = 0;
    LoopStore::LoadInfo info;
    if (!loopStore.load(library.at(app.sel).path, buf, looper.loadCapacity(), n, info)) {
        setToast("LOAD FAIL");
        return;
    }

    looper.stop();
    usbMidi.panic();
    if (!looper.loadCommit(n, info.bars)) {
        setToast("LOAD FAIL");
        return;
    }

    app.bars = info.bars;
    app.bpmX10 = info.bpmX10;
    looper.setBpm(app.bpmX10);
    app.countInLeft = 0;
    app.screen = Screen::LOOP;
    looper.play();
    syncApp();
    setToast("PLAY");
}

// ---------------------------------------------------------------- rename
static char renameLast[32] = {0};
static bool renameSeen = false;

static void startRename()
{
    if (!app.sdOk || app.sel < 0 || app.sel >= library.count()) {
        setToast("NO FILE");
        return;
    }
    const FileEntry& e = library.at(app.sel);
    const char* dot = strrchr(e.name, '.');
    size_t stem = dot ? (size_t)(dot - e.name) : strlen(e.name);
    if (stem > sizeof(app.renameBuf) - 1) stem = sizeof(app.renameBuf) - 1;

    memcpy(app.renameBuf, e.name, stem);
    app.renameBuf[stem] = 0;
    strncpy(app.renameExt, (dot && strlen(dot) < sizeof(app.renameExt)) ? dot : ".MID",
            sizeof(app.renameExt) - 1);
    app.renameExt[sizeof(app.renameExt) - 1] = 0;
    strncpy(app.renameOld, e.name, sizeof(app.renameOld) - 1);
    app.renameOld[sizeof(app.renameOld) - 1] = 0;

    app.renaming = true;
    app.confirmDelete = false;
    renameSeen = false;
    renameLast[0] = 0;
}

static void cancelRename()
{
    app.renaming = false;
    setToast("CANCELED");
}

static void finishRename()
{
    if (!app.renaming) return;
    app.renaming = false;

    if (!app.sdOk || app.sel < 0 || app.sel >= library.count()) {
        setToast("NO FILE");
        return;
    }
    if (!app.renameBuf[0]) {
        setToast("EMPTY NAME");
        return;
    }

    char newName[48];
    snprintf(newName, sizeof(newName), "%s%s", app.renameBuf, app.renameExt);

    char oldPath[96];
    strncpy(oldPath, library.at(app.sel).path, sizeof(oldPath) - 1);
    oldPath[sizeof(oldPath) - 1] = 0;

    char target[128];
    snprintf(target, sizeof(target), LOOP_DIR "/%s", newName);

    if (strcasecmp(oldPath, target) == 0) {
        setToast("UNCHANGED");
        return;
    }
    if (SD.exists(target)) {
        setToast("NAME EXISTS");
        return;
    }
    if (!SD.rename(oldPath, target)) {
        setToast("RENAME FAIL");
        return;
    }

    refreshLibrary();
    for (int i = 0; i < library.count(); ++i) {
        if (strcmp(library.at(i).name, newName) == 0) {
            app.sel = i;
            break;
        }
    }
    adjustScroll();
    setToast("RENAMED");
}

// ---------------------------------------------------------------- settings
static void adjustBpm(int delta)
{
    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        setToast("RECORDING");
        return;
    }
    int v = (int)app.bpmX10 + delta * 10;
    if (v < 400) v = 400;
    if (v > 4000) v = 4000;
    app.bpmX10 = (uint16_t)v;
    looper.setBpm(app.bpmX10);
    prefs.putUShort("bpm", app.bpmX10);
}

static void cycleBars(int delta)
{
    static const uint16_t opts[] = {1, 2, 4, 8, 16, 32};
    int idx = 0;
    for (int i = 0; i < 6; ++i) if (opts[i] == app.bars) idx = i;
    idx += delta;
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;

    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        setToast("CLEAR FIRST");
        return;
    }
    if (looper.hasLoop()) {
        setToast("CLEAR FIRST");
        return;
    }
    app.bars = opts[idx];
    looper.setBars(app.bars);
    prefs.putUShort("bars", app.bars);
}

static void toggleClick()
{
    app.click = !app.click;
    audio.setClick(app.click);
    prefs.putBool("click", app.click);
    setToast(app.click ? "CLICK ON" : "CLICK OFF");
}

static void toggleLoopMode()
{
    const bool on = looper.toggleLoopMode();
    prefs.putBool("loop", on);

    if (!on) {
        if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
            looper.stopRecord();
            usbMidi.panic();
            app.countInLeft = 0;
        }
        setToast("LOOP OFF");
        return;
    }

    if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
        setToast("LOOP REC");
        return;
    }
    if (!app.usbConnected || !usbMidi.inputEndpoint()) {
        setToast("LOOP ON");
        return;
    }
    if (looper.hasLoop()) {
        looper.startOverdub();
        setToast("LOOP REC");
    } else if (startFirstTake()) {
        setToast("LOOP ARM");
    }
}

static void cycleQuantize(int dir)
{
    int q = (int)app.quantize + dir;
    if (q < Q_OFF) q = Q_1_32;
    if (q > Q_1_32) q = Q_OFF;
    app.quantize = (uint8_t)q;
    looper.setQuantize(app.quantize);
    prefs.putUChar("quant", app.quantize);
    static const char* names[] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
    setToast(names[app.quantize]);
}

static void cycleSwing(int dir)
{
    int s = (int)app.swing + dir * 5;
    if (s < 50) s = 50;
    if (s > 75) s = 75;
    app.swing = (uint8_t)s;
    looper.setSwing(app.swing);
    prefs.putUChar("swing", app.swing);
    char b[12];
    snprintf(b, sizeof(b), "SWING %u", app.swing);
    setToast(b);
}

static void adjustSetting(int dir)
{
    switch (app.settingsRow) {
        case 0: adjustBpm(dir * 5); break;
        case 1: cycleBars(dir); break;
        case 2: cycleQuantize(dir); break;
        case 3: cycleSwing(dir); break;
        case 4: toggleClick(); break;
        case 5: app.countIn = !app.countIn; prefs.putBool("cnt", app.countIn);
                setToast(app.countIn ? "COUNT-IN ON" : "COUNT-IN OFF"); break;
        default: break;
    }
}

// ---------------------------------------------------------------- keys
static void handleKeys()
{
    auto& ks = M5Cardputer.Keyboard.keysState();

    static bool pEnter = false, pSpace = false, pDel = false, pTab = false;
    static bool pUp = false, pDown = false, pLeft = false, pRight = false;
    static bool pR = false, pS = false, pM = false, pG = false, pC = false;
    static bool pD = false, pN = false, pMinus = false, pPlus = false, pU = false, pL = false;
    static bool pD1 = false, pD2 = false, pD3 = false, pD4 = false, pFnC = false;

    const bool enter = ks.enter;
    const bool space = ks.space;
    const bool del   = ks.del;
    const bool tab   = ks.tab;
    const bool up    = M5Cardputer.Keyboard.isKeyPressed(';');
    const bool down  = M5Cardputer.Keyboard.isKeyPressed('.');
    const bool left  = M5Cardputer.Keyboard.isKeyPressed(',');
    const bool right = M5Cardputer.Keyboard.isKeyPressed('/');
    const bool rkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('r');
    const bool skey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('s');
    const bool mkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('m');
    const bool gkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('g');
    const bool craw  = M5Cardputer.Keyboard.isKeyPressed('c');
    const bool ckey  = !ks.fn && craw;
    const bool fnC   = ks.fn && craw;
    const bool dkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('d');
    const bool nkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('n');
    const bool ukey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('u');
    const bool lkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('l');
    const bool minus = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('-');
    const bool plus  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('=');
    const bool d1 = M5Cardputer.Keyboard.isKeyPressed('1');
    const bool d2 = M5Cardputer.Keyboard.isKeyPressed('2');
    const bool d3 = M5Cardputer.Keyboard.isKeyPressed('3');
    const bool d4 = M5Cardputer.Keyboard.isKeyPressed('4');

    const bool eEnter = enter && !pEnter;
    const bool eSpace = space && !pSpace;
    const bool eDel   = del && !pDel;
    const bool eTab   = tab && !pTab;
    const bool eUp    = up && !pUp;
    const bool eDown  = down && !pDown;
    const bool eLeft  = left && !pLeft;
    const bool eRight = right && !pRight;
    const bool eR = rkey && !pR;
    const bool eS = skey && !pS;
    const bool eM = mkey && !pM;
    const bool eG = gkey && !pG;
    const bool eC = ckey && !pC;
    const bool eFnC = fnC && !pFnC;
    const bool eD = dkey && !pD;
    const bool eN = nkey && !pN;
    const bool eU = ukey && !pU;
    const bool eL = lkey && !pL;
    const bool eMinus = minus && !pMinus;
    const bool ePlus  = plus && !pPlus;
    const bool eD1 = d1 && !pD1;
    const bool eD2 = d2 && !pD2;
    const bool eD3 = d3 && !pD3;
    const bool eD4 = d4 && !pD4;

    pEnter = enter; pSpace = space; pDel = del; pTab = tab;
    pUp = up; pDown = down; pLeft = left; pRight = right;
    pR = rkey; pS = skey; pM = mkey; pG = gkey; pC = ckey;
    pFnC = fnC;
    pD = dkey; pN = nkey; pMinus = minus; pPlus = plus; pU = ukey;
    pL = lkey;
    pD1 = d1; pD2 = d2; pD3 = d3; pD4 = d4;

    if (app.renaming) {
        char cur[32] = {0};
        int clen = 0;
        for (char c : ks.word) {
            if (c < 32 || c > 126) continue;
            if (strchr("/\\:*?\"<>|", c)) continue;
            if (clen < (int)sizeof(cur) - 1) cur[clen++] = c;
        }
        if (!renameSeen) {
            strncpy(renameLast, cur, sizeof(renameLast) - 1);
            renameLast[sizeof(renameLast) - 1] = 0;
            renameSeen = true;
        } else {
            int count[128] = {0};
            for (const char* p = renameLast; *p; ++p) count[(uint8_t)*p]++;
            for (int i = 0; i < clen; ++i) {
                const uint8_t c = (uint8_t)cur[i];
                if (count[c] > 0) { --count[c]; continue; }
                const size_t n = strlen(app.renameBuf);
                if (n < sizeof(app.renameBuf) - 1) {
                    app.renameBuf[n] = (char)c;
                    app.renameBuf[n + 1] = 0;
                }
            }
            strncpy(renameLast, cur, sizeof(renameLast) - 1);
            renameLast[sizeof(renameLast) - 1] = 0;
        }

        if (eDel) {
            const size_t n = strlen(app.renameBuf);
            if (n) app.renameBuf[n - 1] = 0;
        }
        if (eEnter) finishRename();
        else if (eTab) cancelRename();
        return;
    }

    if (app.confirmDelete) {
        if (eEnter || eSpace) deleteSelected();
        else if (eDel || eD) { app.confirmDelete = false; setToast("CANCELED"); }
        return;
    }

    if (app.settings) {
        if (eUp)   app.settingsRow = (app.settingsRow + 5) % 6;
        if (eDown) app.settingsRow = (app.settingsRow + 1) % 6;
        if (eLeft) adjustSetting(-1);
        if (eRight) adjustSetting(+1);
        if (eEnter) {
            if (app.settingsRow >= 4) adjustSetting(+1);
            else app.settings = false;
        }
        if (eDel || eG || eTab) app.settings = false;
        return;
    }

    if (eTab) {
        if (app.screen == Screen::LOOP) {
            refreshLibrary();
            app.screen = Screen::FILES;
            setToast("FILES");
        } else if (app.screen == Screen::FILES) {
            app.screen = Screen::LOOP;
        }
        return;
    }

    if (app.screen == Screen::LOOP) {
        if (eR || eSpace) toggleRecord();
        if (eEnter) togglePlay();
        if (eS) saveLoop();
        if (eC) clearLoop();
        if (eFnC) clearSelectedLane();
        if (eM) toggleClick();
        if (eL) toggleLoopMode();
        if (eU) {
            const bool ok = looper.undo();
            syncApp();
            setToast(ok ? "UNDO" : "NOTHING TO UNDO");
        }
        if (eG) { app.settings = true; app.settingsRow = 0; }

        if (!ks.fn && eMinus) adjustBpm(-1);
        if (!ks.fn && ePlus)  adjustBpm(+1);

        if (!ks.fn && eLeft)  adjustBpm(-1);
        if (!ks.fn && eRight) adjustBpm(+1);
        if (ks.fn && eLeft)  adjustBpm(-5);
        if (ks.fn && eRight) adjustBpm(+5);

        if (!ks.fn && eUp)   app.selectedLane = (app.selectedLane + 3) % 4;
        if (!ks.fn && eDown) app.selectedLane = (app.selectedLane + 1) % 4;
        if (ks.fn && eUp)   cycleBars(+1);
        if (ks.fn && eDown) cycleBars(-1);

        if (eD1) { if (ks.fn) toggleLaneMute(0); else app.selectedLane = 0; }
        if (eD2) { if (ks.fn) toggleLaneMute(1); else app.selectedLane = 1; }
        if (eD3) { if (ks.fn) toggleLaneMute(2); else app.selectedLane = 2; }
        if (eD4) { if (ks.fn) toggleLaneMute(3); else app.selectedLane = 3; }

        if (eDel) {
            if (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED) {
                looper.stopRecord();
            } else {
                looper.stop();
            }
            usbMidi.panic();
            app.countInLeft = 0;
            setToast("STOPPED");
        }
        return;
    }

    if (app.screen == Screen::FILES) {
        const int n = library.count();
        if (eUp && n)   { app.sel = (app.sel + n - 1) % n; adjustScroll(); }
        if (eDown && n) { app.sel = (app.sel + 1) % n; adjustScroll(); }
        if (eD && n) app.confirmDelete = true;
        if (eN && n) startRename();
        if (eEnter) {
            if (n) loadSelected();
            else   setToast("NO FILE");
        }
        if (eDel) app.screen = Screen::LOOP;
        return;
    }

    if (app.screen == Screen::NO_SD) {
        if (eEnter) {
            app.sdOk = library.begin();
            if (app.sdOk) {
                SD.mkdir(LOOP_DIR);
                refreshLibrary();
                app.screen = Screen::LOOP;
                setToast("SD OK");
            } else {
                setToast("NO SD");
            }
        }
    }
}

// ---------------------------------------------------------------- serial
static void handleSerial()
{
    while (Serial.available()) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == 'i' || c == 'I') {
            Serial.printf("[loop] state=%d bars=%u bpm=%u ev=%d take=%d phase=%u rx=%u ovf=%d mode=%d heap=%u\n",
                          (int)looper.state(), app.bars, app.bpmX10 / 10, looper.count(),
                          looper.takeCount(), (unsigned)looper.phasePermille(),
                          (unsigned)usbMidi.inputCount(), looper.overflow() ? 1 : 0,
                          looper.loopMode() ? 1 : 0, (unsigned)ESP.getFreeHeap());
        } else if (c == 'r' || c == 'R') {
            toggleRecord();
        } else if (c == 'p' || c == 'P') {
            togglePlay();
        } else if (c == 's' || c == 'S') {
            saveLoop();
        } else if (c == 'c' || c == 'C') {
            clearLoop();
        }
    }
}

// ---------------------------------------------------------------- setup/loop
void setup()
{
    Serial.begin(115200);
    delay(60);
    Serial.printf("\n[looper] boot v%s heap=%u\n", APP_VERSION, (unsigned)ESP.getFreeHeap());

    auto cfg = M5.config();
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setBrightness(140);

    prefs.begin("looper", false);
    app.bpmX10  = prefs.getUShort("bpm", 1200);
    app.bars    = prefs.getUShort("bars", 4);
    app.click   = prefs.getBool("click", true);
    app.countIn = prefs.getBool("cnt", true);
    app.quantize = prefs.getUChar("quant", Q_1_16);
    app.swing    = prefs.getUChar("swing", 50);
    app.loopMode = prefs.getBool("loop", false);
    if (app.quantize > Q_1_32) app.quantize = Q_1_16;
    if (app.swing < 50) app.swing = 50;
    if (app.swing > 75) app.swing = 75;
    app.selectedLane = prefs.getUChar("lane", 0);
    if (app.bpmX10 < 400 || app.bpmX10 > 4000) app.bpmX10 = 1200;
    if (app.bars < 1 || app.bars > LOOP_MAX_BARS) app.bars = 4;
    if (app.selectedLane > 3) app.selectedLane = 0;

    ui.begin();
    ui.boot(0, 0);
    delay(80);

    const bool audioOk = audio.begin();
    audio.setClick(app.click);
    audio.setClickVolume(80);

    looper.begin();
    looper.setBars(app.bars);
    looper.setBpm(app.bpmX10);
    looper.setQuantize(app.quantize);
    looper.setSwing(app.swing);
    looper.setLoopMode(app.loopMode);
    looper.setSink(emitEvent);

    audio.start();

    usbMidi.setConnectCallback(onUsbConnect, nullptr);
    usbMidi.setInputCallback(onUsbMidi, nullptr);
    app.usbHostOk = usbMidi.begin();

    app.sdOk = library.begin();
    if (app.sdOk) {
        SD.mkdir(LOOP_DIR);
        refreshLibrary();
    }

    const int batt = M5.Power.getBatteryLevel();
    app.battery = (batt >= 0 && batt <= 100) ? (uint8_t)batt : 80;

    app.screen = app.sdOk ? Screen::LOOP : Screen::NO_SD;
    ui.boot(100, library.count());
    delay(1500);
    ui.render();

    if (!audioOk) setToast("AUDIO INIT FAILED");
    else if (!audio.codecOk()) setToast("NO CODEC");

    Serial.printf("[looper] audio=%d codec=%d sd=%d files=%d heap=%u\n",
                  audioOk ? 1 : 0, audio.codecOk() ? 1 : 0, app.sdOk ? 1 : 0,
                  library.count(), (unsigned)ESP.getFreeHeap());
}

void loop()
{
    M5Cardputer.update();
    syncApp();
    looper.service();

    // If the event buffer fills, surface it instead of silently truncating.
    static bool overflowWarned = false;
    if (looper.overflow() && !overflowWarned) {
        overflowWarned = true;
        setToast("EVENT LIMIT");
    }
    if (!looper.overflow()) overflowWarned = false;

    // USB unplug while recording: finish the take instead of leaving it stuck.
    if (!app.usbConnected &&
        (looper.state() == LoopState::REC || looper.state() == LoopState::ARMED)) {
        looper.stopRecord();
        usbMidi.panic();
        app.countInLeft = 0;
        setToast("USB LOST");
    }

    if (app.countInLeft) {
        if (looper.state() != LoopState::ARMED) {
            app.countInLeft = 0;
        } else if ((int32_t)(millis() - app.countInNext) >= 0) {
            audio.triggerClick(app.countInLeft == LOOP_BEATS);
            app.countInNext += app.countInInterval ? app.countInInterval : 500;
            if (app.countInLeft) --app.countInLeft;
        }
    }

    handleSerial();
    handleKeys();
    syncApp();

    app.usbDeviceSeen = usbMidi.deviceSeen();

    static uint32_t lastBat = 0;
    if (millis() - lastBat > 10000) {
        lastBat = millis();
        const int b = M5.Power.getBatteryLevel();
        if (b >= 0 && b <= 100) app.battery = (uint8_t)b;
    }

    static uint32_t lastCounts = 0;
    if (millis() - lastCounts >= 100) {
        lastCounts = millis();
        looper.laneCounts(app.laneCount);
        for (int i = 0; i < 4; ++i) app.laneMuted[i] = looper.mute((uint8_t)i);
    }

    // 30 fps so the recording lanes can visibly pulse with incoming notes.
    static uint32_t lastRender = 0;
    if (millis() - lastRender >= 33) {
        lastRender = millis();
        ui.render();
    }

    delay(2);
}
