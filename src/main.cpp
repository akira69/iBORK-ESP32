#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "screen_wake.hpp"
#include <esp_wifi.h>
#include <mbedtls/platform_util.h>
#include "panel.hpp"
#include "panel_api.hpp"


namespace {
display_hw::Display lcd;

enum View { Fleet, Servers, Detail, Pin, Settings, Network, ChangePin, Display, DisplayPin };
View view=Fleet;
constexpr uint32_t bg=0x0a131e, bar=0x101c29, tile=0x192b39, grid=0x344959;
constexpr uint32_t ink=0xf2f7f8, muted=0xaac1cc, mint=0x64efc4, gold=0xffbc66;
constexpr int gearX=645, gearY=8, gearW=60, gearH=54;
bool connected, touchReady, displayReady, hasData, requestActive, live, pendingPower;
char pin[13]{}, pendingDevice[33]{};
size_t pinLen;
uint32_t checkedAt, goodAt, lockedAt;
bool locked;
unsigned failures;
PanelData fleetData{};
PanelStatus health=PanelStatus::Network;
int selected, scrollOffset;
using panel::DetailPoll;
DetailPoll detailPoll=DetailPoll::Regular;
panel::Gesture gesture;
bool paintedLink;
String notice;
const char *powerNotice="";
const char *action="on";
void draw();
void clearPin() { mbedtls_platform_zeroize(pin,sizeof(pin)); pinLen=0; }
bool fresh() { return !live || (connected && hasData && health==PanelStatus::Ok && millis()-goodAt<60000); }

#define ART(name) extern const uint8_t name[] asm("_binary_art_" #name "_jpg_start"); \
                  extern const uint8_t name##End[] asm("_binary_art_" #name "_jpg_end")
ART(ibork); ART(finch); ART(settings);
void label(int x, int y, const String &value, uint32_t color = ink, int size = 1) {
    extern const uint8_t small[] asm("_binary_fonts_panel16_vlw_start");
    extern const uint8_t medium[] asm("_binary_fonts_panel25_vlw_start");
    extern const uint8_t large[] asm("_binary_fonts_panel40_vlw_start");
    static lgfx::PointerWrapper data[] = {small, medium, large};
    static lgfx::VLWfont faces[3];
    static const bool loaded[] = {
        faces[0].loadFont(&data[0]), faces[1].loadFont(&data[1]), faces[2].loadFont(&data[2])
    };
    const int index = constrain(size, 1, 3) - 1;
    if (loaded[index]) lcd.setFont(&faces[index]);
    else lcd.setFont(&fonts::Font2);
    lcd.setTextSize(1);
    lcd.setTextColor(color);
    lcd.drawString(value.c_str(), x, y);
}
void box(int x, int y, int w, int h, uint32_t color = tile) {
    lcd.fillRoundRect(x,y,w,h,10,color);
}
void image(const uint8_t *start, const uint8_t *end, int x, int y) {
    lcd.drawJpg(start, end-start, x,y);
}
#include "wifi_setup.inc"
void demo() {
    float day[]={110,103,101,94,91,88,86,88,115,164,170,159,-1,148,135,128,152,158,130,115,97,92,90,91};
    float month[]={132,141,150,134,115,126,131,138,145,132,-1,-1,144,157,161,145,128,137,142,137,154,167,143,135,147,139,128,131,138,125};
    memcpy(fleetData.day,day,sizeof(day)); memcpy(fleetData.month,month,sizeof(month));
    fleetData.count=3; fleetData.metered=2;
    PanelDevice samples[]={
        {"storage","Storage","File server",86,true},
        {"compute","Compute","Application server",5,true},
        {"backup","Backup","Backup server",-1,false}
    };
    for (int i=0;i<fleetData.count;++i) {
        samples[i].widgets=panel::defaultWidgets;
        fleetData.devices[i]=std::move(samples[i]);
    }
    for(float w:day) if(w>=0) { fleetData.dayKwh+=w/1000; ++fleetData.dayHours; }
    for(float w:month) if(w>=0) { fleetData.monthKwh+=w*24/1000; fleetData.monthHours+=24; }
    hasData=true;
}
String watts(float w) { return w<0?"No reading":String(w,0)+" W"; }
template<size_t N>
void chart(const panel::SectionBox &b,const char *title,const float (&values)[N],float hours,float kwh,uint32_t accent) {
    const int x=b.x, y=b.y, plotWidth=b.w-32;
    box(x,y,b.w,b.h);
    label(x+15,y+8,title,ink,2);
    label(x+b.w-168,y+11,hasData && kwh>=0 && fleetData.usdPerKwh>=0?"$"+String(kwh*fleetData.usdPerKwh,2):"--",accent,2);
    label(x+16,y+45,"Avg W",muted);
    for(int dy:{69,90,111}) lcd.drawFastHLine(x+16,y+dy,plotWidth,grid);
    float peak=1;
    for(float v:values) if(v>peak) peak=v;
    int lastX=-1,lastY=0;
    if(hasData) for(size_t i=0;i<N;++i) {
        if(values[i]<0) { lastX=-1; continue; }
        int px=x+16+plotWidth*i/(N-1),py=y+111-lroundf(42*values[i]/peak);
        if(lastX>=0) lcd.drawWideLine(lastX,lastY,px,py,2,accent);
        else lcd.fillCircle(px,py,2,accent);
        lastX=px; lastY=py;
    }
    label(x+16,y+118,hasData?String(hours,1)+"/"+String(N==24?24:720)+" hours covered":"Waiting for readings",muted);
}
void sidebarIcon(const PanelDevice &d,int x,int y) {
    if(d.sidebarImage && lcd.drawJpg(d.sidebarImage.get(),d.sidebarBytes,x,y,40,40)) return;
    lcd.fillCircle(x+20,y+20,19,grid);
    String initials=String(d.name).substring(0,2);
    initials.toUpperCase(); label(x+8,y+9,initials);
}
void fleetCards(int start,int top,int bottom) {
    for(int i=0;i<fleetData.count;++i) {
        const int x=panel::cardX(i), y=panel::cardY(i,start);
        if(y>=bottom || y+panel::cardHeight<=top) continue;
        const auto &s=fleetData.devices[i];
        box(x,y,panel::cardWidth,panel::cardHeight);
        sidebarIcon(s,x+5,y+5);
        lcd.setClipRect(x+51,top,panel::cardWidth-75,bottom-top);
        label(x+51,y+3,s.sidebarLabel[0]?s.sidebarLabel:s.name);
        label(x+51,y+26,watts(s.watts),s.watts<0?muted:mint);
        lcd.setClipRect(0,top,800,bottom-top);
        lcd.drawLine(x+panel::cardWidth-15,y+19,x+panel::cardWidth-9,y+25,muted);
        lcd.drawLine(x+panel::cardWidth-9,y+25,x+panel::cardWidth-15,y+31,muted);
    }
    if(!fleetData.count) label(22,start+10,hasData?"No servers on this panel":"Waiting for iBORK",muted,2);
}
panel::HomeLayout layout() {
    return panel::homeLayout(view==Servers?panel::selectorSections:fleetData.home,fleetData.count);
}
void fleet(int top=panel::headerHeight,int bottom=panel::screenBottom) {
    const auto page=layout();
    lcd.setClipRect(0,top,800,bottom-top);
    lcd.fillRect(0,top,800,bottom-top,bg);
    if (!page.count) label(20,100,"No home sections enabled",muted,2);
    for (int i=0;i<page.count;++i) {
        auto b=page.boxes[i];
        b.y-=scrollOffset;
        if (b.y>=bottom || b.y+b.h<=top) continue;
        if (b.kind==panel::Section::Servers) fleetCards(b.y,top,bottom);
        else if (b.kind==panel::Section::Day) chart(b,"24 hours",fleetData.day,fleetData.dayHours,fleetData.dayKwh,mint);
        else if (b.kind==panel::Section::Month) chart(b,"30 days",fleetData.month,fleetData.monthHours,fleetData.monthKwh,gold);
        else {
            int metered=0; float total=0;
            for(int j=0;j<fleetData.count;++j) if(fleetData.devices[j].watts>=0) { ++metered; total+=fleetData.devices[j].watts; }
            box(16,b.y,360,b.h,0x71e5be); box(388,b.y,298,b.h,0x22303b);
            label(29,b.y+2,metered>0 && metered<fleetData.metered?"Partial fleet power":"Fleet power",0x102e27);
            label(29,b.y+17,metered?watts(total):"-- W",0x102e27,3);
            label(401,b.y+4,String(fleetData.count)+(fleetData.count==1?" server":" servers"),ink,2);
            label(401,b.y+37,String(metered)+" metered",muted);
            label(698,b.y+9,fleetData.usdPerKwh>=0?"$"+String(fleetData.usdPerKwh,4):"Rate",muted);
            label(698,b.y+32,fleetData.usdPerKwh>=0?"/ kWh":"unavailable",muted);
        }
    }
    lcd.clearClipRect();
}
void scrollFleet(int previous) {
    const int delta=scrollOffset-previous, shift=abs(delta);
    const int height=panel::screenBottom-panel::headerHeight;
    if (shift>=height) { fleet(); return; }
    // Preserve pixels between the two positions and redraw only the uncovered strip.
    lcd.copyRect(0,panel::headerHeight+(delta<0?shift:0),800,height-shift,
                 0,panel::headerHeight+(delta>0?shift:0));
    if(delta>0) fleet(panel::screenBottom-shift,panel::screenBottom);
    else fleet(panel::headerHeight,panel::headerHeight+shift);
}
int controlsTop(const PanelDevice &d) {
    return panel::widgetTop(d.widgets,d.controls,panel::Section::Controls);
}
void detail() {
    const auto &d=fleetData.devices[selected];
    sidebarIcon(d,32,94);
    lcd.setClipRect(102,77,680,82);
    label(102,78,d.name,ink,3);
    label(105,130,d.model,muted);
    lcd.clearClipRect();
    bool any=false;
    for (int i=0;i<d.widgets.count;++i) {
        const auto kind=d.widgets.items[i];
        const int y=panel::widgetTop(d.widgets,d.controls,kind);
        if (y<0) continue;
        any=true;
        if (kind==panel::Section::Controls) {
            box(16,y,376,58); box(408,y,376,58);
            label(34,y+14,"Power on",fresh() && !pendingPower?ink:muted,2);
            label(426,y+14,"Graceful shutdown",fresh() && !pendingPower?ink:muted,2);
        } else {
            const bool power=kind==panel::Section::Power, temp=kind==panel::Section::Temperature;
            const float value=temp?fleetData.temperature:fleetData.fan;
            const bool measured=!strcmp(fleetData.detailId,d.id) && std::isfinite(value);
            box(16,y,768,58);
            label(34,y+18,power?"Power draw":temp?"Hottest temperature":"Lowest fan speed",muted);
            label(378,y+14,power?watts(d.watts):measured?String(value,temp?1:0)+(temp?" °C":" RPM"):"No reading",mint,2);
        }
    }
    if (!any) label(20,180,"No widgets enabled for this server",muted,2);
    if(live && !strcmp(d.id,pendingDevice)) label(19,438,powerNotice,muted);
    else if(notice.length()) label(19,438,notice,muted);
    else if(controlsTop(d)>=0) label(19,438,live?"Enter the panel PIN to confirm an action":"Demo actions only",muted);
}
const char *keys[]={"1","2","3","4","5","6","7","8","9","Clear","0","Del"};
#include "pin_change.inc"
#include "screen_settings.inc"
void keypad() {
    const auto &d=fleetData.devices[selected];
    lcd.setClipRect(20,84,394,60);
    label(20,89,d.name,ink,3);
    lcd.clearClipRect();
    label(22,150,action[0]=='s'?"Graceful shutdown":"Power on",gold,2);
    label(22,190,d.id,muted);
    label(22,215,live?"Panel PIN (6-12 digits)":"Demo PIN 246810",muted);
    drawPinInput(); label(22,310,notice,gold);
    drawPinKeys("Confirm action",pinLen>=6 && !locked);
}
const char *healthText() {
    if(!live) return "DEMO DATA";
    if(!connected) return hasData?"OFFLINE - LAST READINGS":"OFFLINE";
    if(!fresh() && hasData) return "STALE - LAST READINGS";
    switch(health) {
        case PanelStatus::Config: return "CHECK PANEL CONFIG";
        case PanelStatus::Clock: return "WAITING FOR CLOCK";
        case PanelStatus::Denied: return "ACCESS DENIED";
        case PanelStatus::Invalid: return "INVALID RESPONSE";
        default: return fresh()?"LIVE":"WAITING FOR IBORK";
    }
}
void connectionIcons() {
    constexpr uint32_t active=0x61e796, unavailable=0xff6c73;
    const uint32_t wifi=connected?active:unavailable;
    paintedLink=live && fresh();
    const uint32_t link=paintedLink?active:unavailable;
    lcd.fillRect(520,10,110,49,bar);
    for(int radius:{24,16,8}) lcd.fillArc(548,46,radius,radius-3,222,318,wifi);
    lcd.fillCircle(548,46,3,wifi);
    for(int y:{19,36}) {
        lcd.drawRoundRect(590,y,32,14,3,link);
        lcd.fillCircle(597,y+7,2,link);
        lcd.drawFastHLine(605,y+7,10,link);
    }
}
void draw() {
    gesture.cancel();
    if(!brightness) return;
    static bool headerDrawn=false;
    if(!headerDrawn) {
        lcd.fillRect(0,0,800,70,bar); lcd.drawFastHLine(0,69,800,grid);
        image(ibork,iborkEnd,8,1); image(finch,finchEnd,722,1);
        box(gearX,gearY,gearW,gearH);
        image(settings,settingsEnd,gearX+(gearW-36)/2,gearY+(gearH-36)/2);
        headerDrawn=true;
    }
    lcd.fillRect(78,7,442,55,bar);
    if(view!=Fleet && view!=Servers) lcd.fillRect(0,70,800,410,bg);
    label(78,18,(view==Fleet || view==Servers || view==Detail)?"Home":"< Back",ink,2);
    if(view==Fleet || view==Detail || view==Servers) {
        box(210,10,128,48,view==Servers?grid:tile);
        label(224,18,"Servers",ink,2);
    }
    const char *state=healthText();
    lcd.setClipRect(344,7,176,56);
    label(347,24,!live?"Demo data":!connected?"Offline":hasData && !fresh()?"Stale readings":fresh()?"Live":state,live && fresh()?mint:gold);
    lcd.clearClipRect();
    connectionIcons();
    switch(view) {
        case Fleet: case Servers: fleet(); break; case Detail: detail(); break;
        case Pin: keypad(); break; case Network: networkView(); break;
        case ChangePin: drawChangePin(); break;
        case Display: drawSchedule(); break;
        case DisplayPin: drawSchedulePin(); break;
        case Settings:
            label(20,89,"Settings",ink,3);
            box(16,172,766,72);
            label(36,189,"Wi-Fi setup",ink,2);
            label(718,190,">",mint,2);
            box(16,264,766,72);
            label(36,281,"Change power PIN",live && connected?ink:muted,2);
            label(718,282,">",mint,2);
            box(16,350,766,64);
            label(36,365,"Display schedule",ink,2);
            label(718,365,">",mint,2);
            label(20,430,changePinMessage,muted);
            break;
    }
}
void tap(int x,int y) {
    if(x>=gearX && x<gearX+gearW && y>=gearY && y<gearY+gearH) {
        if(view==Settings) return;
        if(view==Network) forgetPass();
        if(view==ChangePin) cancelChangePin();
        view=Settings; clearPin(); notice="";
    } else if(view==Settings && x>=16 && x<782 && y>=172 && y<244) {
        view=Network;
        if(joining) wifiScreen=Joining; else scanWiFi();
    } else if(view==Settings && x>=16 && x<782 && y>=264 && y<336) {
        openChangePin();
    } else if(view==Settings && x>=16 && x<782 && y>=350 && y<414) {
        openSchedule();
    } else if((view==Fleet || view==Detail || view==Servers) && x>=210 && x<338 && y>=10 && y<58) {
        view=Servers; scrollOffset=0;
    } else if(y<70 && x<210 && view!=Fleet) {
        if(view==Network && wifiScreen==Password) { forgetPass(); wifiScreen=Networks; }
        else if(view==Network) { forgetPass(); view=Settings; }
        else if(view==ChangePin) cancelChangePin();
        else if(view==Display) view=Settings;
        else if(view==DisplayPin) { clearPin(); view=Display; }
        else { view=view==Pin?Detail:Fleet; scrollOffset=0; clearPin(); notice=""; }
    } else if(view==Display || view==DisplayPin) {
        tapSchedule(x,y); return;
    } else if(view==ChangePin) {
        tapChangePin(x,y); return;
    } else if(view==Network) {
        if(!tapNetwork(x,y)) return;
    } else if(view==Fleet || view==Servers) {
        const int i=layout().deviceAt(x,y,scrollOffset,fleetData.count);
        if(i<0) return;
        selected=i; view=Detail; detailPoll=DetailPoll::Opened; notice="";
    } else if(view==Detail && fresh() && !pendingPower) {
        const int hit=panel::controlAt(x,y,controlsTop(fleetData.devices[selected]));
        if (hit<0) return;
        action=hit==0?"on":"soft"; view=Pin; clearPin(); notice=locked?"Try again in 30 seconds":"";
    } else if(view==Pin && fresh() && !locked && controlsTop(fleetData.devices[selected])>=0) {
        bool wasReady=pinLen>=6;
        if(editPinKey(x,y)) {
            drawPinInput();
            if(wasReady!=(pinLen>=6)) drawPinSubmit("Confirm action",pinLen>=6);
            return;
        }
        if(x>=426 && x<759 && y>=369 && y<443 && pinLen>=6) {
            auto &d=fleetData.devices[selected];
            if(live) {
                if(requestActive || !startPanelPower(d.id,action,pin)) { notice="Please try again shortly"; draw(); return; }
                strlcpy(pendingDevice,d.id,sizeof(pendingDevice));
                requestActive=pendingPower=true; view=Detail; powerNotice="Sending request...";
            } else if(!strcmp(pin,"246810")) {
                failures=0; d.watts=action[0]=='s'?5:86; view=Detail; notice="Demo power state changed";
            } else {
                notice="Incorrect PIN";
                if(++failures>=3) { failures=0; locked=true; lockedAt=millis(); notice="Try again in 30 seconds"; }
            }
            clearPin();
        } else return;
    } else return;
    draw();
}
void servicePanel() {
    if(!live) return;
    PanelStatus status; PanelRequest kind;
    char id[33]{};
    if(view==Pin || view==Detail) strlcpy(id,fleetData.devices[selected].id,sizeof(id));
    if(takePanelResult(status,kind,fleetData)) {
        requestActive=false;
        if(kind==PanelRequest::ChangePin) finishChangePin(status);
        else if(kind==PanelRequest::Display) finishSchedule(status);
        else if(kind==PanelRequest::Power) {
            pendingPower=false;
            powerNotice=status==PanelStatus::Ok?"Request accepted":status==PanelStatus::Denied?"Access denied. Check the PIN.":
                   status==PanelStatus::Limited?"Too many attempts. Wait before trying again.":"Outcome unknown. Check the device before trying again.";
            checkedAt=millis()-30000;
        } else {
            health=status; checkedAt=millis();
            if(status==PanelStatus::Ok) {
                if(panel::validDisplaySchedule(fleetData.displaySchedule))
                    rememberSchedule(fleetData.displaySchedule);
                hasData=true; goodAt=millis()-uint32_t(min(86400.0,max(0.0,difftime(time(nullptr),fleetData.capturedAt)))*1000);
                Serial.printf("iBORK panel GET OK devices=%d ip=%s\n",fleetData.count,WiFi.localIP().toString().c_str());
                if(id[0]) {
                    selected=panel::deviceIndex(fleetData,id);
                    if(selected<0) { view=Fleet; clearPin(); }
                    else if(view==Pin && controlsTop(fleetData.devices[selected])<0) { view=Detail; clearPin(); }
                    else if(view==Detail) detailPoll=panel::afterDetailRead(detailPoll,fleetData,fleetData.devices[selected]);
                }
                scrollOffset=min(scrollOffset,layout().limit);
            } else Serial.printf("iBORK panel GET unavailable code=%u\n",unsigned(status));
        }
        if(view!=Network) draw();
    }
    if(view==Pin && !fresh()) { view=Detail; clearPin(); notice="Refresh required"; draw(); }
    const bool opening=view==Detail && detailPoll==DetailPoll::Opened;
    const uint32_t interval=view==Detail && detailPoll==DetailPoll::Followup ? 3000U :
                            health==PanelStatus::Clock ? 5000U : 30000U;
    if(connected && !requestActive && view!=Pin && view!=ChangePin && view!=DisplayPin &&
       (opening || millis()-checkedAt>=interval)) {
        checkedAt=millis(); requestActive=startPanelRead(view==Detail?fleetData.devices[selected].id:nullptr);
        detailPoll=opening && requestActive ? DetailPoll::FirstRead : DetailPoll::Regular;
    }
}
}
void setup() {
    Serial.begin(115200);
    displayReady=psramFound() && lcd.begin();
    if(!displayReady) { Serial.println("iBORK: RGB/PSRAM initialization failed"); return; }
    canDim=display_hw::initBacklight();
    touchReady=display_hw::initTouch();
    live=panelConfigured(); loadSchedule(); if(!live) demo();


    const bool radio = WiFi.mode(WIFI_STA);
    wifi_config_t station{};
    const bool saved = radio && esp_wifi_get_config(WIFI_IF_STA, &station) == ESP_OK && station.sta.ssid[0];
    mbedtls_platform_zeroize(&station, sizeof(station));
    Serial.printf("iBORK Wi-Fi: radio=%d saved=%d internalHeap=%u\n", radio, saved, ESP.getFreeHeap());
    WiFi.setAutoReconnect(saved);
    if (saved) WiFi.begin();
    else { view=Network; scanWiFi(); }
    draw();
    Serial.printf("iBORK: 800x480 fleet ready; touch=%s; PSRAM=%u\n",touchReady?"ready":"missing",ESP.getPsramSize());
}
void loop() {
    if(!displayReady) { delay(100); return; }
    serviceWiFi();
    bool online=WiFi.status()==WL_CONNECTED && WiFi.localIP()!=IPAddress();
    if(connected!=online) {
        connected=online;
        if(live) { clearPin(); if(view==Pin) view=Detail; }
        if(connected) {

            startPanelClock();
            checkedAt=millis()-30000;
        } else health=PanelStatus::Network;
        draw();
    }
    servicePanel();
    serviceChangePin();
    serviceScreen();
    if(locked && millis()-lockedAt>=30000) { locked=false; notice=""; if(view==Pin) draw(); }
    if(brightness && paintedLink!=(live && fresh())) {
        draw();
    }
    if(touchReady) {
        uint16_t x=0,y=0;
        const auto event=display_hw::readTouch(x,y);
        if(event==panel::Contact::None || wakeTouch(event)) { delay(12); return; }
        const int prior=scrollOffset;
        if(gesture.update(event,x,y,(view==Fleet || view==Servers)?layout().limit:0,scrollOffset))
            tap(gesture.x,gesture.y);
        else if(prior!=scrollOffset) scrollFleet(prior);
    }
    delay(12);
}
