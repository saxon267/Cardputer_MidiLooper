#include "usb_midi_host.h"

#if USB_MIDI_HOST
#include <Arduino.h>
#include <esp_intr_alloc.h>
#include <esp_timer.h>
#include <string.h>
#include <usb/usb_helpers.h>
#include <usb/usb_host.h>

UsbMidiHost usbMidi;

bool UsbMidiHost::begin()
{
    usb_host_config_t hostConfig{};
    hostConfig.skip_phy_setup = false;
    hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&hostConfig) != ESP_OK) return false;

    usb_host_client_config_t clientConfig{};
    clientConfig.is_synchronous = false;
    clientConfig.max_num_event_msg = 8;
    clientConfig.async.client_event_callback = clientCb;
    clientConfig.async.callback_arg = this;
    if (usb_host_client_register(&clientConfig, &_client) != ESP_OK) {
        usb_host_uninstall();
        _client = nullptr;
        return false;
    }
    if (xTaskCreatePinnedToCore(taskEntry, "usb-midi", 6144, this, 5, (TaskHandle_t*)&_task, 0) != pdPASS) {
        usb_host_client_deregister(_client);
        _client = nullptr;
        usb_host_uninstall();
        return false;
    }
    return true;
}

void UsbMidiHost::taskEntry(void* arg)
{
    static_cast<UsbMidiHost*>(arg)->taskLoop();
}

void UsbMidiHost::taskLoop()
{
    while (true) {
        poll();
        vTaskDelay(1);
    }
}

void UsbMidiHost::clientCb(const usb_host_client_event_msg_t* msg, void* arg)
{
    if (!msg || !arg) return;
    auto* self = static_cast<UsbMidiHost*>(arg);
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (!self->_dev) self->openDevice(msg->new_dev.address);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        if (self->_dev && msg->dev_gone.dev_hdl == self->_dev) {
            self->_seen = false;
            self->closeDevice();
        }
    }
}

void UsbMidiHost::transferCb(usb_transfer_t* t)
{
    if (t && t->context) static_cast<UsbMidiHost*>(t->context)->_inFlight = false;
}

void UsbMidiHost::inputCb(usb_transfer_t* t)
{
    if (t && t->context) static_cast<UsbMidiHost*>(t->context)->handleInput(*t);
}

void UsbMidiHost::handleInput(usb_transfer_t& t)
{
    if (t.status == USB_TRANSFER_STATUS_COMPLETED) {
        for (int off = 0; off + 3 < t.actual_num_bytes; off += 4) {
            const uint8_t* p = t.data_buffer + off;
            if (p[0] == 0 && p[1] == 0) continue;
            if (p[1] == 0xF8) {
                clockPulse();
            } else if (p[1] >= 0x80 && p[1] < 0xF0 && _inCb) {
                ++_rx;
                _inCb(p[1], p[2], p[3], _inCtx);
            }
        }
        if (_dev) usb_host_transfer_submit(&t);
    } else if (t.status != USB_TRANSFER_STATUS_CANCELED && t.status != USB_TRANSFER_STATUS_NO_DEVICE) {
        if (_dev) usb_host_transfer_submit(&t);
    }
}

void UsbMidiHost::clockPulse()
{
    int64_t now = esp_timer_get_time();
    if (_lastClockUs != 0) {
        int64_t dt = now - _lastClockUs;
        if (dt > 3000 && dt < 200000) {
            uint32_t b = (uint32_t)(25000000ull / (uint32_t)dt);
            if (!_clockValid) _bpmX10 = (uint16_t)b;
            else              _bpmX10 = (uint16_t)((_bpmX10 * 3u + b) / 4u);
            _clockValid = true;
        }
    }
    _lastClockUs = now;
}

float UsbMidiHost::bpm() const
{
    if (!_clockValid) return 0.0f;
    if (esp_timer_get_time() - _lastClockUs > 500000) return 0.0f;
    return _bpmX10 / 10.0f;
}

bool UsbMidiHost::clockValid() const
{
    return _clockValid && (esp_timer_get_time() - _lastClockUs <= 500000);
}

