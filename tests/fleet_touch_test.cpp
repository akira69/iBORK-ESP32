#include "../src/fleet_touch.hpp"
#include <algorithm>
#include <cassert>

int main() {
    using namespace panel;
    const auto standard=homeLayout(defaultHome,16);
    const int limit=standard.limit;
    assert(limit==48 && homeLayout(defaultHome,12).limit==0);
    assert(standard.deviceAt(16,310,0,16)==0 && standard.deviceAt(210,310,0,16)==1);
    assert(standard.deviceAt(204,310,0,16)==-1 && standard.deviceAt(16,360,0,16)==-1);
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
    Sections widgets{4,{Section::Temperature,Section::Controls,Section::Fan,Section::Power}};
    assert(widgetTop(widgets,true,Section::Controls)==230 && widgetTop(widgets,false,Section::Controls)<0);
    assert(widgetTop(widgets,false,Section::Fan)==230 && widgetTop({},true,Section::Controls)<0);
    assert(controlAt(16,230,230)==0 && controlAt(408,287,230)==1 && controlAt(407,230,230)<0);
    assert(controlAt(392,230,230)<0 && controlAt(18,288,230)<0 && controlAt(18,230,-1)<0);
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
