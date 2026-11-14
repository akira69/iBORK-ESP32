#pragma once
#include <cstdint>
#include "fleet_touch.hpp"

namespace panel {
constexpr int minManualBrightness=10;
constexpr int savedManualBrightness(int value) {
    return value<=0 || value>100?0:value<minManualBrightness?minManualBrightness:value;
}
struct ScreenWake {
    uint32_t touchedAt=0;
    int manualBrightness=0;
    bool active=false, held=false, consuming=false;
    bool awake(uint32_t now) {
        if(active && uint32_t(now-touchedAt)>=300000U) active=false;
        return active;
    }
    bool touch(Contact event,int brightness,uint32_t now) {
        if(manualBrightness>0) return false;
        if(event==Contact::Down) {
            consuming=consuming || brightness<100;
            touchedAt=now; active=held=true;
        }
        const bool skip=consuming;
        if(event==Contact::Up) {
            if(held) touchedAt=now;
            held=consuming=false;
        }
        return skip;
    }
    int level(int scheduled,uint32_t now) {
        const int manual=savedManualBrightness(manualBrightness);
        return manual?manual:awake(now)?100:scheduled;
    }
};

struct BrightnessSlider {
    static constexpr int left=40, right=550, top=180, bottom=236;
    bool held=false, cancelled=false;
    bool update(Contact event,int x,int y,int &level,int minimum=minManualBrightness,int offset=0) {
        if(event==Contact::None) return false;
        if(event==Contact::Up) {
            const bool consumed=held;
            held=cancelled=false;
            return consumed;
        }
        if(event==Contact::Cancel) { cancelled=true; return held; }
        if(cancelled) return held;
        if(!held) held=x>=left && x<=right && y>=top+offset && y<bottom+offset;
        if(!held) { cancelled=true; return false; }
        x=x<left?left:x>right?right:x;
        level=minimum+((x-left)*(100-minimum)+(right-left)/2)/(right-left);
        return true;
    }
};
}
