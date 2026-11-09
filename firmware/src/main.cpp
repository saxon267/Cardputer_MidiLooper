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
    switch (e.type) {
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
    looper.onMidi(status, d1, d2, (uint32_t)micros());
}

static void syncApp()
{
    app.loopState     = looper.state();
    app.hasLoop       = looper.hasLoop();
    app.overdub       = looper.isOverdub();
    app.takeFull      = looper.takeFull();
    app.overflow      = looper.overflow();
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
static void toggleRecord()
{
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

    looper.startFirst();
    app.countInLeft     = app.countIn ? LOOP_BEATS : 0;
    app.countInInterval = (uint16_t)(600000u / (app.bpmX10 ? app.bpmX10 : 1200u));
    app.countInNext     = millis();
    setToast(app.countIn ? "COUNT IN" : "WAIT MIDI");
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
        setToast("NO LOOP");
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
    setToast("CLEARED");
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

static void adjustSetting(int dir)
{
    switch (app.settingsRow) {
        case 0: adjustBpm(dir * 5); break;
        case 1: cycleBars(dir); break;
        case 2: toggleClick(); break;
        case 3: app.countIn = !app.countIn; prefs.putBool("cnt", app.countIn);
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
    static bool pD = false, pN = false, pMinus = false, pPlus = false;

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
    const bool ckey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('c');
    const bool dkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('d');
    const bool nkey  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('n');
    const bool minus = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('-');
    const bool plus  = !ks.fn && M5Cardputer.Keyboard.isKeyPressed('=');

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
    const bool eD = dkey && !pD;
    const bool eN = nkey && !pN;
    const bool eMinus = minus && !pMinus;
    const bool ePlus  = plus && !pPlus;

    pEnter = enter; pSpace = space; pDel = del; pTab = tab;
    pUp = up; pDown = down; pLeft = left; pRight = right;
    pR = rkey; pS = skey; pM = mkey; pG = gkey; pC = ckey;
    pD = dkey; pN = nkey; pMinus = minus; pPlus = plus;

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
        if (eUp)   app.settingsRow = (app.settingsRow + 3) % 4;
        if (eDown) app.settingsRow = (app.settingsRow + 1) % 4;
        if (eLeft) adjustSetting(-1);
        if (eRight) adjustSetting(+1);
        if (eEnter) {
            if (app.settingsRow >= 2) adjustSetting(+1);
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
        if (eM) toggleClick();
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

        if (M5Cardputer.Keyboard.isKeyPressed('1')) app.selectedLane = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('2')) app.selectedLane = 1;
        if (M5Cardputer.Keyboard.isKeyPressed('3')) app.selectedLane = 2;
        if (M5Cardputer.Keyboard.isKeyPressed('4')) app.selectedLane = 3;

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
        if (eEnter) setToast("D DEL  N RENAME");
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
            Serial.printf("[loop] state=%d bars=%u bpm=%u ev=%d take=%d phase=%u heap=%u\n",
                          (int)looper.state(), app.bars, app.bpmX10 / 10, looper.count(),
                          looper.takeCount(), (unsigned)looper.phasePermille(),
                          (unsigned)ESP.getFreeHeap());
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
    delay(180);
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

    static uint32_t lastRender = 0;
    if (millis() - lastRender >= 100) {
        lastRender = millis();
        looper.laneCounts(app.laneCount);
        ui.render();
    }

    delay(2);
}
