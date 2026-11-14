#include "../src/panel_api.hpp"
#include <cassert>
#include <string>

namespace {
enum View { Settings, ChangePin };
View view=Settings;
bool live=true, connected=true, requestActive=false;
unsigned long ticks;
char pin[13]{};
size_t pinLen;
int submissions;
bool canSubmit=true;
const char *keys[]={"1","2","3","4","5","6","7","8","9","Clear","0","Del"};
constexpr int ink=1, muted=2, gold=3, bg=4, tile=5, mint=6;
struct {
    void fillRect(int,int,int,int,int) {}
    void drawRoundRect(int,int,int,int,int,int) {}
} screen;
auto *lcd=&screen;
void box(int,int,int,int,int=0) {}
std::string enteredMask;
void label(int x,int y,const char *value,int,int=1) { if(x==22 && y==249) enteredMask=value; }
void draw() {}
uint32_t millis() { return ticks; }
void mbedtls_platform_zeroize(void *p,size_t n) { memset(p,0,n); }
void clearPin() { memset(pin,0,sizeof(pin)); pinLen=0; }
bool startPanelPinChange(const char *current,const char *replacement) {
    if(!canSubmit) return false;
    assert(!strcmp(current,"012345") && !strcmp(replacement,"111111"));
    ++submissions;
    return true;
}
#include "../src/pin_change.inc"
void input(const char *digits) {
    while(*digits) {
        int k=*digits=='0'?10:*digits-'1';
        assert(tapChangePin(440+115*(k%3),100+69*(k/3)));
        ++digits;
    }
}
bool cleared(const char (&s)[13]) {
    for (char c : s) if(c) return false;
    return true;
}
void proceed() { assert(tapChangePin(500,400)); }
}
int main() {
    assert(!strcmp(panel::requestLine(PanelRequest::Read),"GET /api/v1/panel"));
    assert(!strcmp(panel::requestLine(PanelRequest::Power),"POST /api/v1/panel/power"));
    assert(!strcmp(panel::requestLine(PanelRequest::ChangePin),"POST /api/v1/panel/pin"));
    using R=PanelRequest; using S=PanelStatus;
    static_assert(panel::responseCapacity(R::Read)==264*1024,"snapshot limit");
    for(auto kind:{R::Power,R::ChangePin,R::Display}) assert(panel::responseCapacity(kind)==1024);
    assert(panel::responseStatus(R::ChangePin,404)==S::Unsupported);
    assert(panel::responseStatus(R::ChangePin,405)==S::Unsupported);
    assert(panel::responseStatus(R::ChangePin,401)==S::Unauthorized);
    assert(panel::responseStatus(R::ChangePin,403)==S::Denied);
    assert(panel::responseStatus(R::ChangePin,429)==S::Limited);
    assert(panel::responseStatus(R::ChangePin,400)==S::Rejected);
    assert(panel::responseStatus(R::ChangePin,503)==S::Network);
    assert(panel::responseStatus(R::ChangePin,200)==S::Ok);
    assert(panel::responseStatus(R::Power,404)==S::Network);
    assert(panel::responseStatus(R::Read,401)==S::Denied);
    connected=false; openChangePin(); assert(view==Settings);
    connected=true; live=false; openChangePin(); assert(view==Settings);
    live=true; openChangePin(); assert(view==ChangePin);
    input("012"); drawChangePin(); assert(enteredMask=="***");
    tapChangePin(500,400); assert(submissions==0 && pinLen==3);
    input("345"); proceed(); assert(cleared(pin) && !strcmp(currentPin,"012345"));
    input("012345"); proceed(); assert(!submissions && !pinLen);
    input("111111"); proceed();
    assert(view==Settings && requestActive && submissions==1 && cleared(pin) && cleared(currentPin));
    openChangePin(); assert(view==Settings);
    finishChangePin(PanelStatus::Network); requestActive=false;
    assert(strstr(changePinMessage,"unknown"));
    finishChangePin(PanelStatus::Unsupported); assert(strstr(changePinMessage,"support"));
    finishChangePin(PanelStatus::Ok); assert(strstr(changePinMessage,"changed"));
    openChangePin(); input("012345"); proceed(); input("12"); cancelChangePin();
    assert(view==Settings && cleared(pin) && cleared(currentPin));
    openChangePin(); input("012345"); proceed(); input("12");
    ticks+=60000; serviceChangePin(); assert(view==Settings && cleared(pin) && cleared(currentPin));
    openChangePin(); input("012345"); proceed(); connected=false; serviceChangePin();
    assert(view==Settings && cleared(currentPin) && submissions==1);
    connected=true; openChangePin(); input("012345"); proceed(); input("111111");
    canSubmit=false; proceed(); assert(!requestActive && cleared(pin) && cleared(currentPin) && submissions==1);
}
