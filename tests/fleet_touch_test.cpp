#include "../src/fleet_touch.hpp"
#include <algorithm>
#include <cassert>

int main() {
    using namespace panel;
    static_assert(!navHit(74,32) && !navHit(278,32) && navHit(284,10) && navHit(391,57) && !navHit(392,57));
    for(int i=0;i<4;++i) {
        const int x=paletteX(i), y=paletteY(i);
        assert(paletteAt(x,y)==i && paletteAt(x+paletteWidth-1,y+paletteHeight-1)==i);
        assert(paletteAt(x+paletteWidth,y)==-1 && paletteAt(x,y+paletteHeight)==-1);
    }
    assert(paletteAt(250,30)==-1 && paletteAt(750,479)==-1);
    const auto standard=homeLayout(defaultHome,16);
    assert(standard.limit==0 && standard.boxes[1].h>200 && standard.boxes[2].h==standard.boxes[1].h);
    assert(standard.boxes[3].h==cardHeight);
    const int top=standard.boxes[3].y;
    assert(standard.deviceAt(16,top,0,16)==0 && standard.deviceAt(210,top,0,16)==1);
    assert(standard.deviceAt(204,top,0,16)==-1 && standard.deviceAt(16,top+cardHeight,0,16)==-1);
    const auto selector=homeLayout(selectorSections,16,true);
    for (int n=0;n<16;++n) assert(selector.deviceAt(cardX(n)+2,cardY(n,78)+3,0,16)==n);
    assert(homeLayout({},16).count==0 && homeLayout({},16).deviceAt(16,80,0,16)==-1);
    assert(homeLayout(selectorSections,0).deviceAt(16,80,0,0)==-1);
    auto order=defaultHome;
    do {
        for (order.count=1; order.count<=4; ++order.count) {
            const auto page=homeLayout(order,16);
            assert(page.count==order.count && page.limit>=0);
            for (int i=0;i<page.count;++i) {
                const auto &box=page.boxes[i];
                assert(box.kind==order.items[i] && box.y>=78 && box.y+box.h-page.limit<=480);
                if (isChart(box.kind)) {
                    const bool adjacent=(i>0 && isChart(page.boxes[i-1].kind)) ||
                                        (i+1<page.count && isChart(page.boxes[i+1].kind));
                    assert(box.w==(adjacent?376:768));
                }
                if (box.kind==Section::Servers) {
                    assert(box.h==cardHeight);
                    for (int n=0;n<columns;++n) {
                        const int y=cardY(n,box.y)+3, offset=std::min(page.limit,std::max(0,y-78));
                        assert(page.deviceAt(cardX(n)+2,y-offset,offset,16)==n);
                    }
                }
            }
        }
    } while(std::next_permutation(order.items,order.items+4));
    assert(controlAt(switchLeft,114)==0 && controlAt(783,263)==0);
    assert(controlAt(switchX,264)<0 && controlAt(switchX,275)<0);
    assert(controlAt(switchX,276)==1 && controlAt(783,425)==1);
    assert(controlAt(784,114)<0 && controlAt(switchLeft-1,425)<0);
    assert(controlAt(switchX,113)<0 && controlAt(switchX,426)<0);
    const int limit=homeLayout({4,{Section::Day,Section::Power,Section::Month,Section::Servers}},16).limit;
    assert(limit>0);
    Gesture g;
    int offset = 0;
    auto frame = [&](Contact event, int x = 0, int y = 0) { return g.update(event,x,y,limit,offset); };
    assert(!frame(Contact::Down,50,405) && !frame(Contact::None));
    assert(!frame(Contact::Down,52,402) && frame(Contact::Up));
    assert(g.x == 50 && g.y == 405 && offset == 0);
    assert(!frame(Contact::Down,50,405) && !frame(Contact::Down,50,405-limit-1));
    assert(offset == limit && !frame(Contact::Up));
    assert(!frame(Contact::Down,50,160) && !frame(Contact::Down,50,160+limit+1));
    assert(offset == 0 && !frame(Contact::Down,50,320) && !frame(Contact::Up));
    offset = limit-5;
    assert(!frame(Contact::Down,50,420) && !frame(Contact::Down,50,310));
    assert(offset == limit && !frame(Contact::Up));
    assert(!frame(Contact::Down,540,40) && !frame(Contact::Down,540,400));
    assert(offset == limit && !frame(Contact::Up));
    assert(!frame(Contact::Down,50,400) && !frame(Contact::Cancel));
    assert(!frame(Contact::Down,50,398) && !frame(Contact::Up));
    assert(!frame(Contact::Down,50,400));
    g.cancel();
    assert(!frame(Contact::Up) && !frame(Contact::Up));
}