uint16_t UsbMidiHost::activeNotes() const
{
    uint16_t n = 0;
    for (int ch = 0; ch < 16; ++ch) {
        for (int w = 0; w < 4; ++w) {
            uint32_t v = _on[ch][w];
            while (v) { ++n; v &= v - 1; }
        }
    }
    return n;
}

void UsbMidiHost::poll()
{
    if (!_client) return;
    usb_host_client_handle_events(_client, 0);
    uint32_t flags = 0;
    usb_host_lib_handle_events(0, &flags);
    submitNext();
}

void UsbMidiHost::openDevice(uint8_t address)
{
    if (usb_host_device_open(_client, address, &_dev) != ESP_OK) return;
    _seen = true;
    _midiFound = false;
    _head = _tail = 0;
    _clockValid = false;
    _lastClockUs = 0;
    _rx = 0;
    memset(_on, 0, sizeof(_on));

    const usb_device_desc_t* dd = nullptr;
    if (usb_host_get_device_descriptor(_dev, &dd) == ESP_OK && dd) {
        _vid = dd->idVendor;
        _pid = dd->idProduct;
    }

    const usb_config_desc_t* cfg = nullptr;
    if (usb_host_get_active_config_descriptor(_dev, &cfg) != ESP_OK || !cfg) {
        closeDevice();
        return;
    }
    parseConfig(cfg);
    if (!_connected) closeDevice();
}

void UsbMidiHost::parseConfig(const usb_config_desc_t* cfg)
{
    const uint8_t* p = cfg->val;
    const uint8_t* end = p + cfg->wTotalLength;
    bool inMidi = false;
    bool claimed = false;
    uint8_t iface = 0, alt = 0;

    while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
        if (p[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const auto* id = reinterpret_cast<const usb_intf_desc_t*>(p);
            if (p[0] >= sizeof(usb_intf_desc_t)) {
                inMidi = (id->bInterfaceClass == 1 && id->bInterfaceSubClass == 3);
                iface = id->bInterfaceNumber;
                alt = id->bAlternateSetting;
            }
        } else if (p[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && inMidi) {
            if (p[0] < sizeof(usb_ep_desc_t)) break;
            const auto* ep = reinterpret_cast<const usb_ep_desc_t*>(p);
            const bool isOut = (ep->bEndpointAddress & 0x80) == 0;
            const uint8_t type = ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK;
            const bool bulk = type == USB_BM_ATTRIBUTES_XFER_BULK || type == USB_BM_ATTRIBUTES_XFER_INT;
            if (!bulk || !ep->wMaxPacketSize) { p += p[0]; continue; }

            if (!claimed) {
                if (usb_host_interface_claim(_client, _dev, iface, alt) != ESP_OK) {
                    p += p[0];
                    continue;
                }
                claimed = true;
                _iface = iface;
                _alt = alt;
            }

            if (isOut && !_out) {
                if (usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &_out) == ESP_OK) {
                    _ep = ep->bEndpointAddress;
                    _pkt = ep->wMaxPacketSize > MAX_XFER ? MAX_XFER : ep->wMaxPacketSize;
                    _out->device_handle = _dev;
                    _out->bEndpointAddress = _ep;
                    _out->callback = transferCb;
                    _out->context = this;
                    _midiFound = true;
                }
            } else if (!isOut && !_in) {
                if (usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &_in) == ESP_OK) {
                    _inEp = ep->bEndpointAddress;
                    _inPkt = ep->wMaxPacketSize > MAX_XFER ? MAX_XFER : ep->wMaxPacketSize;
                    _in->device_handle = _dev;
                    _in->bEndpointAddress = _inEp;
                    _in->callback = inputCb;
                    _in->context = this;
                    _in->num_bytes = _inPkt;
                    if (usb_host_transfer_submit(_in) != ESP_OK) {
                        usb_host_transfer_free(_in);
                        _in = nullptr;
                    }
                }
            }
        }
        p += p[0];
    }

    if (_out) {
        _connected = true;
        fire(true);
    }
}

