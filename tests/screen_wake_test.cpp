#include "../src/screen_wake.hpp"
#include <cassert>
#include <initializer_list>
#include <limits>

int main() {
    using namespace panel;
    TouchRetry touch;
    const uint32_t faultAt=std::numeric_limits<uint32_t>::max()-100;
    assert(!touch.failed(true,faultAt-4));
    assert(!touch.failed(true,faultAt-3));
    assert(!touch.failed(false,faultAt-2));
    assert(!touch.failed(true,faultAt-2));
    assert(!touch.failed(true,faultAt-1));
    assert(touch.failed(true,faultAt));
    assert(!touch.due(faultAt+4999) && touch.due(faultAt+5000) && !touch.due(faultAt+5001));
    ScreenWake manual;
    manual.manualBrightness=25;
    assert(!manual.touch(Contact::Down,25,1));
    assert(!manual.touch(Contact::Up,25,2));
    assert(manual.level(0,3)==25);
    assert(manual.level(100,300003)==25);
    manual.manualBrightness=1;
    assert(manual.level(100,300004)==10);
    manual.manualBrightness=100;
    assert(manual.level(0,300005)==100);
    manual.manualBrightness=0;
    assert(manual.level(25,300006)==25);
    assert(manual.touch(Contact::Down,25,300007));
    assert(manual.level(25,300008)==100);
    assert(manual.level(25,600007)==25);

    BrightnessSlider slider;
    int level=100;
    assert(!slider.update(Contact::Down,40,100,level));
    assert(!slider.update(Contact::Down,40,200,level) && level==100);
    assert(!slider.update(Contact::Up,40,100,level));
    assert(slider.update(Contact::Down,40,200,level) && level==10);
    assert(slider.update(Contact::Down,295,200,level) && level==55);
    assert(slider.update(Contact::Down,799,300,level) && level==100);
    assert(slider.update(Contact::Down,0,200,level) && level==10);
    assert(slider.update(Contact::Cancel,300,200,level));
    assert(slider.update(Contact::Down,550,200,level) && level==10);
    assert(slider.update(Contact::Up,0,0,level));
    assert(slider.update(Contact::Down,550,200,level) && level==100);
    assert(slider.update(Contact::Up,0,0,level));
    assert(!slider.update(Contact::Down,604,200,level));
    BrightnessSlider contrast;
    assert(contrast.update(Contact::Down,40,320,level,0,120) && level==0);
    assert(contrast.update(Contact::Down,295,320,level,0,120) && level==50);
    assert(contrast.update(Contact::Down,550,320,level,0,120) && level==100);
    assert(contrast.update(Contact::Up,550,320,level,0,120));
    for(int brightness : {0,25,99}) {
        ScreenWake wake;
        const uint32_t start=std::numeric_limits<uint32_t>::max()-1000;
        assert(wake.touch(Contact::Down,brightness,start));
        assert(wake.awake(start+1));
        assert(wake.touch(Contact::None,100,start+2));
        assert(wake.touch(Contact::Cancel,100,start+3));
        assert(wake.touch(Contact::Down,100,start+4));
        assert(wake.touch(Contact::Up,100,start+5));
        assert(!wake.touch(Contact::Down,100,start+600));
        assert(!wake.touch(Contact::Up,100,start+900));
        assert(!wake.touch(Contact::Up,100,start+290000));
        assert(wake.awake(start+300899));
        assert(!wake.awake(start+300900));
    }
}
