#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "screen_wake.hpp"
#include <esp_wifi.h>
#include <mbedtls/platform_util.h>
#include "panel.hpp"
#include "panel_api.hpp"


namespace {
display_hw::Display screen;
lgfx::LGFX_Sprite frame(&screen);
lgfx::LovyanGFX *lcd=&screen;
bool buffered, refreshing;
uint8_t followups;
unsigned refreshPixels;
uint32_t chartUs, publishUs;
panel::TouchRetry touchRetry;


enum View { Fleet, Servers, Detail, Pin, Settings, Network, ChangePin, Display, Schedule, DisplayPin, Colors, Clock };
View view=Fleet;
struct Palette { const char *name; uint32_t background, surface, header, line, accent; };
constexpr Palette palettes[]={
    {"Teal",0x0a131e,0x192b39,0x101c29,0x344959,0x64efc4},
    {"Amber",0x1a1510,0x30271d,0x211a13,0x594735,0xffbf69},
    {"Ice",0x111a22,0x22313e,0x17232d,0x405768,0xb4dbf5},
    {"Crimson",0x1b1217,0x35232c,0x251820,0x5b3b4a,0xff879b},
    {"Graphite",0x121416,0x272a2e,0x1b1d20,0x474c52,0xdbe2e8},
    {"Forest",0x111a14,0x23342a,0x17231b,0x3d5948,0xa8dd95},
    {"Ocean",0x10192e,0x212e4b,0x17223a,0x3d5277,0x8fb2ff},
    {"Copper",0x201713,0x3a2922,0x2a1e18,0x604438,0xffab8e}
};
static_assert(sizeof(palettes)/sizeof(*palettes)==panel::paletteCount,"Each palette needs a touch target");
unsigned theme;
bool headerDrawn;
const char *themeNote="";
uint32_t bg=palettes[0].background, tile=palettes[0].surface, mint=palettes[0].accent;
uint32_t bar=palettes[0].header, grid=palettes[0].line;
constexpr uint32_t ink=0xf2f7f8, gold=0xffbc66;
uint32_t muted=0xaac1cc;
int contrast;
uint32_t mixColor(uint32_t from,uint32_t to,int percent) {
    uint32_t color=0;
    for(int shift=0;shift<24;shift+=8) {
        const uint32_t channel=(((from>>shift)&255)*(100-percent)+((to>>shift)&255)*percent+50)/100;
        color|=channel<<shift;
    }
    return color;
}
void setTheme(unsigned index) {
    theme=index<panel::paletteCount?index:0;
    const auto &palette=palettes[theme];
    bg=mixColor(palette.background,0,contrast/2);
    tile=palette.surface; mint=palette.accent;
    bar=mixColor(palette.header,0,contrast/2);
    grid=mixColor(palette.line,0xaac1cc,contrast/2);
    muted=mixColor(0xaac1cc,ink,contrast);
    headerDrawn=false;
}
constexpr int gearX=645, gearY=8, gearW=60, gearH=54;
bool connected, touchReady, displayReady, hasData, requestActive, live, pendingPower;
char pin[13]{}, pendingDevice[33]{};
size_t pinLen;
uint32_t checkedAt, goodAt, lockedAt;
bool locked;
unsigned failures;
PanelData fleetData{};
PanelStatus health=PanelStatus::Network;
panel::ReadRetry readRetry;
int selected, scrollOffset;
bool detailFromServers;
using panel::DetailPoll;
DetailPoll detailPoll=DetailPoll::Regular;
panel::Gesture gesture;
bool paintedLink;
const char *paintedHealth="";
String notice;
const char *powerNotice="";
const char *action="on";
void draw();
void clearPin() { mbedtls_platform_zeroize(pin,sizeof(pin)); pinLen=0; }
bool recentReadings() { return hasData && millis()-goodAt<60000; }
bool fresh() { return !live || (connected && health==PanelStatus::Ok && recentReadings()); }

#define ART(name,ext) extern const uint8_t name[] asm("_binary_art_" #name "_" #ext "_start"); \
                  extern const uint8_t name##End[] asm("_binary_art_" #name "_" #ext "_end")
ART(ibork,jpg); ART(finch,jpg); ART(settings,jpg); ART(transmission,png);
ART(knife0,png); ART(knife4,png);
const lgfx::IFont *panelFont(int size) {
    extern const uint8_t small[] asm("_binary_fonts_panel16_vlw_start");
    extern const uint8_t medium[] asm("_binary_fonts_panel25_vlw_start");
    extern const uint8_t large[] asm("_binary_fonts_panel40_vlw_start");
    static lgfx::PointerWrapper data[] = {small, medium, large};
    static lgfx::VLWfont faces[3];
    static const bool loaded[] = {
        faces[0].loadFont(&data[0]), faces[1].loadFont(&data[1]), faces[2].loadFont(&data[2])
    };
    const int index = constrain(size, 1, 3) - 1;
    return loaded[index]?static_cast<const lgfx::IFont *>(&faces[index]):&fonts::Font2;
}
void label(int x, int y, const String &value, uint32_t color = ink, int size = 1) {
    lcd->setFont(panelFont(size));
    lcd->setTextSize(1);
    lcd->setTextColor(color);
    lcd->drawString(value.c_str(), x, y);
}
void box(int x, int y, int w, int h, uint32_t color = tile) {
    lcd->fillRoundRect(x,y,w,h,10,color);
}
// The linker provides separate symbols for the bounds of each embedded file.
size_t assetSize(const uint8_t *start,const uint8_t *end) {
    return reinterpret_cast<uintptr_t>(end)-reinterpret_cast<uintptr_t>(start);
}
void image(const uint8_t *start, const uint8_t *end, int x, int y) {
    lcd->drawJpg(start,assetSize(start,end), x,y);
}
#include "wifi_setup.inc"
void demo() {
    float day[]={110,103,101,94,91,88,86,88,115,164,170,159,-1,148,135,128,152,158,130,115,97,92,90,91};
    float month[]={132,141,150,134,115,126,131,138,145,132,-1,-1,144,157,161,145,128,137,142,137,154,167,143,135,147,139,128,131,138,125};
    for(size_t i=0;i<sizeof(fleetData.day)/sizeof(float);++i) fleetData.day[i]=day[i/4];
    for(size_t i=0;i<sizeof(fleetData.month)/sizeof(float);++i) fleetData.month[i]=month[i/4];
    fleetData.count=3; fleetData.metered=2;
    PanelDevice samples[]={
        {"storage","Storage","File server",86,true},
        {"compute","Compute","Application server",5,true},
        {"backup","Backup","Backup server",-1,false}
    };
    for (int i=0;i<fleetData.count;++i) {
        samples[i].widgets=panel::defaultWidgets;
        samples[i].metered=samples[i].watts>=0;
        fleetData.devices[i]=std::move(samples[i]);
    }
    for(float w:day) if(w>=0) { fleetData.dayKwh+=w/1000; ++fleetData.dayHours; }
    for(float w:month) if(w>=0) { fleetData.monthKwh+=w*24/1000; fleetData.monthHours+=24; }
    hasData=true;
}
String watts(float w) { return w<0?"No reading":String(w,0)+" W"; }
// A live poll should only write rectangles whose displayed content changed.
void publish(int x,int y,int w,int h) {
    if(!refreshing) return;
    if((view==Fleet || view==Servers) && y+h>panel::headerHeight) {
        const int bottom=min(y+h,panel::screenBottom);
        y=max(y,panel::headerHeight); h=bottom-y;
    }
    if(h<=0) return;
    if(!buffered) {refreshPixels+=w*h; return;}
    const uint32_t started=micros();
    // Compare in internal RAM and send only changed runs to the live RGB framebuffer.
    static lgfx::rgb565_t before[display_hw::widthPixels], after[display_hw::widthPixels];
    for(int row=y;row<y+h;++row) {
        frame.readRect(x,row,w,1,after);
        screen.readRect(x,row,w,1,before);
        for(int i=0;i<w;) {
            while(i<w && before[i].raw==after[i].raw) ++i;
            const int start=i;
            while(i<w && before[i].raw!=after[i].raw) ++i;
            if(i>start) {
                screen.pushImage(x+start,row,i-start,1,after+start);
                refreshPixels+=i-start;
            }
        }
        if((row-y)%16==15) delay(1);
    }
    publishUs+=micros()-started;
}
void field(String &previous,int x,int y,int w,int h,const String &value,uint32_t color,int size=1) {
    if(refreshing && previous==value) return;
    previous=value;
    if(refreshing) lcd->fillRect(x,y,w,h,tile);
    label(x,y,value,color,size);
    publish(x,y,w,h);
}
String costText(double cost) {
    if(cost<0) return "--";
    if(cost<9999.995) return "$"+String(cost,2);
    char text[20];
    snprintf(text,sizeof(text),"$%.1e",cost);
    return text;
}
template<size_t N>
void chart(const panel::SectionBox &b,const char *title,const float (&values)[N],float hours,float kwh,uint32_t accent) {
    const int x=b.x, y=b.y, plotX=x+100, plotWidth=b.w-116;
    const int plotTop=y+76, plotBottom=y+b.h-56, plotHeight=plotBottom-plotTop;
    const bool monthly=b.kind==panel::Section::Month;
    const bool hasHistory=hasData && (!live || fleetData.chartAt);
    const int costX=x+b.w-(monthly?260:168);
    static String cost,coverage;
    static int16_t last[N];
    static panel::WattRange lastRange{};
    if(!refreshing) {
        box(x,y,b.w,b.h);
        label(x+15,y+8,title,ink,2);
        label(x+16,y+45,monthly?"6 hr avg W":"15 min avg W",muted);
        label(plotX,plotBottom+12,monthly?"30d ago":"24h ago",muted);
        label(x+b.w-50,plotBottom+12,"Now",muted);
        if(monthly) label(costX,y+5,"Actual",muted);
    }
    const double actualCost=hasHistory && kwh>=0 && fleetData.usdPerKwh>=0?kwh*fleetData.usdPerKwh:-1;
    field(cost,costX,y+(monthly?25:11),monthly?124:156,34,costText(actualCost),accent,2);
    if(monthly) {
        static String forecastCaption,forecast;
        const int forecastX=x+b.w-134;
        const double estimate=panel::monthlyCostEstimate(actualCost,hours);
        field(forecastCaption,forecastX,y+5,122,20,estimate>=0?"30-day est.":"",muted);
        field(forecast,forecastX,y+25,122,34,estimate>=0?costText(estimate):String(),accent,2);
    }
    field(coverage,x+16,y+b.h-22,b.w-32,22,hasHistory?String(hours,1)+"/"+
          String(monthly?panel::monthWindowHours:24)+" hours covered":"Waiting for readings",muted);
    const uint32_t started=micros();
    const auto range=hasHistory?panel::chartWattRange(values):panel::WattRange{};
    const double span=range.high-range.low;
    int16_t heights[N];
    for(size_t i=0;i<N;++i)
        heights[i]=span>0 && values[i]>=0?std::lround(plotHeight*(values[i]-range.low)/span):-1;
    if(refreshing && lastRange.low==range.low && lastRange.high==range.high &&
       !memcmp(heights,last,sizeof(heights))) { chartUs+=micros()-started; return; }
    memcpy(last,heights,sizeof(heights));
    lastRange=range;
    const int plotClearY=plotTop-10, plotClearHeight=plotHeight+22;
    if(refreshing) lcd->fillRect(x+12,plotClearY,b.w-24,plotClearHeight,tile);
    for(int i=0;i<3;++i) {
        const int tickY=plotBottom-i*plotHeight/2;
        lcd->drawFastHLine(plotX,tickY,plotWidth+1,grid);
        if(span>0) {
            const double value=range.low+i*span/2;
            char text[16];
            snprintf(text,sizeof(text),value>=100000?"%.3g":value==std::floor(value)?"%.0f":"%.1f",value);
            label(x+16,tickY-10,text,muted);
        }
    }
    int lastX=-1,lastY=0;
    for(size_t i=0;i<N;++i) {
        if(heights[i]<0) { lastX=-1; continue; }
        int px=plotX+plotWidth*i/(N-1),py=plotBottom-heights[i];
        if(lastX>=0) lcd->drawWideLine(lastX,lastY,px,py,1.5f,accent);
        else lcd->fillCircle(px,py,1,accent);
        lastX=px; lastY=py;
    }
    chartUs+=micros()-started;
    publish(x+12,plotClearY,b.w-24,plotClearHeight);
}
void sidebarIcon(const PanelDevice &d,int x,int y) {
    constexpr int size=40;
    if(d.sidebarImage && lcd->drawJpg(d.sidebarImage.get(),d.sidebarBytes,x,y,size,size)) return;
    lcd->fillCircle(x+size/2,y+size/2,size/2-1,grid);
    String initials=String(d.name).substring(0,2);
    initials.toUpperCase(); label(x+size/5,y+size/4,initials,ink);
}
void fleetCards(const panel::SectionBox &b,int top,int bottom) {
    for(int i=0;i<fleetData.count;++i) {
        const int x=panel::cardX(i), y=panel::cardY(i,b.y);
        if(y>=bottom || y>=b.y+b.h) break;
        if(y+panel::cardHeight<=top) continue;
        const auto &s=fleetData.devices[i];
        static String readings[16];
        if(refreshing) {
            field(readings[i],x+51,y+26,panel::cardWidth-75,23,watts(s.watts),s.watts<0?muted:mint);
            continue;
        }
        box(x,y,panel::cardWidth,panel::cardHeight);
        if(s.metered) lcd->drawRoundRect(x,y,panel::cardWidth,panel::cardHeight,10,mint);
        sidebarIcon(s,x+5,y+5);
        lcd->setClipRect(x+51,top,panel::cardWidth-75,bottom-top);
        label(x+51,y+3,s.sidebarLabel[0]?s.sidebarLabel:s.name);
        field(readings[i],x+51,y+26,panel::cardWidth-75,23,watts(s.watts),s.watts<0?muted:mint);
        lcd->setClipRect(0,top,800,bottom-top);
        lcd->drawLine(x+panel::cardWidth-15,y+19,x+panel::cardWidth-9,y+25,muted);
        lcd->drawLine(x+panel::cardWidth-9,y+25,x+panel::cardWidth-15,y+31,muted);
    }
    if(!refreshing && !fleetData.count) label(22,b.y+10,hasData?"No servers on this panel":"Waiting for iBORK",muted,2);
}
panel::HomeLayout layout() {
    return panel::homeLayout(view==Servers?panel::selectorSections:fleetData.home,fleetData.count,view==Servers);
}
bool showServerSelector() {
    return view==Fleet && (fleetData.count>panel::columns || !fleetData.home.contains(panel::Section::Servers));
}
void fleet(int top=panel::headerHeight,int bottom=panel::screenBottom) {
    const auto page=layout();
    lcd->setClipRect(0,top,800,bottom-top);
    if(!refreshing) lcd->fillRect(0,top,800,bottom-top,bg);
    if (!refreshing && !page.count) label(20,100,"No home sections enabled",muted,2);
    for (int i=0;i<page.count;++i) {
        auto b=page.boxes[i];
        b.y-=scrollOffset;
        if (b.y>=bottom || b.y+b.h<=top) continue;
        if (b.kind==panel::Section::Servers) fleetCards(b,top,bottom);
        else if (b.kind==panel::Section::Day) chart(b,"24 hours",fleetData.day,fleetData.dayHours,fleetData.dayKwh,mint);
        else if (b.kind==panel::Section::Month) chart(b,"30 days",fleetData.month,fleetData.monthHours,fleetData.monthKwh,gold);
        else {
            int reporting=0; float total=0;
            for(int j=0;j<fleetData.count;++j) if(fleetData.devices[j].watts>=0) { ++reporting; total+=fleetData.devices[j].watts; }
            static String power,meters;
            if(!refreshing) {
                box(b.x,b.y,b.w,b.h);
                label(30,b.y+6,"Current fleet power",muted);
                if(hasData) lcd->drawRoundRect(624,b.y+17,144,36,10,mint);
            }
            field(power,30,b.y+24,248,44,reporting?watts(total):"-- W",mint,3);
            if(hasData) field(meters,634,b.y+25,124,20,
                fleetData.metered?String(reporting)+"/"+String(fleetData.metered)+" reporting":"No meters",ink);
            if(refreshing) continue;
            lcd->drawPng(transmission,assetSize(transmission,transmissionEnd),291,b.y+3);
            label(364,b.y+10,"Electricity rate",muted);
            label(364,b.y+34,fleetData.usdPerKwh>=0?"$"+String(fleetData.usdPerKwh,4)+" / kWh":"-- / kWh",ink);
        }
    }
    lcd->clearClipRect();
}
void scrollFleet(int previous) {
    const int delta=scrollOffset-previous, shift=abs(delta);
    const int height=panel::screenBottom-panel::headerHeight;
    if (shift>=height) { fleet(); return; }
    // Preserve pixels between the two positions and redraw only the uncovered strip.
    lcd->copyRect(0,panel::headerHeight+(delta<0?shift:0),800,height-shift,
                 0,panel::headerHeight+(delta>0?shift:0));
    if(delta>0) fleet(panel::screenBottom-shift,panel::screenBottom);
    else fleet(panel::headerHeight,panel::headerHeight+shift);
}
bool hasSwitch(const PanelDevice &d) {
    return d.controls && d.widgets.contains(panel::Section::Controls);
}
void knifeSwitch(int pose) {
    lcd->fillRect(panel::switchX,panel::switchY,panel::switchWidth,panel::switchHeight,tile);
    if(pose<0) label(panel::switchX+44,panel::switchY+112,connected?"Reading chassis state":"Offline",muted);
    else {
        const auto *start=pose?knife4:knife0, *end=pose?knife4End:knife0End;
        lcd->drawPng(start,assetSize(start,end),panel::switchX,panel::switchY);
    }
}
void detail() {
    const auto &d=fleetData.devices[selected];
    const bool controls=hasSwitch(d);
    if(!refreshing || d.imageChanged) {
        lcd->fillRect(16,76,80,80,bg);
        if(!d.detailImage ||
           !lcd->drawJpg(d.detailImage.get(),d.detailBytes,16,76,80,80))
            sidebarIcon(d,36,96);
        publish(16,76,80,80);
    }
    if(!refreshing) {
        lcd->setClipRect(102,77,controls?panel::switchLeft-112:680,82);
        label(102,78,d.name,ink,3);
        label(105,130,d.model,muted);
        lcd->clearClipRect();
        if(controls) {
            box(panel::switchLeft,76,784-panel::switchLeft,350);
            label(496,82,"Power control",ink,2);

        }
    }
    if(controls) {
        static bool paintedEnabled=false;
        const bool enabled=fresh() && !pendingPower;
        if(!refreshing || paintedEnabled!=enabled) {
            label(496,116,"Power on",enabled?ink:muted);
            label(496,395,"Graceful shutdown",enabled?ink:muted);
            publish(496,116,128,23); publish(496,395,158,23);
            paintedEnabled=enabled;
        }
        static int paintedPose=-1;
        const int pose=live?d.powered:d.watts>5;
        if(!refreshing || paintedPose!=pose) {
            knifeSwitch(pose);
            publish(panel::switchX,panel::switchY,panel::switchWidth,panel::switchHeight);
            paintedPose=pose;
        }
    }
    int y=164;
    for (int i=0;i<d.widgets.count;++i) {
        const auto kind=d.widgets.items[i];
        if(kind==panel::Section::Controls) continue;
        const bool power=kind==panel::Section::Power, temp=kind==panel::Section::Temperature;
        const bool current=d.hasDetail;
        const bool loading=(fresh() && d.pending) || (!current && connected && requestActive);
        const bool fan=kind==panel::Section::Fan;
        const int height=fan?78:58;
        if(!refreshing) {
            box(16,y,controls?452:768,height);
            label(34,y+height/2-11,power?"Power draw":temp?"Hottest temperature":"Fan Speeds",muted);
        }
        static String powerReading,temperature,lowest,highest;
        const int x=controls?208:378, width=controls?248:378;
        if(fan) {
            const auto rpm=[current,loading](float speed) -> String {
                return current && std::isfinite(speed)?String(speed,0)+" RPM":loading?"Loading":"No reading";
            };
            field(lowest,x,y+8,width,32,"Lowest: "+rpm(d.fan),mint,2);
            field(highest,x,y+40,width,32,"Highest: "+rpm(d.fanMax),mint,2);
        } else {
            field(power?powerReading:temperature,x,y+14,width,33,power?watts(d.watts):current && std::isfinite(d.temperature)?
                  String(d.temperature,1)+u8" \u00b0C":loading?"Loading":"No reading",mint,2);
        }
        y+=height+8;
    }
    if(controls) {
        static String stateNote;
        field(stateNote,660,86,114,23,d.powered>=0 && (!fresh() || (live && !d.confirmed))?"Last known":"",muted);
    }
    if(refreshing) return;
    if(y==164 && !controls) label(20,180,"No widgets enabled for this server",muted,2);
    if(live && !strcmp(d.id,pendingDevice)) label(19,438,powerNotice,muted);
    else if(notice.length()) label(19,438,notice,muted);
    else if(controls) label(19,438,live?"Enter the panel PIN to confirm an action":"Demo actions only",muted);
}
const char *keys[]={"1","2","3","4","5","6","7","8","9","Clear","0","Del"};
#include "pin_change.inc"
#include "screen_settings.inc"
void keypad() {
    const auto &d=fleetData.devices[selected];
    lcd->setClipRect(20,84,394,60);
    label(20,89,d.name,ink,3);
    lcd->clearClipRect();
    label(22,150,action[0]=='s'?"Graceful shutdown":"Power on",gold,2);
    label(22,190,d.id,muted);
    label(22,215,live?"Panel PIN (6-12 digits)":"Demo PIN 246810",muted);
    drawPinInput(); label(22,310,notice,gold);
    drawPinKeys("Confirm action",pinLen>=6 && !locked);
}
const char *healthText() {
    if(!live) return "DEMO DATA";
    if(!connected) return hasData?"Offline - last readings":"Offline";
    return panel::readHealthText(health,hasData,recentReadings());
}
bool connectionChanged() {
    return paintedLink!=(live && fresh()) || strcmp(paintedHealth,healthText())!=0;
}
void connectionIcons() {
    constexpr uint32_t active=0x61e796, unavailable=0xff6c73;
    const uint32_t wifi=connected?active:unavailable;
    paintedLink=live && fresh();
    const uint32_t link=paintedLink?active:unavailable;
    lcd->fillRect(576,10,59,49,bar);
    for(int radius:{18,12,6}) lcd->fillArc(592,44,radius,radius-2,222,318,wifi);
    lcd->fillCircle(592,44,2,wifi);
    for(int y:{19,36}) {
        lcd->drawRoundRect(610,y,24,14,3,link);
        lcd->fillCircle(616,y+7,2,link);
        lcd->drawFastHLine(622,y+7,7,link);
    }
}
void refreshStatus() {
    lcd->fillRect(396,7,180,56,bar);
    lcd->setClipRect(396,7,180,56);
    paintedHealth=healthText();
    label(397,24,paintedHealth,gold);
    lcd->clearClipRect();
    connectionIcons();
    publish(396,7,239,56);
}
void refreshClock() {
    static int paintedMinute=-2;
    static time_t checkedSecond=-1;
    const time_t now=time(nullptr);
    if(now==checkedSecond && !clockDirty) return;
    checkedSecond=now;
    const int minute=panel::localMinuteOfDay(now);
    if(minute==paintedMinute && !clockDirty) return;
    paintedMinute=minute;
    clockDirty=false;
    char clock[6];
    const auto meridiem=panel::formatClock(clock,minute,clockSettings.mode);
    constexpr int x=170, width=110;
    lcd->fillRect(x,7,width,56,bar);
    if(clock[0]) {
        const uint32_t colors[]={clockSettings.style==1?mint:ink,0x64efc4,0xffbc66,0x9bccff};
        const uint32_t color=colors[clockSettings.color];
        if(clockSettings.style==2) lcd->drawRoundRect(x,8,width,54,6,grid);
        const lgfx::IFont *faces[]={panelFont(1),&fonts::Font2,&fonts::Font7};
        lcd->setFont(faces[clockSettings.font]);
        lcd->setTextSize(1);
        constexpr float heights[]={16,23,31};
        const float scale=min(heights[clockSettings.size]/lcd->fontHeight(),100.0f/lcd->textWidth(clock));
        lcd->setTextSize(scale);
        lcd->setTextColor(color);
        const int left=x+(width-lcd->textWidth(clock)-clockSettings.bold)/2;
        const int top=(*meridiem?25:34)-lcd->fontHeight()/2;
        lcd->drawString(clock,left,top);
        if(clockSettings.bold) lcd->drawString(clock,left+1,top);
        if(*meridiem) label(x+44,43,meridiem,color);
    }
    publish(x,7,width,56);
}
void pollSpinner() {
    static int previous=-1;
    const bool loading=live && connected && !hasData && requestActive;
    const int phase=loading?int((millis()/125)%8):-1;
    if(!brightness || phase==previous) return;
    previous=phase;
    screen.fillRect(549,44,22,22,bar);
    if(phase>=0) screen.fillArc(560,55,8,5,phase*45,phase*45+215,mint);
}
void refreshLink() {
    refreshing=true;
    if(buffered) lcd=&frame;
    refreshStatus();
    if(view==Detail) detail();
    lcd=&screen; refreshing=false;
}
void drawColors() {
    label(16,82,"Panel theme",ink,2);
    for(unsigned i=0;i<panel::paletteCount;++i) {
        const auto &palette=palettes[i];
        const int x=panel::paletteX(i), y=panel::paletteY(i);
        box(x,y,panel::paletteWidth,panel::paletteHeight,palette.background);
        lcd->drawRoundRect(x,y,panel::paletteWidth,panel::paletteHeight,10,i==theme?palette.accent:grid);
        label(x+12,y+9,palette.name,ink,2);
        lcd->fillRoundRect(x+12,y+47,panel::paletteWidth-24,44,6,palette.surface);
        lcd->fillRect(x+12,y+58,panel::paletteWidth-24,2,palette.line);
        lcd->fillCircle(x+29,y+74,5,palette.accent);
        lcd->fillRoundRect(x+43,y+71,70,5,2,palette.accent);
        if(i==theme) label(x+12,y+99,"Selected",palette.accent);
    }
    label(20,424,themeNote,gold);
}
void draw() {
    gesture.cancel();
    if(!brightness) return;
    if(buffered) lcd=&frame;
    if(!headerDrawn) {
        lcd->fillRect(0,0,800,70,bar); lcd->drawFastHLine(0,69,800,grid);
        image(ibork,iborkEnd,8,1); image(finch,finchEnd,722,1);
        box(gearX,gearY,gearW,gearH);
        image(settings,settingsEnd,gearX+(gearW-36)/2,gearY+(gearH-36)/2);
        label(82,17,"iBORK",ink,2);
        headerDrawn=true; clockDirty=true;
    }
    lcd->fillRect(panel::navX,7,panel::navW,55,bar);
    if(view!=Fleet && view!=Servers) lcd->fillRect(0,70,800,410,bg);
    const bool selector=showServerSelector();
    if(view!=Fleet || selector) {
        box(panel::navX,panel::navY,panel::navW,panel::navH);
        label(panel::navX+12,19,selector?"Servers":"< Back",ink,2);
    }
    refreshStatus();
    refreshClock();
    switch(view) {
        case Fleet: case Servers: fleet(); break; case Detail: detail(); break;
        case Pin: keypad(); break; case Network: networkView(); break;
        case ChangePin: drawChangePin(); break;
        case Display: drawDisplay(); break;
        case Clock: drawClockSettings(); break;
        case Schedule: drawSchedule(); break;
        case DisplayPin: drawSchedulePin(); break;
        case Colors: drawColors(); break;
        case Settings:
            label(20,89,"Settings",ink,3);
            box(16,172,766,72);
            label(36,189,"Wi-Fi setup",ink,2);
            label(718,190,">",mint,2);
            box(16,264,766,72);
            label(36,281,"Change power PIN",live && connected?ink:muted,2);
            label(718,282,">",mint,2);
            box(16,350,373,64); box(405,350,377,64);
            label(36,365,"Display",ink,2);
            label(355,365,">",mint,2);
            label(424,365,String("Theme: ")+palettes[theme].name,ink,2);
            label(748,365,">",mint,2);
            label(20,430,changePinMessage,muted);
            break;
    }
    if(buffered) {
        lcd=&screen;
        frame.pushSprite(0,0);
    }
}
void tap(int x,int y) {
    if(x>=gearX && x<gearX+gearW && y>=gearY && y<gearY+gearH) {
        if(view==Settings) return;
        if(view==Network) forgetPass();
        if(view==ChangePin) cancelChangePin();
        view=Settings; themeNote=""; clearPin(); notice="";
    } else if(view==Settings && x>=16 && x<782 && y>=172 && y<244) {
        view=Network;
        if(joining) wifiScreen=Joining; else scanWiFi();
    } else if(view==Settings && x>=16 && x<782 && y>=264 && y<336) {
        openChangePin();
    } else if(view==Settings && x>=405 && x<782 && y>=350 && y<414) {
        themeNote=""; view=Colors;
    } else if(view==Settings && x>=16 && x<389 && y>=350 && y<414) {
        view=Display; displayNote="";
    } else if(panel::navHit(x,y) && showServerSelector()) {
        view=Servers; scrollOffset=0;
    } else if(panel::navHit(x,y) && view!=Fleet) {
        if(view==Network && wifiScreen==Password) { forgetPass(); wifiScreen=Networks; }
        else if(view==Network) { forgetPass(); view=Settings; }
        else if(view==ChangePin) cancelChangePin();
        else if(view==Display || view==Colors) view=Settings;
        else if(view==Schedule || view==Clock) view=Display;
        else if(view==DisplayPin) { clearPin(); view=Schedule; }
        else {
            if(view==Pin) view=Detail;
            else if(view==Detail && detailFromServers) view=Servers;
            else view=Fleet;
            scrollOffset=0; clearPin(); notice="";
        }
    } else if(view==Colors) {
        const int choice=panel::paletteAt(x,y);
        if(choice<0 || unsigned(choice)==theme) return;
        Preferences prefs;
        if(prefs.begin("display",false) && prefs.putUChar("theme",choice)==1) {
            setTheme(choice); themeNote="";
        } else themeNote="Could not save color. Tap to retry.";
    } else if(view==Display) {
        tapDisplay(x,y); return;
    } else if(view==Clock) {
        tapClockSettings(x,y); return;
    } else if(view==Schedule || view==DisplayPin) {
        tapSchedule(x,y); return;
    } else if(view==ChangePin) {
        tapChangePin(x,y); return;
    } else if(view==Network) {
        if(!tapNetwork(x,y)) return;
    } else if(view==Fleet || view==Servers) {
        const int i=layout().deviceAt(x,y,scrollOffset,fleetData.count);
        if(i<0) return;
        detailFromServers=view==Servers;
        selected=i; view=Detail; detailPoll=DetailPoll::Opened; followups=0; notice="";
    } else if(view==Detail && fresh() && !pendingPower) {
        const int hit=hasSwitch(fleetData.devices[selected])?panel::controlAt(x,y):-1;
        if (hit<0) return;
        action=hit==0?"on":"soft"; view=Pin; clearPin(); pinLastInput=millis(); notice=locked?"Try again in 30 seconds":"";
    } else if(view==Pin && fresh() && !locked && hasSwitch(fleetData.devices[selected])) {
        bool wasReady=pinLen>=6;
        if(editPinKey(x,y)) {
            pinLastInput=millis();
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
    bool layoutChanged=false;
    const bool hadData=hasData;
    const auto priorChartAt=fleetData.chartAt;
    const auto priorView=view;
    if(takePanelResult(status,kind,fleetData,layoutChanged)) {
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
            if(readRetry.completed(status) && connected && view!=Network) {
                Serial.println("iBORK panel transport: reconnecting Wi-Fi");
                WiFi.reconnect();
            }
            if(status==PanelStatus::Ok) {
                if(panel::validDisplaySchedule(fleetData.displaySchedule))
                    rememberSchedule(fleetData.displaySchedule);
                hasData=true; goodAt=millis()-uint32_t(min(86400.0,max(0.0,difftime(time(nullptr),fleetData.capturedAt)))*1000);
                Serial.printf("iBORK panel GET OK devices=%d metered=%d rate=%.4f schedule=%u charts=%s ip=%s\n",
                              fleetData.count,fleetData.metered,fleetData.usdPerKwh,
                              unsigned(fleetData.displaySchedule.revision),priorChartAt!=fleetData.chartAt?"new":"cached",WiFi.localIP().toString().c_str());
                if(fleetData.artRevision) {
                    int power=0,art=0;
                    for(int i=0;i<fleetData.count;++i) {
                        power+=fleetData.devices[i].confirmed;
                        art+=fleetData.devices[i].detailImage!=nullptr;
                    }
                    Serial.printf("iBORK preload: power=%d images=%d pending=%d rev=%lu\n",power,art,fleetData.waiting,(unsigned long)fleetData.artRevision);
                }
                if(id[0]) {
                    selected=panel::deviceIndex(fleetData,id);
                    if(selected<0) { view=Fleet; clearPin(); }
                    else if(view==Pin && !hasSwitch(fleetData.devices[selected])) { view=Detail; clearPin(); }
                    else if(view==Detail && !fleetData.artRevision) detailPoll=panel::afterDetailRead(detailPoll,fleetData,fleetData.devices[selected]);
                }
                if(view!=Detail || !fleetData.devices[selected].pending) followups=0;
                scrollOffset=min(scrollOffset,layout().limit);
            } else Serial.printf("iBORK panel GET unavailable code=%u\n",unsigned(status));
        }
        if(view!=Network) {
            const bool dashboard=view==Fleet || view==Servers || view==Detail;
            const bool statusChanged=connectionChanged();
            if(kind!=PanelRequest::Read || layoutChanged || (!hadData && hasData) || priorView!=view ||
               (!dashboard && statusChanged)) draw();
            else if(brightness && dashboard) {
                refreshing=true; refreshPixels=chartUs=publishUs=0;
                if(buffered) lcd=&frame;
                if(statusChanged) refreshStatus();
                if(view==Detail) detail(); else if(status==PanelStatus::Ok) fleet();
                lcd=&screen; refreshing=false;
                Serial.printf("iBORK refresh: %u pixels, charts %lu us, publish %lu us\n",refreshPixels,
                              static_cast<unsigned long>(chartUs),static_cast<unsigned long>(publishUs));
            }
        }
    }
    if(view==Pin && (!fresh() || pinIdle())) {
        view=Detail; clearPin(); notice=fresh()?"PIN entry expired.":"Refresh required"; draw();
    }
    const bool opening=view==Detail && detailPoll==DetailPoll::Opened;
    const bool warming=health==PanelStatus::Ok && view==Detail && fleetData.devices[selected].pending && followups<3;
    const uint32_t interval=warming?2000U:health==PanelStatus::Ok && view==Detail && detailPoll==DetailPoll::Followup?3000U:
                            health==PanelStatus::Clock?5000U:readRetry.delayMs();
    const bool enteringPin=view==Pin || view==ChangePin || view==DisplayPin;
    if(brightness && connected && view!=Network && !enteringPin && !requestActive && (opening || millis()-checkedAt>=interval)) {
        checkedAt=millis();
        const bool charts=!fleetData.chartAt || difftime(time(nullptr),fleetData.chartAt)>=60;
        requestActive=startPanelRead(view==Detail?fleetData.devices[selected].id:nullptr,fleetData.artRevision,charts);
        if(requestActive) {
            if(warming) ++followups;
            detailPoll=opening?DetailPoll::FirstRead:DetailPoll::Regular;
        }
    }
}
}
void setup() {
    Serial.begin(115200);
    displayReady=psramFound() && screen.begin();
    if(!displayReady) { Serial.println("iBORK: RGB/PSRAM initialization failed"); return; }
    frame.setPsram(true);
    frame.setColorDepth(16);
    buffered=frame.createSprite(display_hw::widthPixels,display_hw::heightPixels)!=nullptr;
    Serial.printf("iBORK frame buffer: %s\n",buffered?"ready":"direct drawing");
    canDim=display_hw::initBacklight();
    touchReady=display_hw::initTouch();
    touchRetry.checkedAt=millis();
    {
        Preferences prefs;
        if(prefs.begin("display",true)) {
            contrast=min(100,int(prefs.getUChar("contrast",0)));
            setTheme(prefs.getUChar("theme",0));
        }
    }
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
    const bool online=WiFi.status()==WL_CONNECTED && WiFi.localIP()!=IPAddress();
    static panel::StationRetry stationRetry;
    if(stationRetry.due(online || !WiFi.getAutoReconnect() || view==Network || joining ||
                        scanCount==WIFI_SCAN_RUNNING,millis())) {
        Serial.println("iBORK Wi-Fi: retrying saved network");
        esp_wifi_connect();
    }
    if(connected!=online) {
        const auto was=view;
        connected=online;
        if(live) { clearPin(); if(view==Pin) view=Detail; }
        if(connected) {

            startPanelClock();
            checkedAt=millis()-30000;
        } else health=PanelStatus::Network;
        if(was==view && brightness && (view==Fleet || view==Servers || view==Detail)) {
            refreshLink();
        } else draw();
    }
    servicePanel();
    serviceChangePin();
    serviceScreen();
    if(brightness) {
        refreshing=true;
        if(buffered) lcd=&frame;
        refreshClock();
        lcd=&screen;
        refreshing=false;
    }
    if(locked && millis()-lockedAt>=30000) { locked=false; notice=""; if(view==Pin) draw(); }
    if(brightness && connectionChanged()) {
        if(view==Fleet || view==Servers || view==Detail) {
            refreshLink();
        } else draw();
    }
    pollSpinner();
    if(!touchReady && touchRetry.due(millis())) {
        Wire1.end();
        touchReady=display_hw::initTouch();
    }
    if(touchReady) {
        uint16_t x=0,y=0;
        bool ioError;
        const auto event=display_hw::readTouch(x,y,ioError);
        if(touchRetry.failed(ioError,millis())) {
            touchReady=false;
            gesture={}; gesture.cancelled=true;
            wake.held=wake.consuming=false; brightnessSlider={};
            return;
        }
        if(event==panel::Contact::None || wakeTouch(event) || slideDisplay(event,x,y)) { delay(12); return; }
        const int prior=scrollOffset;
        if(gesture.update(event,x,y,(view==Fleet || view==Servers)?layout().limit:0,scrollOffset))
            tap(gesture.x,gesture.y);
        else if(prior!=scrollOffset) scrollFleet(prior);
    }
    delay(12);
}
