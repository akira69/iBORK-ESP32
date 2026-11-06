// Included inside the display namespace, sharing its drawing and touch primitives.
enum WiFiScreen { Networks, Password, Joining };
WiFiScreen wifiScreen=Networks;
int wifiPage, scanCount=WIFI_SCAN_FAILED, keyMode;
uint32_t scanStarted, joinStarted;
String wifiName, wifiMessage;
char passphrase[64]{};
size_t passLen;
bool joining, resumeAfterScan;
wifi_config_t previousWiFi{};
const char *keyboard[][4]={
    {"1234567890","qwertyuiop","asdfghjkl","zxcvbnm"},
    {"1234567890","QWERTYUIOP","ASDFGHJKL","ZXCVBNM"},
    {"!@#$%^&*()","-_=+[]{}|\\",";:'\",.<>/?","`~"}
};
void forgetPass() { mbedtls_platform_zeroize(passphrase,sizeof(passphrase)); passLen=0; }
void scanWiFi() {
    forgetPass(); wifiScreen=Networks; wifiPage=0; wifiMessage="";
    if (scanCount == WIFI_SCAN_RUNNING) return;
    WiFi.scanDelete();
    esp_err_t result = esp_wifi_scan_start(nullptr, false);
    if (result == ESP_ERR_WIFI_STATE && WiFi.status() != WL_CONNECTED) {
        // The radio cannot scan while trying to associate with an old network.
        resumeAfterScan=WiFi.getAutoReconnect();
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        delay(150);
        result = esp_wifi_scan_start(nullptr, false);
    }
    scanCount = result == ESP_OK ? WIFI_SCAN_RUNNING : WIFI_SCAN_FAILED;
    Serial.printf("iBORK scan start: %s\n", esp_err_to_name(result));
    scanStarted=millis();
}
void joinWiFi() {
    WiFi.scanDelete();
    if(esp_wifi_get_config(WIFI_IF_STA,&previousWiFi)!=ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK) {
        mbedtls_platform_zeroize(&previousWiFi,sizeof(previousWiFi));
        scanWiFi(); wifiMessage="Could not start connection"; return;
    }
    joining=true; joinStarted=millis(); wifiScreen=Joining; wifiMessage="";
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifiName.c_str(),passphrase);
    forgetPass();
}
void finishJoin(bool success) {
    joining=false;
    wifi_config_t joined{};
    bool saved=success && esp_wifi_get_config(WIFI_IF_STA,&joined)==ESP_OK;
    if(!success) { WiFi.setAutoReconnect(false); WiFi.disconnect(false,false); }
    bool storage=esp_wifi_set_storage(WIFI_STORAGE_FLASH)==ESP_OK;
    if(saved) saved=storage && esp_wifi_set_config(WIFI_IF_STA,&joined)==ESP_OK;
    if(!success && esp_wifi_set_config(WIFI_IF_STA,&previousWiFi)==ESP_OK && previousWiFi.sta.ssid[0]) {
        WiFi.setAutoReconnect(true);
        esp_wifi_connect();
    }
    mbedtls_platform_zeroize(&joined,sizeof(joined));
    mbedtls_platform_zeroize(&previousWiFi,sizeof(previousWiFi));
    if(view==Network) scanWiFi(); else wifiScreen=Networks;
    wifiMessage=success?(saved?"Connected: "+WiFi.localIP().toString():"Connected; could not save network"):
                        "Join failed. Check the password and try again.";
    if(view==Network) draw();
}
void networkView() {
    if(wifiScreen==Password) {
        label(18,80,"Wi-Fi password",ink,2);
        label(20,118,wifiName.substring(0,34),muted);
        String mask; for(size_t i=0;i<min(passLen,size_t(20));++i) mask+="*";
        label(400,91,mask,mint,2);
        for(int r=0;r<4;++r) for(int c=0;keyboard[keyMode][r][c];++c) {
            int x=16+77*c,y=151+57*r;
            box(x,y,70,51); label(x+23,y+9,String(keyboard[keyMode][r][c]),ink,2);
        }
        const char *actions[]={"Cancel","Shift","#+=","Space","Delete","Join"};
        for(int i=0;i<6;++i) { int x=16+128*i; box(x,391,120,68); label(x+13,409,actions[i],i==5 && passLen<8?muted:ink,2); }
    } else if(wifiScreen==Joining) {
        label(20,112,"Connecting to Wi-Fi",ink,3);
        label(22,190,wifiName,muted,2);
        label(22,252,"Waiting for a network address (up to 25 seconds).",muted);
    } else {
        label(20,77,"Wi-Fi networks",ink,2);
        label(250,87,wifiMessage.length()?wifiMessage:(connected?WiFi.localIP().toString():"Select your network"),muted);
        if(scanCount<0) label(24,157,scanCount==WIFI_SCAN_RUNNING?"Scanning...":"Scan unavailable. Tap Rescan.",gold);
        else if(scanCount==0) label(24,157,"No networks found. Tap Rescan.",muted);
        for(int row=0;row<5 && wifiPage*5+row<scanCount;++row) {
            int i=wifiPage*5+row,y=117+52*row;
            box(16,y,766,47);
            lcd.setClipRect(28,y,630,47);
            label(32,y+9,WiFi.SSID(i),ink,2); lcd.clearClipRect();
            label(671,y+14,String(WiFi.RSSI(i))+" dBm",muted);
        }
        const char *actions[]={"Rescan","Previous","Next"};
        for(int i=0;i<3;++i) { int x=16+256*i; box(x,398,245,60); label(x+12,411,actions[i],ink,2); }
    }
}
bool tapNetwork(int x,int y) {
    if(wifiScreen==Password) {
        if(y>=151 && y<379 && x>=16 && x<782 && (x-16)%77<70 && (y-151)%57<51) {
            const char *row=keyboard[keyMode][(y-151)/57];
            size_t c=(x-16)/77;
            if(c<strlen(row) && passLen<63) { passphrase[passLen++]=row[c]; passphrase[passLen]=0; }
        } else if(y>=391 && y<459 && x>=16 && x<784 && (x-16)%128<120) {
            switch((x-16)/128) {
                case 0: forgetPass(); wifiScreen=Networks; break;
                case 1: keyMode=keyMode==1?0:1; break;
                case 2: keyMode=keyMode==2?0:2; break;
                case 3: if(passLen<63) { passphrase[passLen++]=' '; passphrase[passLen]=0; } break;
                case 4: if(passLen) passphrase[--passLen]=0; break;
                case 5: if(passLen>=8) joinWiFi(); break;
            }
        } else return false;
    } else if(wifiScreen==Networks && x>=16 && x<782 && y>=117 && y<377 && (y-117)%52<47) {
        int i=wifiPage*5+(y-117)/52;
        if(i>=scanCount) return false;
        auto auth=WiFi.encryptionType(i);
        if(auth!=WIFI_AUTH_WPA2_PSK && auth!=WIFI_AUTH_WPA_WPA2_PSK && auth!=WIFI_AUTH_WPA2_WPA3_PSK && auth!=WIFI_AUTH_WPA3_PSK) {
            wifiMessage="Select a WPA2/WPA3 Personal network."; return true;
        }
        wifiName=WiFi.SSID(i); keyMode=0; forgetPass(); wifiScreen=Password;
    } else if(wifiScreen==Networks && x>=16 && x<784 && y>=398 && y<458 && (x-16)%256<245) {
        switch((x-16)/256) {
            case 0: scanWiFi(); break;
            case 1: if(wifiPage) --wifiPage; break;
            case 2: if((wifiPage+1)*5<scanCount) ++wifiPage; break;
        }
    } else return false;
    return true;
}
void serviceWiFi() {

    if(wifiScreen==Networks && scanCount==WIFI_SCAN_RUNNING) {
        const int found = WiFi.scanComplete();
        // The Arduino scan-done event owns the results even for native scans.
        if (found >= 0) {
            scanCount = found;
            Serial.printf("iBORK scan complete: %d networks\n", found);
        } else if (millis() - scanStarted > 15000) {
            esp_wifi_scan_stop();
            scanCount = WIFI_SCAN_FAILED;
            Serial.println("iBORK scan timed out");
        }
        if(scanCount!=WIFI_SCAN_RUNNING && view==Network) draw();
    }
    if(resumeAfterScan && scanCount!=WIFI_SCAN_RUNNING) {
        resumeAfterScan=false;
        WiFi.setAutoReconnect(true);
        if(WiFi.status()!=WL_CONNECTED) esp_wifi_connect();
    }
    if(joining) {
        bool success=WiFi.status()==WL_CONNECTED && WiFi.SSID()==wifiName && WiFi.localIP()!=IPAddress();
        if(success || panel::elapsed(millis(),joinStarted,25000)) finishJoin(success);
    }
}
