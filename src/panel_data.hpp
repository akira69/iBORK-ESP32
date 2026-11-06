#pragma once
#include "display_schedule.hpp"
#include "panel_layout.hpp"
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <cstdlib>
#include <memory>
#ifdef ARDUINO_ARCH_ESP32
#include <esp32-hal-psram.h>
#endif
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
struct FreeSidebarImage {
    void operator()(uint8_t *image) const { std::free(image); }
};
// The server limits device IDs to 32 ASCII characters and inventory to 16.
struct PanelDevice {
    char id[33], name[101], model[101];
    float watts;
    bool controls;
    char sidebarLabel[25];
    std::unique_ptr<uint8_t, FreeSidebarImage> sidebarImage;
    uint16_t sidebarBytes;
    panel::Sections widgets;
    bool metered;
};
struct PanelData {
    PanelDevice devices[16];
    int count, metered;
    panel::DisplaySchedule displaySchedule;
    bool canEditDisplay;
    panel::Sections home=panel::defaultHome;
    char detailId[33];
    std::unique_ptr<uint8_t,FreeSidebarImage> detailImage;
    uint16_t detailBytes=0;
    float temperature, fan, fanMax;
    int8_t powered=-1; // Unknown until a selected chassis reports on or off.
    time_t capturedAt;
    double usdPerKwh=-1;
    float dayHours, monthHours;
    float day[24], month[30], dayKwh, monthKwh;
};
namespace panel {
// Go JSON escaping can expand a full inventory with custom icons beyond 64 KiB.
constexpr size_t maxSnapshotBytes=84*1024;
// Telemetry can be repainted in place while the visible arrangement and artwork match.
inline bool samePanelLayout(const PanelData &a,const PanelData &b) {
    if(a.count!=b.count || a.metered!=b.metered || a.usdPerKwh!=b.usdPerKwh ||
       a.canEditDisplay!=b.canEditDisplay || a.displaySchedule.revision!=b.displaySchedule.revision ||
       a.home.count!=b.home.count || memcmp(a.home.items,b.home.items,a.home.count)) return false;
    for(int i=0;i<a.count;++i) {
        const auto &x=a.devices[i], &y=b.devices[i];
        if(strcmp(x.id,y.id) || strcmp(x.name,y.name) || strcmp(x.model,y.model) ||
           x.controls!=y.controls || x.metered!=y.metered || x.widgets.count!=y.widgets.count ||
           memcmp(x.widgets.items,y.widgets.items,x.widgets.count) || strcmp(x.sidebarLabel,y.sidebarLabel) ||
           x.sidebarBytes!=y.sidebarBytes || (x.sidebarBytes && memcmp(x.sidebarImage.get(),y.sidebarImage.get(),x.sidebarBytes)))
            return false;
    }
    return true;
}
enum class DetailPoll { Regular, Opened, FirstRead, Followup };
inline DetailPoll afterDetailRead(DetailPoll poll,const PanelData &data,const PanelDevice &selected) {
    if (poll!=DetailPoll::FirstRead) return poll;
    const bool missing=(selected.controls && selected.widgets.contains(Section::Controls) && data.powered<0) ||
                       (selected.widgets.contains(Section::Temperature) && !std::isfinite(data.temperature)) ||
                       (selected.widgets.contains(Section::Fan) && !std::isfinite(data.fan));
    return missing && !strcmp(data.detailId,selected.id) ? DetailPoll::Followup : DetailPoll::Regular;
}
inline int deviceIndex(const PanelData &data,const char *id) {
    for (int i=0;i<data.count;++i) if (!strcmp(data.devices[i].id,id)) return i;
    return -1;
}
inline bool validId(const char *id) {
    if (!id || !*id || strlen(id)>32 || *id=='-') return false;
    return strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789-")==strlen(id);
}
inline bool number(const cJSON *item, float &out) {
    if (cJSON_IsNull(item)) { out=-1; return true; }
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble<0 || item->valuedouble>1e12) return false;
    out=static_cast<float>(item->valuedouble);
    return true;
}
template<size_t N>
inline bool series(const cJSON *items, float (&out)[N]) {
    if (!cJSON_IsArray(items) || cJSON_GetArraySize(items)!=N) return false;
    size_t i=0;
    for (auto p=items->child;p;p=p->next) if (!number(p,out[i++])) return false;
    return true;
}
inline bool unsignedInteger(const cJSON *item, uint32_t max) {
    return cJSON_IsNumber(item) && item->valuedouble>=0 && item->valuedouble<=max &&
           std::trunc(item->valuedouble)==item->valuedouble;
}
inline DisplaySchedule decodeSchedule(const cJSON *source) {
    auto revision=cJSON_GetObjectItemCaseSensitive(source,"revision");
    auto periods=cJSON_GetObjectItemCaseSensitive(source,"periods");
    if (!unsignedInteger(revision,UINT32_MAX) || !cJSON_IsArray(periods) || cJSON_GetArraySize(periods)!=4) return {};
    DisplaySchedule schedule{};
    schedule.revision=static_cast<uint32_t>(revision->valuedouble);
    int i=0;
    for (auto p=periods->child; p; p=p->next) {
        const auto *start=cJSON_GetObjectItemCaseSensitive(p,"start");
        const auto *brightness=cJSON_GetObjectItemCaseSensitive(p,"brightness");
        if (!unsignedInteger(start,1439) || !unsignedInteger(brightness,100)) return {};
        schedule.periods[i++]={start->valueint,brightness->valueint};
    }
    return validDisplaySchedule(schedule) ? schedule : DisplaySchedule{};
}
inline uint16_t decodeImage(const char *image,size_t limit,std::unique_ptr<uint8_t,FreeSidebarImage> &output) {
    output.reset();
    if(!image) return 0;
    const size_t size=strlen(image);
    if (!size || size>4*((limit+2)/3) || size%4) return 0;
    const size_t symbols=strspn(image,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
    const size_t padding=size-symbols;
    if (padding>2 || strspn(image+symbols,"=")!=padding) return 0;
    const size_t bytes=size/4*3-padding;
    if (bytes>limit) return 0;
#ifdef ARDUINO_ARCH_ESP32
    output.reset(static_cast<uint8_t *>(ps_malloc(bytes)));
#else
    output.reset(static_cast<uint8_t *>(std::malloc(bytes)));
#endif
    if (!output) return 0;
    size_t written=0;
    if (mbedtls_base64_decode(output.get(),bytes,&written,
                              reinterpret_cast<const uint8_t *>(image),size) || written!=bytes) {
        output.reset();
        return 0;
    }
    return static_cast<uint16_t>(written);
}
inline void decodeSidebar(const cJSON *sidebar, PanelDevice &device) {
    device.sidebarLabel[0]=0;
    device.sidebarImage.reset();
    device.sidebarBytes=0;
    const char *id=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(sidebar,"id"));
    const char *label=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(sidebar,"label"));
    const char *image=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(sidebar,"image"));
    if (!id || strcmp(id,device.id) || !label || !*label || strlen(label)>=sizeof(device.sidebarLabel) || !image) return;
    unsigned glyphs=0;
    for (auto c=reinterpret_cast<const unsigned char *>(label); *c;) {
        const unsigned lead=*c++;
        if (++glyphs>6 || lead<0x20 || lead==0x7f) return;
        if (lead<0x80) continue;
        if (lead<0xc2 || lead>0xf4 || (lead==0xc2 && *c<0xa0) ||
            (lead==0xe0 && *c<0xa0) || (lead==0xed && *c>=0xa0) ||
            (lead==0xf0 && *c<0x90) || (lead==0xf4 && *c>=0x90)) return;
        for (int n=lead<0xe0 ? 1 : lead<0xf0 ? 2 : 3; n>0; --n)
            if ((*c++ & 0xc0)!=0x80) return;
    }
    device.sidebarBytes=decodeImage(image,2048,device.sidebarImage);
    if(!device.sidebarBytes) return;
    strcpy(device.sidebarLabel,label);
}
inline Sections decodeSections(const cJSON *array, const Sections &legacy, bool home) {
    if (!array) return legacy;
    Sections order{};
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array)>4) return order;
    static const char *const names[]={"power","day","month","servers","temperature","fan","controls"};
    for (auto item=array->child; item; item=item->next) {
        const char *name=cJSON_GetStringValue(item);
        if (!name) continue;
        for (int key=0;key<7;++key) {
            const auto kind=static_cast<Section>(key);
            const bool allowed=kind==Section::Power || (home ? key<=3 : key>=4);
            if (allowed && !strcmp(name,names[key]) && !order.contains(kind))
                order.items[order.count++]=kind;
        }
    }
    return order;
}
inline float sensorReading(const cJSON *detail,const char *key,double minimum) {
    const auto *item=cJSON_GetObjectItemCaseSensitive(detail,key);
    return cJSON_IsNumber(item) && item->valuedouble>=minimum && item->valuedouble<=1e12 ?
           static_cast<float>(item->valuedouble) : NAN;
}
inline bool decode(const cJSON *root, PanelData &data,time_t now,const char *requestedId=nullptr) {
    auto get=[root](const char *key){ return cJSON_GetObjectItemCaseSensitive(root,key); };
    auto devices=get("devices");
    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices)>16 ||
        !series(get("day"),data.day) || !series(get("month"),data.month) ||
        !number(get("dayKwh"),data.dayKwh) || !number(get("monthKwh"),data.monthKwh) ||
        !number(get("dayHours"),data.dayHours) || data.dayHours<0 || data.dayHours>24 ||
        !number(get("monthHours"),data.monthHours) || data.monthHours<0 || data.monthHours>720) return false;
    auto when=get("capturedAt"), meters=get("metered"), total=get("total");
    if(!cJSON_IsNumber(when) || std::trunc(when->valuedouble)!=when->valuedouble ||
       when->valuedouble<1704067200 || when->valuedouble>double(now)+30 ||
       when->valuedouble>double(std::numeric_limits<time_t>::max()) ||
       !cJSON_IsNumber(total) || total->valuedouble!=cJSON_GetArraySize(devices) ||
       !cJSON_IsNumber(meters) || meters->valueint!=meters->valuedouble || meters->valueint<0 || meters->valueint>total->valueint) return false;
    data.capturedAt=time_t(when->valuedouble); data.metered=meters->valueint;
    data.count=0;
    data.home=decodeSections(get("home"),defaultHome,true);
    data.detailId[0]=0;
    data.detailImage.reset(); data.detailBytes=0;
    data.temperature=data.fan=data.fanMax=NAN;
    data.powered=-1;
    for(auto p=devices->child;p;p=p->next) {
        auto field=[p](const char *key){return cJSON_GetObjectItemCaseSensitive(p,key);};
        const char *id=cJSON_GetStringValue(field("id")), *name=cJSON_GetStringValue(field("name")),
                   *model=cJSON_GetStringValue(field("model"));
        auto &d=data.devices[data.count];
        if (!validId(id) || !name || strlen(name)>=sizeof(d.name) || !model || strlen(model)>=sizeof(d.model) ||
            !number(field("watts"),d.watts) ||
            !cJSON_IsBool(field("controls"))) return false;
        if (deviceIndex(data,id)>=0) return false;
        snprintf(d.id,sizeof(d.id),"%s",id);
        snprintf(d.name,sizeof(d.name),"%s",name);
        snprintf(d.model,sizeof(d.model),"%s",model);
        d.controls=cJSON_IsTrue(field("controls"));
        d.metered=cJSON_IsTrue(field("metered")) || d.watts>=0;
        d.widgets=decodeSections(field("widgets"),defaultWidgets,false);
        decodeSidebar(field("sidebar"),d);
        if (requestedId && !strcmp(id,requestedId)) {
            const auto *detail=get("detail");
            const char *detailId=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(detail,"id"));
            if (detailId && !strcmp(detailId,id)) {
                strcpy(data.detailId,id);
                data.detailBytes=decodeImage(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(detail,"image")),8192,data.detailImage);
                const char *state=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(detail,"powerState"));
                if(state && !strcmp(state,"on")) data.powered=1;
                else if(state && !strcmp(state,"off")) data.powered=0;
                data.temperature=sensorReading(detail,"temperature",-273.15);
                data.fan=sensorReading(detail,"fan",0);
                data.fanMax=sensorReading(detail,"fanMax",0);
            }
        }
        ++data.count;
    }
    const auto *rate=get("usdPerKwh");
    data.usdPerKwh=cJSON_IsNumber(rate) && rate->valuedouble>=0 && rate->valuedouble<=1000 ? rate->valuedouble : -1;
    data.displaySchedule=decodeSchedule(get("displaySchedule"));
    data.canEditDisplay=cJSON_IsTrue(get("canEditDisplay"));
    return true;
}
inline bool shallowJson(const char *data,size_t size) {
    bool quoted=false,escaped=false;
    int depth=0;
    for(size_t i=0;i<size;++i) {
        char c=data[i];
        if(static_cast<unsigned char>(c)<0x20 && (quoted || (c!='\t' && c!='\r' && c!='\n'))) return false;
        if(escaped) {
            // cJSON exposes strings through C pointers; a decoded NUL would shorten a device ID.
            if(c=='u' && size-i>=5 && !memcmp(data+i+1,"0000",4)) return false;
            escaped=false;
        }
        else if(quoted && c=='\\') escaped=true;
        else if(c=='"') quoted=!quoted;
        else if(!quoted && (c=='{' || c=='[')) { if(++depth>6) return false; }
        else if(!quoted && (c=='}' || c==']')) { if(--depth<0) return false; }
    }
    return !quoted && depth==0;
}
constexpr bool elapsed(uint32_t now,uint32_t start,uint32_t duration) {
    return uint32_t(now-start)>=duration;
}
}
