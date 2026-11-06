#pragma once
#include "panel_data.hpp"

enum class PanelStatus { Ok, Config, Clock, Network, Invalid, Denied, Limited, Unauthorized, Unsupported, Rejected, Stale };
enum class PanelRequest { Read, Power, ChangePin, Display };
namespace panel {
// Command endpoints return a short acknowledgement or error.
constexpr size_t responseCapacity(PanelRequest kind) {
    return kind==PanelRequest::Read ? maxSnapshotBytes : 1024;
}
inline bool validPin(const char *digits) {
    return digits && strlen(digits)>=6 && strlen(digits)<=12 && strspn(digits,"0123456789")==strlen(digits);
}
inline bool displayRequestBody(char (&body)[220], const DisplaySchedule &schedule,const char *pin) {
    if (!validPin(pin) || !validDisplaySchedule(schedule)) return false;
    char periods[4][sizeof("{\"start\":1439,\"brightness\":100}")];
    for (int i=0; i<4; ++i) {
        const auto &period=schedule.periods[i];
        snprintf(periods[i],sizeof(periods[i]),"{\"start\":%d,\"brightness\":%d}",period.start,period.brightness);
    }
    const int bytes=snprintf(body,sizeof(body),"{\"revision\":%lu,\"periods\":[%s,%s,%s,%s],\"pin\":\"%s\"}",
             static_cast<unsigned long>(schedule.revision),periods[0],periods[1],periods[2],periods[3],pin);
    return bytes>=0 && static_cast<size_t>(bytes)<sizeof(body);
}
inline const char *requestLine(PanelRequest kind) {
    switch(kind) {
        case PanelRequest::Read: return "GET /api/v1/panel";
        case PanelRequest::Power: return "POST /api/v1/panel/power";
        case PanelRequest::Display: return "PUT /api/v1/panel/display";
        case PanelRequest::ChangePin: return "POST /api/v1/panel/pin";
    }
    return "";
}
inline PanelStatus responseStatus(PanelRequest kind,unsigned code) {
    if(kind==PanelRequest::Display && code==409) return PanelStatus::Stale;
    if(kind==PanelRequest::ChangePin || kind==PanelRequest::Display) {
        if(code==401) return PanelStatus::Unauthorized;
        if(code==404 || code==405) return PanelStatus::Unsupported;
        if(code==400) return PanelStatus::Rejected;
    }
    if(code==401 || code==403) return PanelStatus::Denied;
    if(code==429) return PanelStatus::Limited;
    return code==200?PanelStatus::Ok:PanelStatus::Network;
}
}
bool panelConfigured();
void startPanelClock();
bool startPanelRead(const char *detailId=nullptr);
bool startPanelDisplay(const panel::DisplaySchedule &schedule,const char *pin);
bool startPanelPower(const char *id, const char *action, const char *pin);
bool startPanelPinChange(const char *currentPin,const char *newPin);
bool takePanelResult(PanelStatus &status, PanelRequest &kind, PanelData &data,bool &layoutChanged,bool &detailImageChanged);
