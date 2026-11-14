#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace panel {
struct DisplayPeriod {
    int start, brightness;
};
struct DisplaySchedule {
    uint32_t revision;
    DisplayPeriod periods[4];
};

inline DisplaySchedule defaultDisplaySchedule() {
    return {1, {{60, 0}, {300, 25}, {420, 100}, {1140, 25}}};
}
inline bool validDisplaySchedule(const DisplaySchedule &schedule) {
    if (!schedule.revision) return false;
    for (int i=0; i<4; ++i) {
        const auto &period=schedule.periods[i];
        if (period.start<0 || period.start>=1440 || period.brightness<0 || period.brightness>100) return false;
        for (int j=0; j<i; ++j) if (period.start==schedule.periods[j].start) return false;
    }
    return true;
}
inline int localMinuteOfDay(time_t now) {
    struct tm local{};
    if (now<1704067200 || !localtime_r(&now,&local)) return -1;
    return local.tm_hour*60+local.tm_min;
}
inline void formatMinuteOfDay(char (&clock)[6], int minute) {
    if (minute<0 || minute>=1440) std::memcpy(clock,"--:--",sizeof(clock));
    else std::snprintf(clock,sizeof(clock),"%02d:%02d",minute/60,minute%60);
}
enum class ClockMode : uint8_t { Hour24, Hour12, Off };
struct ClockSettings {
    ClockMode mode=ClockMode::Hour24;
    uint8_t font=0, style=0;
    bool valid() const { return mode<=ClockMode::Off && font<2 && style<3; }
};
inline const char *formatClock(char (&clock)[6], int minute, ClockMode mode) {
    if(mode==ClockMode::Off) { clock[0]='\0'; return ""; }
    if(mode!=ClockMode::Hour12 || minute<0 || minute>=1440) {
        formatMinuteOfDay(clock,minute);
        return "";
    }
    const int hour=(minute/60+11)%12+1;
    formatMinuteOfDay(clock,hour*60+minute%60);
    return minute<720?"AM":"PM";
}
inline int brightnessAt(const DisplaySchedule &schedule, int minuteOfDay) {
    if (minuteOfDay<0 || minuteOfDay>=1440 || !validDisplaySchedule(schedule)) return 100;
    int closest=1440, brightness=100;
    for (const auto &period : schedule.periods) {
        const int minutesSinceStart=(minuteOfDay-period.start+1440)%1440;
        if (minutesSinceStart<closest) {
            closest=minutesSinceStart;
            brightness=period.brightness;
        }
    }
    return brightness;
}
}
