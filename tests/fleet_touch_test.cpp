#include "../src/fleet_touch.hpp"
#include <algorithm>
#include <cassert>

int main() {
    using namespace panel;
    static_assert(!navHit(74,32) && !navHit(120,32) && navHit(198,10) && navHit(305,57) && !navHit(306,57));
    for(int i=0;i<4;++i) {
        const int x=paletteX(i), y=paletteY(i);
        assert(paletteAt(x,y)==i && paletteAt(x+paletteWidth-1,y+paletteHeight-1)==i);
        assert(paletteAt(x+paletteWidth,y)==-1 && paletteAt(x,y+paletteHeight)==-1);
    }
    assert(paletteAt(250,30)==-1 && paletteAt(750,479)==-1);
    const auto standard=homeLayout(defaultHome,16);
    const int limit=standard.limit;
    assert(limit==55 && homeLayout(defaultHome,12).limit==0);
    assert(standard.deviceAt(16,317,0,16)==0 && standard.deviceAt(210,317,0,16)==1);
    assert(standard.deviceAt(204,317,0,16)==-1 && standard.deviceAt(16,367,0,16)==-1);
    assert(standard.deviceAt(16,69,limit,16)==-1 && standard.deviceAt(16,480,limit,16)==-1);
    assert(standard.deviceAt(cardX(15),479,limit,16)==15 && standard.deviceAt(791,470,limit,16)==-1);
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
                    for (int n=0;n<16;++n) {
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
    Gesture g;
    int offset = 0;
    auto frame = [&](Contact event, int x = 0, int y = 0) { return g.update(event,x,y,limit,offset); };
    assert(!frame(Contact::Down,50,405) && !frame(Contact::None));
    assert(!frame(Contact::Down,52,402) && frame(Contact::Up));
    assert(g.x == 50 && g.y == 405 && offset == 0);
    assert(!frame(Contact::Down,50,405) && !frame(Contact::Down,50,325));
    assert(offset == limit && !frame(Contact::Up));
    assert(!frame(Contact::Down,50,320) && !frame(Contact::Down,50,460));
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
