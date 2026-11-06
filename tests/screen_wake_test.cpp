#include "../src/screen_wake.hpp"
#include <cassert>
#include <initializer_list>
#include <limits>

int main() {
    using namespace panel;
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
