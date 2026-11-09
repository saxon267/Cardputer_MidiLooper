#include "ui.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "font8.h"
#include "library.h"
#include "loop.h"

UI ui;

// ---------------------------------------------------------------- primitives
void UI::begin()
{
    _c.setColorDepth(16);
    _c.createSprite(SCR_W, SCR_H);
    _c.fillSprite(C_BG);
}

int UI::tw(const char* s, int scale) const
{
    return (int)strlen(s) * 6 * scale;
}

void UI::text(int x, int y, const char* s, uint16_t col, bool bold, int scale)
{
    const uint8_t (*tbl)[8] = bold ? FONT8B : FONT8;
    int cx = x;
    for (const char* p = s; *p; ++p) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 95) c = '?';
        const uint8_t* g = tbl[c - 32];
        for (int row = 0; row < 8; ++row) {
            const uint8_t bits = g[row];
            for (int b = 0; b < 6; ++b) {
                if (bits & (1 << b)) {
                    _c.fillRect(cx + b * scale, y + row * scale, scale, scale, col);
                }
            }
        }
        cx += 6 * scale;
        if (cx > SCR_W) break;
    }
}

void UI::textf(int x, int y, uint16_t col, bool bold, int scale, const char* fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    text(x, y, buf, col, bold, scale);
}

void UI::textR(int right, int y, const char* s, uint16_t col, bool bold, int scale)
{
    text(right - tw(s, scale), y, s, col, bold, scale);
}

void UI::textC(int cx, int y, const char* s, uint16_t col, bool bold, int scale)
{
    text(cx - tw(s, scale) / 2, y, s, col, bold, scale);
}

void UI::dot(int x, int y, uint16_t col)
{
    rect(x, y, 5, 5, col);
    rect(x + 1, y + 1, 1, 1, C_BAR);
}

void UI::battery(int x, int y, uint8_t pct)
{
    frame(x, y, 14, 7, C_FAINT);
    rect(x + 14, y + 2, 2, 3, C_FAINT);
    int n = (int)(10u * (pct > 100 ? 100 : pct) / 100u);
    rect(x + 2, y + 2, n, 3, pct > 25 ? C_GREEN : C_RED);
}

void UI::iconPlay(int x, int y, uint16_t col)
{
    _c.fillTriangle(x, y, x, y + 8, x + 8, y + 4, col);
}

void UI::iconStop(int x, int y, uint16_t col)
{
    rect(x, y, 8, 8, col);
}

void UI::iconRec(int x, int y, uint16_t col)
{
    rect(x, y, 8, 8, col);
}

void UI::iconFolder(int x, int y, uint16_t col)
{
    rect(x, y + 1, 9, 6, col);
    rect(x, y, 4, 2, col);
    rect(x + 2, y + 3, 5, 1, C_BAR);
}

void UI::iconNote(int x, int y, uint16_t col)
{
    rect(x + 4, y, 1, 8, col);
    rect(x + 4, y, 3, 1, col);
    rect(x + 1, y + 5, 3, 3, col);
    rect(x, y + 6, 1, 1, col);
}

// ---------------------------------------------------------------- chrome
void UI::tab(int x, const char* label, bool active)
{
    rect(x, 2, 42, 9, active ? C_ACC : C_PAD);
    frame(x, 2, 42, 9, active ? C_ACC : C_EDGE);
    text(x + 4, 3, label, active ? C_BG : C_DIM, true);
}

void UI::topbar()
{
    rect(0, 0, SCR_W, TOP_H, C_BAR);
    hline(0, TOP_H - 1, SCR_W, C_LINE);

    tab(4, "LOOP", app.screen == Screen::LOOP);
    tab(48, "FILE", app.screen == Screen::FILES);

    uint16_t uc = !app.usbHostOk ? C_RED
                : app.usbConnected ? C_GREEN
                : app.usbDeviceSeen ? C_AMB : C_FAINT;
    dot(150, 5, uc);
    text(158, 3, "USB", C_DIM);
    dot(184, 5, app.sdOk ? C_AQUA : C_RED);
    text(192, 3, "SD", C_DIM);
    battery(214, 4, app.battery);
}

