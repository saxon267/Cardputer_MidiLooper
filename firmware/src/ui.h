#pragma once
#include <M5Cardputer.h>
#include <M5GFX.h>
#include "app.h"

class UI {
public:
    void begin();
    void boot(uint8_t pct, int files);
    void render();

private:
    M5Canvas _c{&M5Cardputer.Display};

    void text(int x, int y, const char* s, uint16_t col, bool bold = false, int scale = 1);
    void textf(int x, int y, uint16_t col, bool bold, int scale, const char* fmt, ...);
    void textR(int right, int y, const char* s, uint16_t col, bool bold = false, int scale = 1);
    void textC(int cx, int y, const char* s, uint16_t col, bool bold = false, int scale = 1);
    int  tw(const char* s, int scale = 1) const;

    void rect(int x, int y, int w, int h, uint16_t col) { _c.fillRect(x, y, w, h, col); }
    void frame(int x, int y, int w, int h, uint16_t col) { _c.drawRect(x, y, w, h, col); }
    void hline(int x, int y, int w, uint16_t col) { _c.drawFastHLine(x, y, w, col); }
    void vline(int x, int y, int h, uint16_t col) { _c.drawFastVLine(x, y, h, col); }

    void topbar();
    void tab(int x, const char* label, bool active);
    void dot(int x, int y, uint16_t col);
    void battery(int x, int y, uint8_t pct);
    void iconPlay(int x, int y, uint16_t col);
    void iconStop(int x, int y, uint16_t col);
    void iconRec(int x, int y, uint16_t col);
    void iconFolder(int x, int y, uint16_t col);
    void iconNote(int x, int y, uint16_t col);
    void laneRow(int i, int y);
    void progress(int y);

    void drawLoop();
    void drawFiles();
    void drawSettings();
    void drawDelete();
    void drawRename();
    void drawNoSd();
    void drawToast();
};

extern UI ui;
