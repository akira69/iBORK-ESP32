// Included with the other touchscreen views; both PIN prompts reuse the power keypad.
panel::DisplaySchedule schedule=panel::defaultDisplaySchedule(), draft;
panel::ScreenWake wake;
panel::BrightnessSlider brightnessSlider;
int periodIndex, brightness=100;
bool canDim, savingSchedule, cacheSaved=true;
const char *scheduleNote="";
const char *displayNote="";
const char *scheduleKey() { return live?"live-v1":"demo-v1"; }
void loadSchedule() {
    Preferences prefs;
    panel::DisplaySchedule saved{};
    if(!prefs.begin("display",true)) return;
    const int manual=prefs.getUChar("brightness",0);
    wake.manualBrightness=manual<=100?manual:0;
    if(prefs.getBytesLength(scheduleKey())==sizeof(saved) &&
       prefs.getBytes(scheduleKey(),&saved,sizeof(saved))==sizeof(saved) &&
       panel::validDisplaySchedule(saved)) schedule=saved;
}
bool rememberSchedule(const panel::DisplaySchedule &next) {
    if(cacheSaved && !memcmp(&schedule,&next,sizeof(next))) return true;
    Preferences prefs;
    cacheSaved=prefs.begin("display",false) &&
        prefs.putBytes(scheduleKey(),&next,sizeof(next))==sizeof(next);
    schedule=next;
    return cacheSaved;
}
void setScreenBrightness(int percent) {
    if(!canDim || brightness==percent) return;
    const bool wasOff=brightness==0;
    brightness=percent;
    if(!brightness) {
        if(view==ChangePin) cancelChangePin();
        else if(view==Pin || view==DisplayPin) {
            clearPin(); view=view==Pin?Detail:Schedule;
        }
    }
    if(wasOff) { checkedAt=millis()-30000; draw(); }
    display_hw::setBrightness(percent);
}
void serviceScreen() {
    if(view==DisplayPin && (!connected || pinIdle())) {
        clearPin(); view=Schedule; scheduleNote="PIN entry expired."; draw();
    }
    static uint32_t checked;
    if(!panel::elapsed(millis(),checked,250)) return;
    checked=millis();
    setScreenBrightness(!touchReady?100:wake.level(
        panel::brightnessAt(schedule,panel::localMinuteOfDay(time(nullptr))),checked));
}
bool wakeTouch(panel::Contact event) {
    const bool skip=wake.touch(event,brightness,millis());
    if(event==panel::Contact::Down && brightness<100 && !wake.manualBrightness) {
        gesture={};
        setScreenBrightness(100);
    }
    return skip;
}
String startText(int start) {
    char clock[6];
    snprintf(clock,sizeof(clock),"%02d:%02d",start/60,start%60);
    return clock;
}
String stateText(int level) {
    return level==0?String("Screen off"):level==100?String("Normal"):String("Dim ")+level+"%";
}
void openSchedule() {
    view=Schedule;
    if(savingSchedule) return;
    draft=schedule; periodIndex=0;
    scheduleNote="";
}
void scheduleButton(int x,int y,int w,const String &caption,bool selected=false) {
    box(x,y,w,48,selected?0x215448:tile);
    label(x+10,y+12,caption,selected?mint:ink);
}
void drawDisplay() {
    label(18,78,"Display",ink,2);
    label(20,132,"Brightness  "+String(wake.manualBrightness?wake.manualBrightness:brightness)+"%",ink,2);
    const int level=wake.manualBrightness?wake.manualBrightness:brightness;
    const int x=panel::BrightnessSlider::left+(constrain(level,1,100)-1)*510/99;
    lcd->fillRoundRect(40,204,510,8,4,grid);
    lcd->fillRoundRect(40,204,max(8,x-40),8,4,mint);
    lcd->fillCircle(x,208,14,mint);
    label(40,244,"1%",muted); label(508,244,"100%",muted);
    scheduleButton(604,184,174,"Use schedule",!wake.manualBrightness);
    label(20,280,wake.manualBrightness?"Manual brightness on this panel":"Following schedule; touch wakes for five minutes",muted);
    label(20,328,"Schedule",ink,2);
    label(20,368,"Four daily periods in panel local time",muted);
    scheduleButton(604,344,174,"Edit schedule");
    label(20,432,canDim?displayNote:"Backlight control unavailable.",gold);
}
void saveBrightness() {
    Preferences prefs;
    displayNote=prefs.begin("display",false) && prefs.putUChar("brightness",wake.manualBrightness)==1?
        "":"Applied; local save failed. Adjust again to retry.";
}
bool slideBrightness(panel::Contact event,int x,int y) {
    if(view!=Display || !canDim) { brightnessSlider={}; return false; }
    const int previous=wake.manualBrightness;
    if(!brightnessSlider.update(event,x,y,wake.manualBrightness)) return false;
    gesture={};
    if(event==panel::Contact::Up) { saveBrightness(); draw(); }
    else if(previous!=wake.manualBrightness) {
        displayNote="";
        setScreenBrightness(wake.manualBrightness);
        draw();
    }
    return true;
}
void tapDisplay(int x,int y) {
    if(x>=604 && x<778 && y>=184 && y<232 && canDim) {
        wake={};
        saveBrightness();
        setScreenBrightness(panel::brightnessAt(schedule,panel::localMinuteOfDay(time(nullptr))));
    } else if(x>=604 && x<778 && y>=344 && y<392) openSchedule();
    else return;
    draw();
}
void drawSchedule() {
    label(18,78,"Display schedule",ink,2);
    label(19,116,"Start time (panel local time)",muted);
    for(int i=0;i<4;++i) {
        const auto &p=draft.periods[i];
        scheduleButton(16,147+i*58,296,startText(p.start)+"     "+stateText(p.brightness),i==periodIndex);
    }
    const auto &p=draft.periods[periodIndex];
    label(332,118,"Adjust start",muted);
    scheduleButton(332,147,102,"Hour -"); scheduleButton(444,147,102,"Hour +");
    scheduleButton(564,147,102,"Min -"); scheduleButton(676,147,102,"Min +");
    label(332,209,"Screen state",muted);
    scheduleButton(332,236,134,"Normal",p.brightness==100);
    scheduleButton(481,236,134,"Dim",p.brightness>0 && p.brightness<100);
    scheduleButton(630,236,148,"Screen off",p.brightness==0);
    if(p.brightness>0 && p.brightness<100) {
        label(332,295,"Dim brightness  "+String(p.brightness)+"%",muted);
        const char *steps[]={"-10%","-1%","+1%","+10%"};
        for(int i=0;i<4;++i) {
            scheduleButton(332+i*112,321,102,steps[i]);
        }
    }
    const char *hint=scheduleNote;
    if(!*hint) {
        if(!canDim) hint="Backlight control unavailable.";
        else if(live && !connected) hint="Reconnect to iBORK to save.";
        else if(live && !fresh()) hint="Waiting for fresh iBORK readings.";
        else if(live && !fleetData.displaySchedule.revision) hint="Update iBORK to sync the display schedule.";
        else if(live && !fleetData.canEditDisplay) hint="Set a panel PIN on iBORK to save.";
        else if(panel::localMinuteOfDay(time(nullptr))<0) hint="Normal until the clock syncs.";
    }
    label(19,402,hint,gold);
    scheduleButton(604,412,174,savingSchedule?"Saving...":"Save schedule");
}
void drawSchedulePin() {
    label(20,89,"Save display schedule",ink,2);
    label(22,165,"Panel PIN",gold,2);
    drawPinInput(); drawPinKeys("Save schedule",pinLen>=6);
}
void tapSchedule(int x,int y) {
    if(view==DisplayPin) {
        if(editPinKey(x,y)) {
            pinLastInput=millis(); drawPinInput(); drawPinSubmit("Save schedule",pinLen>=6);
        } else if(x>=426 && x<759 && y>=369 && y<443 && pinLen>=6) {
            savingSchedule=!requestActive && startPanelDisplay(draft,pin);
            if(savingSchedule) requestActive=true;
            clearPin(); view=Schedule;
            scheduleNote=savingSchedule?"Saving...":"Server busy. Try again.";
            draw();
        }
        return;
    }
    if(savingSchedule) return;
    auto &p=draft.periods[periodIndex];
    if(x>=16 && x<312 && y>=147 && y<369 && (y-147)%58<48) {
        periodIndex=(y-147)/58;
    } else if(y>=147 && y<195 && x>=332 && x<778) {
        int hour=p.start/60,minute=p.start%60;
        if(x<434) hour=(hour+23)%24;
        else if(x>=444 && x<546) hour=(hour+1)%24;
        else if(x>=564 && x<666) minute=(minute+59)%60;
        else if(x>=676) minute=(minute+1)%60;
        else return;
        p.start=hour*60+minute;
    } else if(y>=236 && y<284 && x>=332 && x<778) {
        if(x<466) p.brightness=100;
        else if(x>=481 && x<615) { if(p.brightness==0 || p.brightness==100) p.brightness=25; }
        else if(x>=630) p.brightness=0;
        else return;
    } else if(y>=321 && y<369 && x>=332 && x<770 && (x-332)%112<102 &&
              p.brightness>0 && p.brightness<100) {
        const int steps[]={-10,-1,1,10};
        p.brightness=constrain(p.brightness+steps[(x-332)/112],1,99);
    } else if(x>=604 && x<778 && y>=412 && y<460) {
        if(!panel::validDisplaySchedule(draft)) scheduleNote="Choose four different start times.";
        else if(!live) scheduleNote=rememberSchedule(draft)?"Saved on this panel.":"Applied; local save failed.";
        else if(!connected || !fleetData.canEditDisplay || !fleetData.displaySchedule.revision) scheduleNote="Connect to iBORK with a panel PIN to save.";
        else if(draft.revision!=schedule.revision) scheduleNote="Schedule changed. Reopen to load it.";
        else if(requestActive) scheduleNote="Refreshing. Tap Save again shortly.";
        else { clearPin(); pinLastInput=millis(); view=DisplayPin; }
        draw(); return;
    } else return;
    scheduleNote="";
    draw();
}
void finishSchedule(PanelStatus status) {
    savingSchedule=false;
    const char *result;
    if(status==PanelStatus::Ok) {
        ++draft.revision;
        result=rememberSchedule(draft)?"Schedule saved.":"Saved on iBORK; local cache unavailable.";
    } else if(status==PanelStatus::Stale) result="Schedule changed. Reopen after refresh.";
    else if(status==PanelStatus::Denied) result="Check the panel PIN and permissions.";
    else if(status==PanelStatus::Limited) result="Too many attempts. Try later.";
    else result="Save unconfirmed. Check after refresh.";
    scheduleNote=changePinMessage=result;
    checkedAt=millis()-30000;
}
