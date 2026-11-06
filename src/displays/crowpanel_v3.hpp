#pragma once
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <Wire.h>
#include "../fleet_touch.hpp"

namespace display_hw {
constexpr int widthPixels=800, heightPixels=480;

// DIS08070H V3.0 panel timings and PCA9557 reset sequence from Elecrow.
class Display : public lgfx::LGFX_Device {
    lgfx::Bus_RGB bus;
    lgfx::Panel_RGB panel;
public:
    Display() {
        auto b = bus.config();
        b.panel = &panel;
        b.pin_d0 = 15; b.pin_d1 = 7; b.pin_d2 = 6; b.pin_d3 = 5;
        b.pin_d4 = 4; b.pin_d5 = 9; b.pin_d6 = 46; b.pin_d7 = 3;
        b.pin_d8 = 8; b.pin_d9 = 16; b.pin_d10 = 1; b.pin_d11 = 14;
        b.pin_d12 = 21; b.pin_d13 = 47; b.pin_d14 = 48; b.pin_d15 = 45;
        b.pin_henable = 41; b.pin_vsync = 40; b.pin_hsync = 39; b.pin_pclk = 0;
        b.freq_write = 14000000;  // Leave PSRAM bandwidth for Wi-Fi alongside RGB scanout.
        b.hsync_front_porch = 40; b.hsync_pulse_width = 48; b.hsync_back_porch = 40;
        b.vsync_front_porch = 1; b.vsync_pulse_width = 31; b.vsync_back_porch = 13;
        b.hsync_polarity = 0; b.vsync_polarity = 0; b.pclk_active_neg = 1;
        b.de_idle_high = 0; b.pclk_idle_high = 0;
        bus.config(b);
        auto p = panel.config();
        p.panel_width = p.memory_width = widthPixels;
        p.panel_height = p.memory_height = heightPixels;
        panel.config(p);
        panel.setBus(&bus);
        setPanel(&panel);
    }
};

inline bool expanderWrite(uint8_t reg, uint8_t value) {
    Wire1.beginTransmission(0x18);
    Wire1.write(reg);
    Wire1.write(value);
    return Wire1.endTransmission() == 0;
}

inline bool initTouch() {
    Wire1.begin(19, 20, 400000);
    Wire1.setTimeOut(30);
    if (!expanderWrite(3, 0xff) || !expanderWrite(1, 0xfc) || !expanderWrite(3, 0xfc)) return false;
    delay(20);
    if (!expanderWrite(1, 0xfd)) return false;
    delay(100);
    if (!expanderWrite(3, 0xfe)) return false;
    Wire1.beginTransmission(0x5d);
    return Wire1.endTransmission() == 0;
}

inline panel::Contact readTouch(uint16_t &x, uint16_t &y) {
    using panel::Contact;
    constexpr uint8_t address = 0x5d;
    Wire1.beginTransmission(address);
    Wire1.write(0x81);
    Wire1.write(0x4e);
    if (Wire1.endTransmission(false) || Wire1.requestFrom(address, uint8_t(7)) != 7) return Contact::Cancel;
    uint8_t data[7];
    for (auto &byte : data) byte = Wire1.read();
    if (!(data[0] & 0x80)) return Contact::None;
    Wire1.beginTransmission(address);
    Wire1.write(0x81); Wire1.write(0x4e); Wire1.write(0);
    if (Wire1.endTransmission()) return Contact::Cancel;
    const unsigned contacts = data[0] & 0x0f;
    if (!contacts) return Contact::Up;
    x = data[2] | (data[3] << 8);
    y = data[4] | (data[5] << 8);
    return contacts == 1 && x < widthPixels && y < heightPixels ? Contact::Down : Contact::Cancel;
}

constexpr int backlightChannel=0;
inline bool initBacklight() {
    constexpr int backlightPin=2;
    if (ledcSetup(backlightChannel,5000,8)>0) {
        ledcWrite(backlightChannel,255);
        ledcAttachPin(backlightPin,backlightChannel);
        return true;
    }
    pinMode(backlightPin,OUTPUT);
    digitalWrite(backlightPin,HIGH);
    return false;
}
inline void setBrightness(int percent) {
    ledcWrite(backlightChannel,(percent*255+50)/100);
}
} // namespace display_hw