void UsbMidiHost::closeDevice()
{
    const bool was = _connected;
    _connected = false;
    _inFlight = false;
    _clockValid = false;
    if (_out) {
        usb_host_transfer_free(_out);
        _out = nullptr;
    }
    if (_in) {
        usb_host_transfer_free(_in);
        _in = nullptr;
    }
    if (_dev) {
        if (_midiFound) usb_host_interface_release(_client, _dev, _iface);
        usb_host_device_close(_client, _dev);
        _dev = nullptr;
    }
    _midiFound = false;
    _ep = 0;
    _inEp = 0;
    _head = _tail = 0;
    memset(_on, 0, sizeof(_on));
    if (was) fire(false);
}

void UsbMidiHost::fire(bool c)
{
    if (_cb) _cb(c, _cbCtx);
}

void UsbMidiHost::push(const uint8_t pkt[4])
{
    uint16_t next = (_tail + 1) % QSIZE;
    if (next == _head) return;  // full: drop oldest-safe
    memcpy(_q[_tail], pkt, 4);
    _tail = next;
}

bool UsbMidiHost::pop(uint8_t pkt[4])
{
    if (_head == _tail) return false;
    memcpy(pkt, _q[_head], 4);
    _head = (_head + 1) % QSIZE;
    return true;
}

void UsbMidiHost::submitNext()
{
    if (!_connected || !_out || _inFlight) return;

    int max = _pkt & ~3;
    if (max < 4) return;
    int n = 0;
    uint8_t pkt[4];
    while (n + 4 <= max && pop(pkt)) {
        memcpy(_out->data_buffer + n, pkt, 4);
        n += 4;
    }
    if (!n) return;

    _out->num_bytes = n;
    _inFlight = true;
    if (usb_host_transfer_submit(_out) != ESP_OK) _inFlight = false;
}

void UsbMidiHost::send2(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t p[4] = {cin, status, d1, d2};
    push(p);
}

void UsbMidiHost::noteOn(uint8_t ch, uint8_t note, uint8_t vel)
{
    ch &= 0x0F;
    note &= 0x7F;
    if (vel == 0) {
        noteOff(ch, note);
        return;
    }
    _on[ch][note >> 5] |= (1u << (note & 31));
    send2(0x09, (uint8_t)(0x90 | ch), note, vel & 0x7F);
}

void UsbMidiHost::noteOff(uint8_t ch, uint8_t note)
{
    ch &= 0x0F;
    note &= 0x7F;
    _on[ch][note >> 5] &= ~(1u << (note & 31));
    send2(0x08, (uint8_t)(0x80 | ch), note, 0);
}

void UsbMidiHost::controlChange(uint8_t ch, uint8_t cc, uint8_t value)
{
    send2(0x0B, (uint8_t)(0xB0 | (ch & 0x0F)), cc & 0x7F, value & 0x7F);
}

void UsbMidiHost::pitchBend(uint8_t ch, uint16_t value)
{
    value &= 0x3FFF;
    send2(0x0E, (uint8_t)(0xE0 | (ch & 0x0F)), (uint8_t)(value & 0x7F),
          (uint8_t)((value >> 7) & 0x7F));
}

void UsbMidiHost::allNotesOff(uint8_t ch)
{
    ch &= 0x0F;
    for (int n = 0; n < 128; ++n) {
        if (_on[ch][n >> 5] & (1u << (n & 31))) {
            // Send both legal note-off forms. Some receivers only implement
            // Note On velocity 0; Sloop accepts both, but this is belt and braces.
            noteOff(ch, (uint8_t)n);
            send2(0x09, (uint8_t)(0x90 | ch), (uint8_t)n, 0);
        }
    }
    _on[ch][0] = _on[ch][1] = _on[ch][2] = _on[ch][3] = 0;
    controlChange(ch, 123, 0);
}

void UsbMidiHost::panic()
{
    _lastPanicNotes = activeNotes();
    for (uint8_t ch = 0; ch < 16; ++ch) {
        allNotesOff(ch);
        controlChange(ch, 64, 0);
        controlChange(ch, 121, 0);
        controlChange(ch, 120, 0);
    }
}
#endif
