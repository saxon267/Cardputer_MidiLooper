#pragma once
#include "config.h"
#include "loop.h"

enum class Screen : uint8_t { BOOT, LOOP, FILES, NO_SD };

struct App {
    Screen   screen      = Screen::BOOT;
    LoopState loopState  = LoopState::STOP;

    uint8_t  selectedLane = 0;
    uint16_t bars         = 4;
    uint16_t bpmX10       = 1200;
    bool     click        = true;
    bool     countIn      = true;
    bool     settings     = false;
    uint8_t  settingsRow  = 0;

    bool     usbHostOk    = false;
    bool     usbDeviceSeen = false;
    bool     usbConnected = false;
    bool     sdOk         = false;
    uint8_t  battery      = 80;

    uint16_t laneCount[4] = {0, 0, 0, 0};
    uint16_t phasePermille = 0;
    uint16_t barNow       = 1;
    bool     hasLoop      = false;
    bool     overdub      = false;
    bool     takeFull     = false;
    bool     overflow     = false;

    char     toast[40]    = {0};
    uint32_t toastUntil   = 0;

    int      sel          = 0;
    int      scroll       = 0;
    bool     confirmDelete = false;

    bool     renaming     = false;
    char     renameBuf[32] = {0};
    char     renameExt[8] = ".MID";
    char     renameOld[40] = {0};

    char     lastSaved[64] = {0};
    bool     saveReq      = false;

    uint8_t  countInLeft  = 0;
    uint32_t countInNext  = 0;
    uint16_t countInInterval = 0;

    uint16_t fmBpmX10     = 0;
    bool     fmClock      = false;
};

extern App app;
