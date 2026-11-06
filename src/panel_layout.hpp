#pragma once
#include <cstdint>

namespace panel {
enum class Section : uint8_t { Power, Day, Month, Servers, Temperature, Fan, Controls };
struct Sections {
    uint8_t count;
    Section items[4];
    bool contains(Section kind) const {
        for (int i=0; i<count; ++i) if (items[i]==kind) return true;
        return false;
    }
};
constexpr Sections defaultHome{4,{Section::Power,Section::Day,Section::Month,Section::Servers}};
constexpr Sections defaultWidgets{2,{Section::Power,Section::Controls}};
constexpr Sections selectorSections{1,{Section::Servers}};
constexpr int headerHeight=70, screenBottom=480, columns=4, columnWidth=194, cardWidth=188, rowHeight=56, cardHeight=50;
constexpr int navX=198, navY=10, navW=108, navH=48;
constexpr bool navHit(int x,int y) { return x>=navX && x<navX+navW && y>=navY && y<navY+navH; }
constexpr int paletteWidth=374, paletteHeight=108;
constexpr int paletteX(int index) { return 16+(index%2)*394; }
constexpr int paletteY(int index) { return 162+(index/2)*124; }
inline int paletteAt(int x,int y) {
    for(int i=0;i<4;++i)
        if(x>=paletteX(i) && x<paletteX(i)+paletteWidth && y>=paletteY(i) && y<paletteY(i)+paletteHeight) return i;
    return -1;
}
inline int cardX(int index) { return 16+(index%columns)*columnWidth; }
inline int cardY(int index,int top) { return top+(index/columns)*rowHeight; }
inline int cardAt(int x,int y,int top,int count) {
    if (x<16 || x>=cardX(columns-1)+cardWidth || (x-16)%columnWidth>=cardWidth ||
        y<headerHeight || y>=screenBottom || y<top) return -1;
    const int fromTop=y-top, index=fromTop/rowHeight*columns+(x-16)/columnWidth;
    return fromTop%rowHeight<cardHeight && index<count ? index : -1;
}
struct SectionBox { Section kind; int x,y,w,h; };
struct HomeLayout {
    SectionBox boxes[4];
    int count,limit;
    int deviceAt(int x,int y,int offset,int devices) const {
        for (int i=0; i<count; ++i)
            if (boxes[i].kind==Section::Servers) return cardAt(x,y,boxes[i].y-offset,devices);
        return -1;
    }
};
inline bool isChart(Section kind) { return kind==Section::Day || kind==Section::Month; }
inline HomeLayout homeLayout(const Sections &sections,int devices) {
    HomeLayout layout{};
    int y=78;
    for (int i=0;i<sections.count;++i) {
        const auto kind=sections.items[i];
        const bool pair=isChart(kind) && i+1<sections.count && isChart(sections.items[i+1]);
        const int rows=devices ? (devices+columns-1)/columns : 1;
        const int height=kind==Section::Servers ? (rows-1)*rowHeight+cardHeight : isChart(kind) ? 145 : 70;
        layout.boxes[layout.count++]={kind,16,y,pair?376:768,height};
        if (pair) layout.boxes[layout.count++]={sections.items[++i],408,y,376,height};
        y+=height+12;
    }
    const int bottom=y-12;
    layout.limit=bottom>screenBottom ? bottom-screenBottom : 0;
    return layout;
}
constexpr int switchLeft=480, switchX=491, switchY=139, switchWidth=282, switchHeight=254;
inline int controlAt(int x,int y) {
    if(x<switchLeft || x>=784 || y<114 || y>=426) return -1;
    constexpr int mid=270;
    return y<mid-6 ? 0 : y>=mid+6 ? 1 : -1;
}
}
