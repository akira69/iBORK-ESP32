#pragma once
#include <cstdint>
#include "fleet_touch.hpp"

namespace panel {
struct ScreenWake {
    uint32_t touchedAt=0;
    bool active=false, held=false, consuming=false;
    bool awake(uint32_t now) {
        if(active && uint32_t(now-touchedAt)>=300000U) active=false;
        return active;
    }
    bool touch(Contact event,int brightness,uint32_t now) {
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
};
}
