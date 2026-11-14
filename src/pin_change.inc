// Included alongside the power keypad so both screens share its masked input.
char currentPin[13]{};
const char *changePinMessage="";
uint32_t pinLastInput;
bool pinIdle() { return panel::elapsed(millis(),pinLastInput,60000); }

void drawPinInput() {
    char mask[]="************";
    mask[pinLen]=0;
    lcd->fillRect(20,245,390,52,bg);
    label(22,249,mask,ink,2);
}
void drawPinSubmit(const char *caption,bool enabled) {
    box(426,369,333,74);
    if(enabled) lcd->drawRoundRect(426,369,333,74,10,mint);
    label(442,387,caption,enabled?ink:muted,2);
}
void drawPinKeys(const char *caption,bool enabled) {
    for(int i=0;i<12;++i) {
        int x=426+(i%3)*115,y=86+(i/3)*69;
        box(x,y,103,58); label(x+20,y+12,keys[i],ink,2);
    }
    drawPinSubmit(caption,enabled);
}
bool editPinKey(int x,int y) {
    if(x<426 || x>=759 || y<86 || y>=351 || (x-426)%115>=103 || (y-86)%69>=58) return false;
    int key=(y-86)/69*3+(x-426)/115;
    if(key==9) clearPin();
    else if(key==11) { if(pinLen) pin[--pinLen]=0; }
    else if(pinLen<12) { pin[pinLen++]=keys[key][0]; pin[pinLen]=0; }
    return true;
}
void cancelChangePin() {
    mbedtls_platform_zeroize(currentPin,sizeof(currentPin));
    clearPin(); changePinMessage=""; view=Settings;
}
void openChangePin() {
    if(requestActive) { changePinMessage="Request in progress. Please wait."; return; }
    if(!live || !connected) {
        changePinMessage=live?"Connect to Wi-Fi first.":"Pair the panel with iBORK first.";
        return;
    }
    cancelChangePin();
    view=ChangePin; pinLastInput=millis();
}
const char *changePinSubmit() { return currentPin[0]?"Save PIN":"Next"; }
void drawChangePin() {
    label(20,89,"Change power PIN",ink,3);
    label(22,165,currentPin[0]?"New PIN":"Current PIN",gold,2);
    label(22,211,"Enter 6 to 12 digits",muted);
    drawPinInput();
    label(22,310,changePinMessage,gold);
    drawPinKeys(changePinSubmit(),pinLen>=6);
}
bool tapChangePin(int x,int y) {
    if(!connected || view!=ChangePin) return false;
    const bool wasReady=pinLen>=6;
    if(editPinKey(x,y)) {
        pinLastInput=millis();
        drawPinInput();
        if(wasReady!=(pinLen>=6)) drawPinSubmit(changePinSubmit(),pinLen>=6);
        return true;
    }
    if(x<426 || x>=759 || y<369 || y>=443 || !wasReady) return false;
    pinLastInput=millis();
    if(!currentPin[0]) {
        memcpy(currentPin,pin,pinLen+1);
        clearPin();
    } else if(!strcmp(currentPin,pin)) {
        clearPin(); changePinMessage="Choose a different PIN.";
    } else {
        bool sent=!requestActive && startPanelPinChange(currentPin,pin);
        cancelChangePin();
        if(sent) requestActive=true;
        changePinMessage=sent?"Changing power PIN...":"Could not send. Enter the PINs again.";
    }
    draw();
    return true;
}
void serviceChangePin() {
    if(view==ChangePin && (!connected || pinIdle())) {
        cancelChangePin();
        changePinMessage=connected?"PIN entry timed out.":"PIN entry canceled while offline.";
        draw();
    }
}
void finishChangePin(PanelStatus status) {
    switch(status) {
        case PanelStatus::Ok: changePinMessage="Power PIN changed."; break;
        case PanelStatus::Unauthorized: changePinMessage="Check the panel bearer token."; break;
        case PanelStatus::Denied: changePinMessage="PIN change denied. Check the current PIN and server version."; break;
        case PanelStatus::Limited: changePinMessage="Too many attempts. Wait before trying again."; break;
        case PanelStatus::Unsupported: changePinMessage="This server does not support PIN changes."; break;
        case PanelStatus::Rejected: changePinMessage="The server rejected the PIN change."; break;
        case PanelStatus::Config: case PanelStatus::Clock: changePinMessage="Check the panel configuration and clock."; break;
        default: changePinMessage="Outcome unknown. Check with the server administrator before retrying.";
    }
}