// ---------------------------------------------------------------- LOOP
void UI::progress(int y)
{
    const int px = 6, pw = 228, ph = 7;
    frame(px - 1, y - 1, pw + 2, ph + 2, C_EDGE);
    rect(px, y, pw, ph, C_LINE);

    int p = app.phasePermille;
    if (p < 0) p = 0;
    if (p > 1000) p = 1000;
    const int fw = pw * p / 1000;

    if (app.loopState == LoopState::REC)       rect(px, y, fw, ph, C_RED);
    else if (app.loopState == LoopState::PLAY) rect(px, y, fw, ph, C_ACC);
    else if (app.hasLoop)                      rect(px, y, fw, ph, C_ACC_DIM);

    const int beats = (int)app.bars * LOOP_BEATS;
    for (int i = 0; i <= beats; ++i) {
        const int x = px + (beats ? i * pw / beats : 0);
        const bool bar = (i % LOOP_BEATS) == 0;
        rect(x, y, 1, bar ? ph : 3, bar ? C_FAINT : C_BG);
    }

    const int hx = px + fw;
    rect(hx - 1, y - 2, 2, ph + 4, C_TXT);
}

void UI::laneRow(int i, int y)
{
    static const uint8_t chs[4]   = {1, 2, 3, 10};
    static const char*   names[4] = {"LEAD", "PAD", "BASS", "DRUM"};
    static const uint16_t cols[4] = {C_ACC, C_AQUA, C_AMB, C_BLUE};

    const bool sel = (app.selectedLane == i);
    if (sel) {
        rect(2, y, 236, 17, C_PAD);
        rect(2, y, 2, 17, cols[i]);
    }

    textf(7, y + 5, cols[i], true, 1, "CH%d", chs[i]);
    text(37, y + 5, names[i], sel ? C_TXT : C_DIM, sel);

    const int mx = 68, my = y + 5, cw = 5, cg = 1;
    rect(mx, my, 18 * (cw + cg) - cg, 6, C_LINE);
    int lit = (int)app.laneCount[i] * 18 / 72;
    if (lit > 18) lit = 18;
    for (int s = 0; s < 18; ++s) {
        rect(mx + s * (cw + cg), my, cw, 6, s < lit ? cols[i] : C_LINE);
    }

    if (app.laneCount[i]) {
        char cnt[8];
        snprintf(cnt, sizeof(cnt), "%u", (unsigned)app.laneCount[i]);
        textR(204, y + 5, cnt, C_DIM);
    } else {
        textR(204, y + 5, "--", C_DIM);
    }
    text(217, y + 5, "ON", C_AQUA);
}

void UI::drawLoop()
{
    const int y = 17;
    const char* label = "STOP";
    uint16_t col = C_FAINT;

    switch (app.loopState) {
        case LoopState::REC:
            label = app.overdub ? "OVERDUB" : "REC";
            col = app.overdub ? C_ACC : C_RED;
            break;
        case LoopState::PLAY:
            label = "PLAY";
            col = C_GREEN;
            break;
        case LoopState::ARMED:
            label = "ARMED";
            col = C_AMB;
            break;
        default:
            break;
    }

    if (app.loopState == LoopState::REC)      iconRec(7, y, C_RED);
    else if (app.loopState == LoopState::PLAY) iconPlay(7, y, C_GREEN);
    else if (app.loopState == LoopState::ARMED) {
        frame(7, y, 8, 8, C_AMB);
        rect(9, y + 2, 4, 4, C_AMB);
    } else {
        iconStop(7, y, C_FAINT);
    }

    text(20, y + 1, label, col, true);
    textf(96, y + 1, C_TXT, true, 1, "BAR %u/%u", app.barNow, app.bars);
    char bpm[12];
    snprintf(bpm, sizeof(bpm), "%u BPM", app.bpmX10 / 10);
    textR(234, y + 1, bpm, C_DIM);

    progress(31);
    for (int i = 0; i < 4; ++i) laneRow(i, 44 + i * 18);

    rect(0, BOT_Y, SCR_W, BOT_H, C_BAR);
    hline(0, BOT_Y - 1, SCR_W, C_LINE);

    const char* hint;
    if (app.settings)      hint = "UP/DN SELECT   </> CHANGE   ESC";
    else if (app.loopState == LoopState::ARMED) hint = "WAIT MIDI...   R CANCEL";
    else if (app.loopState == LoopState::REC)   hint = "R STOP   ENT PLAY   S SAVE";
    else if (app.loopState == LoopState::PLAY)  hint = "R OVERDUB   ENT STOP   S SAVE";
    else                                        hint = "R REC   ENT PLAY   S SAVE";
    text(6, BOT_Y + 1, hint, C_FAINT);
    if (!app.settings) textR(234, BOT_Y + 1, app.hasLoop ? "U UNDO" : "TAB FILE", C_FAINT);
}

