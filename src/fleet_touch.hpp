#pragma once
#include "panel_layout.hpp"
#include <cstdlib>

namespace panel {
enum class Contact { None, Down, Up, Cancel };
struct TouchRetry {
    uint32_t checkedAt=0;
    bool failed(bool ioError,uint32_t now) {
        errors=ioError?errors+1:0;
        if(errors<3) return false;
        errors=0; checkedAt=now;
        return true;
    }
    bool due(uint32_t now) {
        if(now-checkedAt<5000U) return false;
        checkedAt=now;
        return true;
    }
private:
    unsigned errors=0;
};
struct Gesture {
    int x = 0, y = 0, startOffset = 0;
    bool held = false, moved = false, scrollable = false, cancelled = false;
    void cancel() { if (held) cancelled = true; }
    // Commit on release so a drag cannot activate the card or button beneath it.
    bool update(Contact event, int px, int py, int limit, int &offset) {
        if (event == Contact::None) return false;
        if (event == Contact::Cancel) { cancelled = true; return false; }
        if (event == Contact::Up) {
            const bool tap = held && !moved && !cancelled;
            held = cancelled = false;
            return tap;
        }
        if (cancelled) return false;
        if (!held) {
            held = true; moved = false; x = px; y = py; startOffset = offset;
            scrollable = limit > 0 && y >= headerHeight && y < screenBottom;
        } else {
            if (std::abs(px - x) >= 10 || std::abs(py - y) >= 10) moved = true;
            if (moved && scrollable) {
                const int next = startOffset + y - py;
                offset = next < 0 ? 0 : next > limit ? limit : next;
            }
        }
        return false;
    }
};
}
