#pragma once
#include <stdint.h>

#if USB_MIDI_HOST
#include <usb/usb_host.h>

class UsbMidiHost {
public:
    using ConnectCallback = void (*)(bool connected, void* context);
    using InputCallback = void (*)(uint8_t status, uint8_t d1, uint8_t d2, void* context);

    bool begin();
    void poll();
    bool connected() const { return _connected; }
    bool midiFound() const { return _midiFound; }
    bool deviceSeen() const { return _seen; }
    uint16_t vid() const { return _vid; }
    uint16_t pid() const { return _pid; }
    uint8_t endpoint() const { return _ep; }
    uint8_t inputEndpoint() const { return _inEp; }

    void setConnectCallback(ConnectCallback cb, void* context)
    {
        _cb = cb;
        _cbCtx = context;
    }

    void setInputCallback(InputCallback cb, void* context)
    {
        _inCb = cb;
        _inCtx = context;
    }

    void noteOn(uint8_t ch, uint8_t note, uint8_t vel);
    void noteOff(uint8_t ch, uint8_t note);
    void controlChange(uint8_t ch, uint8_t cc, uint8_t value);
    void pitchBend(uint8_t ch, uint16_t value);
    void allNotesOff(uint8_t ch);
    void panic();
    float bpm() const;
    bool clockValid() const;
    uint16_t activeNotes() const;
    uint16_t lastPanicNotes() const { return _lastPanicNotes; }
    uint32_t inputCount() const { return _rx; }

private:
    static constexpr int QSIZE = 1024;
    static constexpr int MAX_XFER = 64;

    void push(const uint8_t pkt[4]);
    bool pop(uint8_t pkt[4]);
    void submitNext();
    void openDevice(uint8_t address);
    void parseConfig(const usb_config_desc_t* cfg);
    void closeDevice();
    void fire(bool connected);
    void send2(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2);
    void clockPulse();
    void handleInput(usb_transfer_t& t);

    static void clientCb(const usb_host_client_event_msg_t* msg, void* arg);
    static void transferCb(usb_transfer_t* t);
    static void inputCb(usb_transfer_t* t);
    static void taskEntry(void* arg);
    void taskLoop();

    usb_host_client_handle_t _client = nullptr;
    void*                    _task = nullptr;
    usb_device_handle_t      _dev    = nullptr;
    usb_transfer_t*          _out    = nullptr;
    usb_transfer_t*          _in     = nullptr;
    volatile bool            _inFlight = false;
    volatile bool            _connected = false;
    bool                     _midiFound = false;
    bool                     _seen = false;
    uint8_t                  _iface = 0;
    uint8_t                  _alt = 0;
    uint8_t                  _ep = 0;
    uint8_t                  _inEp = 0;
    uint8_t                  _pkt = MAX_XFER;
    uint8_t                  _inPkt = MAX_XFER;
    uint16_t                 _vid = 0;
    uint16_t                 _pid = 0;
    ConnectCallback          _cb = nullptr;
    void*                    _cbCtx = nullptr;
    InputCallback            _inCb = nullptr;
    void*                    _inCtx = nullptr;

    uint32_t                 _on[16][4] = {};
    uint32_t                 _rx = 0;
    volatile uint16_t        _bpmX10 = 0;
    volatile int64_t         _lastClockUs = 0;
    volatile bool            _clockValid = false;
    uint16_t                 _lastPanicNotes = 0;

    uint8_t                  _q[QSIZE][4];
    volatile uint16_t        _head = 0;
    volatile uint16_t        _tail = 0;
};

extern UsbMidiHost usbMidi;
#endif