// ---------------------------------------------------------------- FILES
void UI::drawFiles()
{
    rect(0, 13, SCR_W, 11, C_PANEL);
    hline(0, 24, SCR_W, C_LINE);
    iconFolder(5, 16, C_AMB);
    text(17, 16, LOOP_DIR, C_BLUE, true);
    textf(234 - tw("00 FILES"), 16, C_FAINT, false, 1, "%d FILES", library.count());

    if (library.count() == 0) {
        textC(SCR_W / 2, 58, "NO .MID FILES", C_FAINT, true);
        textC(SCR_W / 2, 71, "PUT FILES IN /MIDILOOP", C_FAINT);
    } else {
        const int y0 = 27;
        for (int i = 0; i < 6; ++i) {
            const int idx = app.scroll + i;
            if (idx >= library.count()) break;
            const FileEntry& e = library.at(idx);
            const int y = y0 + i * 16;
            const bool sel = (idx == app.sel);
            if (sel) rect(0, y, SCR_W, 16, C_ACC);
            iconNote(6, y + 4, sel ? C_BG : C_AMB);

            char name[28];
            strncpy(name, e.name, sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            if (strlen(e.name) > 26) {
                strncpy(name, e.name, 25);
                name[25] = 0;
                strcat(name, "~");
            }
            text(18, y + 4, name, sel ? C_BG : C_TXT, sel);

            char sz[12];
            if (e.size >= 1024u * 1024u) snprintf(sz, sizeof(sz), "%uM", (unsigned)(e.size / (1024u * 1024u)));
            else                         snprintf(sz, sizeof(sz), "%uK", (unsigned)((e.size + 1023u) / 1024u));
            textR(230, y + 4, sz, sel ? C_BG : C_FAINT);
        }
    }

    rect(0, BOT_Y, SCR_W, BOT_H, C_BAR);
    hline(0, BOT_Y - 1, SCR_W, C_LINE);
    text(6, BOT_Y + 1, "D DEL   N RENAME", C_FAINT);
    textR(234, BOT_Y + 1, "TAB LOOP", C_FAINT);
}

// ---------------------------------------------------------------- modals
void UI::drawSettings()
{
    rect(0, 13, SCR_W, SCR_H - 13, C_BG);
    frame(28, 16, 184, 104, C_EDGE);
    rect(29, 17, 182, 102, C_PANEL);
    textC(SCR_W / 2, 21, "SETTINGS", C_AMB, true);

    const char* labels[6] = {"BPM", "BARS", "QUANT", "SWING", "CLICK", "COUNT-IN"};
    static const char* qnames[5] = {"OFF", "1/4", "1/8", "1/16", "1/32"};
    char values[6][12];
    snprintf(values[0], sizeof(values[0]), "%u", app.bpmX10 / 10);
    snprintf(values[1], sizeof(values[1]), "%u", app.bars);
    snprintf(values[2], sizeof(values[2]), "%s",
             qnames[app.quantize <= Q_1_32 ? app.quantize : Q_1_16]);
    snprintf(values[3], sizeof(values[3]), "%u", app.swing);
    snprintf(values[4], sizeof(values[4]), "%s", app.click ? "ON" : "OFF");
    snprintf(values[5], sizeof(values[5]), "%s", app.countIn ? "1 BAR" : "OFF");

    for (int i = 0; i < 6; ++i) {
        const int y = 32 + i * 12;
        if (i == app.settingsRow) {
            rect(32, y - 2, 176, 12, C_PAD);
            rect(32, y - 2, 2, 12, C_AMB);
        }
        text(36, y, labels[i], i == app.settingsRow ? C_AMB : C_DIM, i == app.settingsRow);
        textR(176, y, values[i], C_TXT, true);
        text(184, y, "< >", C_FAINT);
    }
    textC(SCR_W / 2, 108, "UP/DN  LEFT/RIGHT  ESC", C_FAINT);
}

void UI::drawDelete()
{
    rect(0, 13, SCR_W, SCR_H - 13, C_BG);
    frame(36, 38, 168, 60, C_EDGE);
    rect(37, 39, 166, 58, C_PANEL);
    textC(SCR_W / 2, 44, "DELETE FILE?", C_RED, true);
    if (app.sel >= 0 && app.sel < library.count()) {
        textC(SCR_W / 2, 58, library.at(app.sel).name, C_TXT);
    }
    textC(SCR_W / 2, 72, "ENTER YES", C_AMB, true);
    textC(SCR_W / 2, 84, "BKSP NO", C_DIM);
}

void UI::drawRename()
{
    rect(0, 13, SCR_W, SCR_H - 13, C_BG);
    frame(30, 32, 180, 72, C_EDGE);
    rect(31, 33, 178, 70, C_PANEL);
    textC(SCR_W / 2, 37, "RENAME FILE", C_AMB, true);
    textC(SCR_W / 2, 50, app.renameOld, C_FAINT);
    textC(SCR_W / 2, 64, app.renameBuf, C_TXT, true);
    textC(SCR_W / 2, 78, "ENTER OK", C_AMB, true);
    textC(SCR_W / 2, 90, "BKSP DEL   TAB NO", C_DIM);
}

void UI::drawNoSd()
{
    textC(SCR_W / 2, 45, "NO SD CARD", C_RED, true);
    textC(SCR_W / 2, 60, "INSERT CARD", C_DIM);
    textC(SCR_W / 2, 80, "ENTER RETRY", C_FAINT);
}

void UI::drawToast()
{
    if (!app.toast[0]) return;
    if ((int32_t)(millis() - app.toastUntil) >= 0) return;
    const int w = tw(app.toast) + 12;
    const int x = (SCR_W - w) / 2;
    rect(x, 108, w, 12, C_AMB);
    text(x + 6, 110, app.toast, C_BG, true);
}

// ---------------------------------------------------------------- boot/render
void UI::boot(uint8_t pct, int files)
{
    _c.fillSprite(C_BG);
    textC(SCR_W / 2, 20, "MIDI", C_ACC, true, 3);
    textC(SCR_W / 2, 48, "LOOP", C_TXT, true, 3);
    textC(SCR_W / 2, 78, "USB HOST", C_DIM, true);
    textC(SCR_W / 2, 90, APP_VERSION, C_FAINT);

    frame(60, 104, 120, 8, C_EDGE);
    rect(61, 105, 118, 6, C_LINE);
    rect(61, 105, 118 * (pct > 100 ? 100 : pct) / 100, 6, C_ACC);
    textC(SCR_W / 2, 118, files > 0 ? "READY" : "NO FILES", C_FAINT);

    _c.pushSprite(0, 0);
}

void UI::render()
{
    _c.fillSprite(C_BG);
    topbar();

    if (app.screen == Screen::LOOP)       drawLoop();
    else if (app.screen == Screen::FILES) drawFiles();
    else                                  drawNoSd();

    if (app.settings)      drawSettings();
    if (app.confirmDelete) drawDelete();
    if (app.renaming)      drawRename();
    drawToast();

    _c.pushSprite(0, 0);
}
