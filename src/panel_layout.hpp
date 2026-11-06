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
        const int height=kind==Section::Servers ? (rows-1)*rowHeight+cardHeight : isChart(kind) ? 145 : 63;
        layout.boxes[layout.count++]={kind,16,y,pair?376:768,height};
        if (pair) layout.boxes[layout.count++]={sections.items[++i],408,y,376,height};
        y+=height+12;
    }
    const int bottom=y-12;
    layout.limit=bottom>screenBottom ? bottom-screenBottom : 0;
    return layout;
}
inline int widgetTop(const Sections &widgets,bool controls,Section target) {
    int y=164;
    for (int i=0;i<widgets.count;++i) {
        const auto kind=widgets.items[i];
        if (kind==Section::Controls && !controls) continue;
        if (kind==target) return y;
        y+=66;
    }
    return -1;
}
inline int controlAt(int x,int y,int rowY) {
    if (rowY<0 || y<rowY || y>=rowY+58) return -1;
    if (x>=16 && x<392) return 0;
    return x>=408 && x<784 ? 1 : -1;
}
}
