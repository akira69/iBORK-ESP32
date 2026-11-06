#include "panel_api.hpp"
#include <http_parser.h>
#include <WiFiClientSecure.h>
#include <Arduino.h>
#include <atomic>
#include <memory>
#include <mbedtls/platform_util.h>
#if __has_include("panel_local.h")
#include "panel_local.h"
#endif
#ifndef IBORK_PANEL_URL
#define IBORK_PANEL_URL ""
#endif
#ifndef IBORK_PANEL_CA
#define IBORK_PANEL_CA ""
#endif
#ifndef IBORK_PANEL_TOKEN
#define IBORK_PANEL_TOKEN ""
#endif
#ifndef IBORK_PANEL_NTP
#define IBORK_PANEL_NTP "pool.ntp.org"
#endif
#ifndef IBORK_PANEL_TZ
#define IBORK_PANEL_TZ "UTC0"
#endif
#ifndef IBORK_PANEL_IP
#define IBORK_PANEL_IP ""
#endif

namespace {
using namespace panel;
constexpr uint32_t timeoutMs = 5000;
char body[220], readDevice[33];
PanelData nextData;
PanelStatus result;
PanelRequest operation;
std::atomic<bool> finished{false};
TaskHandle_t worker;
bool busy;
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

// Arduino's shipped ESP-IDF disables MBEDTLS_HAVE_TIME_DATE. Enforce validity
// for the presented chain and the configured anchor before sending the bearer.
uint64_t timestamp(int y,int m,int d,int h,int min,int sec) {
    return (((((uint64_t(y)*100+m)*100+d)*100+h)*100+min)*100+sec);
}
uint64_t timestamp(const mbedtls_x509_time &t) {
    return timestamp(t.year,t.mon,t.day,t.hour,t.min,t.sec);
}
bool current(const mbedtls_x509_crt *cert, uint64_t now) {
    if (!cert) return false;
    for (;cert;cert=cert->next)
        if (now<timestamp(cert->valid_from) || now>timestamp(cert->valid_to)) return false;
    return true;
}
class PanelTls final : public WiFiClientSecure {
public:
    int connect(const char *host, uint16_t port, int32_t timeout) override {
        _timeout=timeout;
        IPAddress address;
        if (*IBORK_PANEL_IP ? !address.fromString(IBORK_PANEL_IP) : !WiFi.hostByName(host,address)) return 0;
        if (!WiFiClientSecure::connect(address,port,host,IBORK_PANEL_CA,nullptr,nullptr)) return 0;
        time_t now=time(nullptr);
        struct tm t{};
        gmtime_r(&now,&t);
        uint64_t utc=timestamp(t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec);
        mbedtls_x509_crt anchor;
        mbedtls_x509_crt_init(&anchor);
        bool valid=mbedtls_x509_crt_parse(&anchor,reinterpret_cast<const uint8_t *>(IBORK_PANEL_CA),strlen(IBORK_PANEL_CA)+1)==0 &&
                   current(&anchor,utc) && current(getPeerCertificate(),utc);
        mbedtls_x509_crt_free(&anchor);
        if (!valid) stop();
        return valid;
    }
};
struct Response {
    char *data;
    size_t capacity, used;
    bool complete;
};
int readBody(http_parser *parser,const char *data,size_t size) {
    auto &response=*static_cast<Response *>(parser->data);
    if(size>response.capacity-response.used) return 1;
    memcpy(response.data+response.used,data,size); response.used+=size;
    return 0;
}
PanelStatus request() {
    String origin=IBORK_PANEL_URL;
    if(origin.endsWith("/")) origin.remove(origin.length()-1);
    http_parser_url url{};
    if(!origin.startsWith("https://") || strlen(IBORK_PANEL_TOKEN)!=64 ||
       strspn(IBORK_PANEL_TOKEN,"0123456789abcdefABCDEF")!=64 || !*IBORK_PANEL_CA ||
       http_parser_parse_url(origin.c_str(),origin.length(),false,&url)!=0 ||
       !(url.field_set&(1<<UF_HOST)) || (url.field_set & ~((1<<UF_SCHEMA)|(1<<UF_HOST)|(1<<UF_PORT)))) return PanelStatus::Config;
    if(time(nullptr)<1704067200) return PanelStatus::Clock;
    auto host=url.field_data[UF_HOST];
    PanelTls tls;
    tls.setHandshakeTimeout(5);
    String hostname=origin.substring(host.off,host.off+host.len);
    const uint16_t port=(url.field_set & (1<<UF_PORT)) ? url.port : 443;
    if(!tls.connect(hostname.c_str(),port,timeoutMs)) return PanelStatus::Network;
    tls.setTimeout(5);
    String headers=requestLine(operation);
    if (operation==PanelRequest::Read && *readDevice) { headers+="?detail="; headers+=readDevice; }
    headers+=" HTTP/1.1\r\nHost: "+origin.substring(8)+"\r\nConnection: close\r\nAuthorization: Bearer ";
    headers+=IBORK_PANEL_TOKEN;
    if(operation!=PanelRequest::Read) headers+="\r\nContent-Type: application/json\r\nContent-Length: "+String(strlen(body));
    headers+="\r\n\r\n";
    if(tls.print(headers)!=headers.length() ||
       (operation!=PanelRequest::Read && tls.print(body)!=strlen(body))) return PanelStatus::Network;
    mbedtls_platform_zeroize(body,sizeof(body));

    const size_t capacity=responseCapacity(operation);
    std::unique_ptr<char,decltype(&free)> buf(static_cast<char *>(ps_malloc(capacity+1)),free);
    if(!buf) return PanelStatus::Invalid;
    Response response{buf.get(),capacity,0,false};
    http_parser parser{};
    http_parser_init(&parser,HTTP_RESPONSE); parser.data=&response;
    http_parser_settings settings{};
    settings.on_body=readBody;
    settings.on_message_complete=[](http_parser *p)->int {
        static_cast<Response *>(p->data)->complete=true; http_parser_pause(p,1); return 0;
    };
    size_t received=0;
    uint32_t started=millis();
    while(!response.complete) {
        if(elapsed(millis(),started,timeoutMs)) return PanelStatus::Network;
        char chunk[512];
        int n=tls.read(reinterpret_cast<uint8_t *>(chunk),sizeof(chunk));
        if(n<=0 && tls.connected()) { delay(5); continue; }
        size_t count=n>0?size_t(n):0;
        received+=count;
        if(received>capacity+8192) return PanelStatus::Invalid;
        http_parser_execute(&parser,&settings,chunk,count);
        if(!response.complete && (!count || HTTP_PARSER_ERRNO(&parser)!=HPE_OK)) return PanelStatus::Network;
    }
    const PanelStatus status=responseStatus(operation,parser.status_code);
    if(status!=PanelStatus::Ok) return status;
    if(!shallowJson(buf.get(),response.used)) return PanelStatus::Invalid;
    buf.get()[response.used]=0;
    Json json(cJSON_ParseWithOpts(buf.get(),nullptr,true),cJSON_Delete);
    if(!json || (operation!=PanelRequest::Read? !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json.get(),"ok")):!decode(json.get(),nextData,time(nullptr),readDevice))) return PanelStatus::Invalid;
    return PanelStatus::Ok;
}
void run(void *) {
    // The largest inventory includes sixteen base64 images. Keep its JSON tree in PSRAM.
    cJSON_Hooks hooks{ps_malloc,free};
    cJSON_InitHooks(&hooks);
    for (;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        result=request();
        mbedtls_platform_zeroize(body,sizeof(body));
        finished.store(true);
    }
}
bool begin(PanelRequest kind) {
    if (!worker && xTaskCreate(run,"panel-https",12288,nullptr,1,&worker)!=pdPASS) {
        mbedtls_platform_zeroize(body,sizeof(body));
        return false;
    }
    operation=kind; busy=true;
    xTaskNotifyGive(worker);
    return true;
}
}
bool panelConfigured() { return *IBORK_PANEL_URL || *IBORK_PANEL_TOKEN || *IBORK_PANEL_CA; }
void startPanelClock() { configTzTime(IBORK_PANEL_TZ,IBORK_PANEL_NTP,"time.cloudflare.com"); }
bool startPanelRead(const char *detailId) {
    if(busy || (detailId && !validId(detailId))) return false;
    strlcpy(readDevice,detailId?detailId:"",sizeof(readDevice));
    return begin(PanelRequest::Read);
}
bool startPanelDisplay(const panel::DisplaySchedule &schedule,const char *pin) {
    if (busy || !panel::displayRequestBody(body,schedule,pin)) return false;
    return begin(PanelRequest::Display);
}
bool startPanelPower(const char *id,const char *action,const char *pin) {
    if (busy || !validId(id) || (strcmp(action,"on") && strcmp(action,"soft")) ||
        !validPin(pin)) return false;
    snprintf(body,sizeof(body),"{\"id\":\"%s\",\"action\":\"%s\",\"pin\":\"%s\",\"confirm\":\"%s\"}",id,action,pin,action);
    return begin(PanelRequest::Power);
}
bool startPanelPinChange(const char *currentPin,const char *newPin) {
    if(busy || !validPin(currentPin) || !validPin(newPin) || !strcmp(currentPin,newPin)) return false;
    snprintf(body,sizeof(body),"{\"currentPin\":\"%s\",\"newPin\":\"%s\"}",currentPin,newPin);
    return begin(PanelRequest::ChangePin);
}
bool takePanelResult(PanelStatus &status,PanelRequest &kind,PanelData &data) {
    if(!busy || !finished.load()) return false;
    status=result; kind=operation;
    if (kind==PanelRequest::Read && status==PanelStatus::Ok) data=std::move(nextData);
    finished.store(false); busy=false;
    return true;
}
