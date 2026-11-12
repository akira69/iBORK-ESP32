#include "../src/panel_api.hpp"
#include <cassert>
#include <string>

namespace {
void displayTests() {
    auto schedule=panel::defaultDisplaySchedule();
    assert(panel::validDisplaySchedule(schedule));
    std::swap(schedule.periods[0],schedule.periods[3]);
    for (int minute=0; minute<1440; ++minute) {
        int expected=minute>=60 && minute<300 ? 0 : minute>=420 && minute<1140 ? 100 : 25;
        assert(panel::brightnessAt(schedule,minute)==expected);
    }
    assert(panel::brightnessAt(schedule,-1)==100 && panel::brightnessAt(schedule,1440)==100);
    assert(panel::localMinuteOfDay(0)==-1);
    setenv("TZ","STD0DST,M3.2.0,M11.1.0",1);
    tzset();
    assert(panel::localMinuteOfDay(1793494740)==119 && panel::localMinuteOfDay(1793494800)==60);
    assert(panel::localMinuteOfDay(1772935140)==119 && panel::localMinuteOfDay(1772935200)==180);
    schedule.revision=UINT32_MAX;
    for (int i=0; i<4; ++i) schedule.periods[i]={1439-i,100};
    char body[220]{};
    assert(!panel::displayRequestBody(body,schedule,"0000\"0"));
    assert(panel::displayRequestBody(body,schedule,"012345678901"));
    auto wire=cJSON_Parse(body);
    assert(wire && !strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(wire,"pin")),"012345678901"));
    assert(panel::decodeSchedule(wire).revision==UINT32_MAX);
    auto periods=cJSON_GetObjectItem(wire,"periods");
    auto last=cJSON_GetArrayItem(periods,3);
    for (auto invalid : {"-1","1440","0.1","1439","null","\"123\""}) {
        cJSON_ReplaceItemInObject(last,"start",cJSON_Parse(invalid));
        assert(!panel::decodeSchedule(wire).revision);
    }
    cJSON_ReplaceItemInObject(last,"start",cJSON_CreateNumber(999));
    cJSON_ReplaceItemInObject(last,"brightness",cJSON_CreateNumber(101));
    assert(!panel::decodeSchedule(wire).revision);
    cJSON_ReplaceItemInObject(last,"brightness",cJSON_CreateNumber(0));
    for (auto invalid : {"-1","0","4294967296","1.5","true"}) {
        cJSON_ReplaceItemInObject(wire,"revision",cJSON_Parse(invalid));
        assert(!panel::decodeSchedule(wire).revision);
    }
    cJSON_ReplaceItemInObject(wire,"revision",cJSON_CreateNumber(1));
    cJSON_DeleteItemFromArray(periods,3);
    assert(!panel::decodeSchedule(wire).revision);
    cJSON_Delete(wire);
    schedule.periods[0].start=schedule.periods[1].start;
    assert(!panel::validDisplaySchedule(schedule) && !panel::displayRequestBody(body,schedule,"012345"));
    assert(!strcmp(panel::requestLine(PanelRequest::Display),"PUT /api/v1/panel/display"));
    assert(panel::responseStatus(PanelRequest::Display,409)==PanelStatus::Stale);
    assert(panel::responseStatus(PanelRequest::Display,403)==PanelStatus::Denied);
}
void readRecoveryTests() {
    using S=PanelStatus;
    panel::ReadRetry retry;
    assert(retry.delayMs()==15000);
    assert(!retry.completed(S::Network) && retry.delayMs()==2000);
    assert(!retry.completed(S::Network) && retry.delayMs()==5000);
    assert(retry.completed(S::Network) && retry.delayMs()==15000);
    for(int i=0;i<8;++i) assert(!retry.completed(S::Network) && retry.delayMs()==30000);
    assert(!retry.completed(S::Ok) && retry.delayMs()==15000);
    assert(!retry.completed(S::Network) && retry.delayMs()==2000);
    assert(panel::responseStatus(PanelRequest::Read,503)==S::Unavailable);
    assert(!retry.completed(S::Unavailable) && retry.delayMs()==15000);
    assert(!strcmp(panel::readHealthText(S::Network,true,true),"Retrying iBORK"));
    assert(!strcmp(panel::readHealthText(S::Network,true,false),"Last readings"));
    assert(!strcmp(panel::readHealthText(S::Denied,true,false),"ACCESS DENIED"));
    assert(!*panel::readHealthText(S::Ok,true,true));
    const panel::RequestDeadline deadline{UINT32_MAX-5};
    assert(deadline.remaining(4)==11990 && deadline.remaining(11994)==0);
}
}
int main() {
    displayTests();
    readRecoveryTests();
    assert(std::abs(panel::monthlyCostEstimate(16.70,197.2f)-60.97363)<0.0001);
    assert(panel::monthlyCostEstimate(16.70,719.99f)>16.70);
    assert(panel::monthlyCostEstimate(16.70,720)<0);
    assert(panel::monthlyCostEstimate(16.70,0)<0);
    assert(panel::monthlyCostEstimate(-1,197.2f)<0);
    assert(panel::monthlyCostEstimate(0,197.2f)==0);
    const auto varying=panel::chartWattRange({-1,184.25f,186.75f});
    assert(varying.low==184 && varying.high==187);
    const auto flat=panel::chartWattRange({230,230});
    assert(flat.low==229 && flat.high==231);
    const auto zero=panel::chartWattRange({0,0});
    assert(zero.low==0 && zero.high==1);
    const auto missing=panel::chartWattRange({-1,-1});
    assert(missing.low==missing.high);
    const time_t now=1800000000;
    std::string readings="1";
    for(int i=1;i<95;++i) readings+=",null";
    const auto day=readings+",42";
    for(int i=95;i<119;++i) readings+=",null";
    std::string report="{\"capturedAt\":1800000000,\"total\":1,\"metered\":1,\"day\":["+day+
        "],\"month\":["+readings+",84],\"dayKwh\":0.25,\"monthKwh\":null,"
        "\"dayHours\":0.016667,\"monthHours\":24.5,\"devices\":[{\"id\":\"rack-1\",\"name\":\"Rack\","
        "\"model\":\"File server\",\"watts\":null,\"controls\":true}]}";
    PanelData data{};
    cJSON *root=cJSON_Parse(report.c_str());
    assert(root);
    bool valid=panel::decode(root,data,now);
    assert(valid);
    assert(data.day[95]==42 && data.month[119]==84);
    assert(panel::deviceIndex(data,"rack-1")==0 && panel::deviceIndex(data,"removed")==-1);
    std::swap(data.devices[0],data.devices[1]); data.count=2;
    assert(panel::deviceIndex(data,"rack-1")==1);
    assert(panel::decode(root,data,now) && data.count==1);
    assert(data.metered==1 && data.devices[0].watts<0 && data.day[1]<0 && data.dayHours>0 && data.dayHours<1);
    assert(!data.devices[0].metered);
    auto *firstDevice=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"devices"),0);
    cJSON_AddTrueToObject(firstDevice,"metered");
    assert(panel::decode(root,data,now) && data.devices[0].metered && data.devices[0].watts<0);
    cJSON_DeleteItemFromObject(firstDevice,"metered");
    assert(!data.displaySchedule.revision && !data.canEditDisplay && data.usdPerKwh<0);
    cJSON_AddNullToObject(root,"usdPerKwh");
    for (double price : {0.20, 0.0, 1000.0}) {
        cJSON_ReplaceItemInObject(root,"usdPerKwh",cJSON_CreateNumber(price));
        assert(panel::decode(root,data,now) && data.usdPerKwh==price);
    }
    for (auto invalid : {"null","true","\"0.20\"","-0.01","1000.00001","1e999"}) {
        cJSON_ReplaceItemInObject(root,"usdPerKwh",cJSON_Parse(invalid));
        assert(panel::decode(root,data,now) && data.usdPerKwh<0 && data.dayKwh==0.25);
    }
    cJSON_ReplaceItemInObject(root,"usdPerKwh",cJSON_CreateNumber(0.5));
    assert(panel::decode(root,data,now) && data.usdPerKwh==0.5);
    cJSON_DeleteItemFromObject(root,"usdPerKwh");
    assert(panel::decode(root,data,now) && data.usdPerKwh<0);
    char body[220]{};
    assert(panel::displayRequestBody(body,panel::defaultDisplaySchedule(),"123456"));
    cJSON_AddItemToObject(root,"displaySchedule",cJSON_Parse(body));
    cJSON_AddTrueToObject(root,"canEditDisplay");
    assert(panel::decode(root,data,now) && data.displaySchedule.revision==1 && data.canEditDisplay);
    cJSON_ReplaceItemInObject(root,"displaySchedule",cJSON_CreateFalse());
    assert(panel::decode(root,data,now) && !data.displaySchedule.revision && data.count==1);
    auto device=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"devices"),0);
    using panel::Section;
    assert(data.home.count==4 && data.devices[0].widgets.contains(Section::Controls));
    cJSON_AddItemToObject(root,"home",cJSON_Parse(R"(["servers","month","power"])"));
    cJSON_AddItemToObject(device,"widgets",cJSON_Parse(R"(["temperature","fan","power"])"));
    cJSON_AddItemToObject(root,"detail",cJSON_Parse(R"({"id":"rack-1","temperature":-2.5,"fan":0})"));
    assert(panel::decode(root,data,now,"rack-1") && data.home.items[0]==Section::Servers);
    assert(data.devices[0].widgets.count==3 && !data.devices[0].widgets.contains(Section::Controls));
    assert(!strcmp(data.detailId,"rack-1") && data.devices[0].temperature==-2.5 && data.devices[0].fan==0);
    assert(std::isnan(data.devices[0].fanMax)); // Older servers send only the minimum.
    cJSON_AddNumberToObject(cJSON_GetObjectItem(root,"detail"),"fanMax",2400);
    assert(panel::decode(root,data,now,"rack-1") && data.devices[0].fan==0 && data.devices[0].fanMax==2400);
    PanelData next{};
    assert(panel::decode(root,next,now,"rack-1"));
    next.devices[0].watts+=1; next.devices[0].temperature+=1; ++next.capturedAt;
    assert(panel::samePanelLayout(data,next));
    ++next.devices[0].widgets.count;
    assert(!panel::samePanelLayout(data,next));
    auto powerState=cJSON_AddStringToObject(cJSON_GetObjectItem(root,"detail"),"powerState","on");
    cJSON_AddTrueToObject(cJSON_GetObjectItem(root,"detail"),"powerFresh");
    assert(data.devices[0].powered==-1 && panel::decode(root,data,now,"rack-1") && data.devices[0].powered==1);
    cJSON_SetValuestring(powerState,"off");
    assert(panel::decode(root,data,now,"rack-1") && data.devices[0].powered==0);
    assert(panel::decode(root,next,now,"other") && next.devices[0].powered==-1);
    auto artwork=cJSON_AddStringToObject(cJSON_GetObjectItem(root,"detail"),"image","YWJj");
    assert(panel::decode(root,next,now,"rack-1") && next.devices[0].detailBytes==3 && !memcmp(next.devices[0].detailImage.get(),"abc",3));
    assert(panel::decode(root,next,now,"other") && !next.devices[0].detailBytes && !next.devices[0].detailImage);
    cJSON_SetValuestring(artwork,std::string(11000,'A').c_str());
    assert(panel::decode(root,next,now,"rack-1") && !next.devices[0].detailBytes && !next.devices[0].detailImage);
    auto group=cJSON_AddObjectToObject(root,"details");
    auto row=cJSON_AddObjectToObject(group,"rack-1");
    cJSON_AddStringToObject(row,"id","rack-1");
    cJSON_AddStringToObject(row,"image","YWJj");
    cJSON_AddStringToObject(row,"powerState","on");
    cJSON_AddFalseToObject(row,"powerFresh");
    cJSON_AddTrueToObject(row,"pending");
    cJSON_AddNumberToObject(root,"artRevision",123);
    PanelData cold{},warm{};
    assert(panel::decode(root,cold,now) && cold.waiting && cold.devices[0].detailBytes==3 && cold.devices[0].powered==1);
    assert(!cold.devices[0].confirmed);
    cJSON_ReplaceItemInObject(row,"image",cJSON_CreateString("broken"));
    assert(panel::decode(root,warm,now) && warm.devices[0].powered==1 && !warm.devices[0].detailImage);
    cJSON_DeleteItemFromObject(row,"image");
    cJSON_DeleteItemFromObject(row,"powerState");
    assert(panel::decode(root,warm,now));
    panel::retainDetails(warm,cold);
    assert(warm.devices[0].detailBytes==3 && warm.devices[0].detailImage && warm.devices[0].powered==1 && !warm.devices[0].confirmed);
    auto *compact=cJSON_Duplicate(root,true);
    for(const char *key:{"day","month","dayKwh","monthKwh","dayHours","monthHours"}) cJSON_DeleteItemFromObject(compact,key);
    assert(!panel::decode(compact,cold,now));
    assert(panel::decode(compact,cold,now,nullptr,true) && !cold.chartAt);
    panel::retainDetails(cold,warm);
    assert(cold.chartAt==now && cold.day[95]==42 && cold.month[119]==84);
    cJSON_AddNullToObject(compact,"day");
    assert(!panel::decode(compact,cold,now,nullptr,true));
    cJSON_Delete(compact);
    cJSON_DeleteItemFromObject(root,"details");
    cJSON_DeleteItemFromObject(root,"artRevision");
    using panel::DetailPoll;
    const auto &selected=data.devices[0];
    assert(panel::afterDetailRead(DetailPoll::FirstRead,data,selected)==DetailPoll::Regular);
    data.devices[0].fan=NAN;
    assert(panel::afterDetailRead(DetailPoll::FirstRead,data,selected)==DetailPoll::Followup);
    assert(panel::afterDetailRead(DetailPoll::Regular,data,selected)==DetailPoll::Regular);
    assert(panel::afterDetailRead(DetailPoll::Opened,data,selected)==DetailPoll::Opened);
    data.devices[0].widgets=panel::defaultWidgets;
    assert(panel::afterDetailRead(DetailPoll::FirstRead,data,selected)==DetailPoll::Regular);
    data.devices[0].widgets={1,{Section::Fan}};
    data.detailId[0]=0;
    assert(panel::afterDetailRead(DetailPoll::FirstRead,data,selected)==DetailPoll::Regular);
    assert(panel::decode(root,data,now,"other") && !data.detailId[0] && std::isnan(data.devices[0].temperature));
    assert(panel::decode(root,data,now) && !data.detailId[0] && std::isnan(data.devices[0].fan) && std::isnan(data.devices[0].fanMax));
    auto detail=cJSON_GetObjectItem(root,"detail");
    cJSON_ReplaceItemInObject(detail,"id",cJSON_CreateString("other"));
    assert(panel::decode(root,data,now,"rack-1") && !data.detailId[0]);
    cJSON_ReplaceItemInObject(detail,"id",cJSON_CreateString("rack-1"));
    cJSON_ReplaceItemInObject(detail,"fan",cJSON_CreateNumber(-2));
    cJSON_ReplaceItemInObject(detail,"temperature",cJSON_CreateNull());
    cJSON_ReplaceItemInObject(root,"home",cJSON_Parse("[]"));
    cJSON_ReplaceItemInObject(device,"widgets",cJSON_CreateFalse());
    assert(panel::decode(root,data,now,"rack-1") && !data.home.count && !data.devices[0].widgets.count);
    assert(std::isnan(data.devices[0].temperature) && std::isnan(data.devices[0].fan));
    cJSON_ReplaceItemInObject(device,"widgets",cJSON_Parse(R"(["day","unknown","power","power"])"));
    assert(panel::decode(root,data,now) && data.devices[0].widgets.count==1 && data.devices[0].widgets.contains(Section::Power));
    auto sidebar=cJSON_AddObjectToObject(device,"sidebar");
    cJSON_AddStringToObject(sidebar,"id","rack-1");
    cJSON_AddStringToObject(sidebar,"label",u8"\u2605STAR");
    cJSON_AddStringToObject(sidebar,"image","YWJj");
    assert(panel::decode(root,data,now) && data.devices[0].sidebarImage);
    assert(data.devices[0].sidebarBytes==3 && !memcmp(data.devices[0].sidebarImage.get(),"abc",3) && !strcmp(data.devices[0].sidebarLabel,u8"\u2605STAR"));
    for (const auto &invalid : {std::string("Y!Jj"),std::string("A==A"),std::string(2732,'A')}) {
        cJSON_ReplaceItemInObject(sidebar,"image",cJSON_CreateString(invalid.c_str()));
        assert(panel::decode(root,data,now) && !data.devices[0].sidebarImage && !data.devices[0].sidebarBytes && !*data.devices[0].sidebarLabel);
    }
    cJSON_ReplaceItemInObject(sidebar,"image",cJSON_CreateString((std::string(2731,'A')+"=").c_str()));
    assert(panel::decode(root,data,now) && data.devices[0].sidebarBytes==2048 && data.devices[0].sidebarImage.get()[2047]==0);
    for (auto invalid : {"toolong","\xc0\x80","\xe2\x80","\n"}) {
        cJSON_ReplaceItemInObject(sidebar,"label",cJSON_CreateString(invalid));
        assert(panel::decode(root,data,now) && !data.devices[0].sidebarImage);
    }
    cJSON_ReplaceItemInObject(sidebar,"label",cJSON_CreateString("Rack"));
    cJSON_ReplaceItemInObject(sidebar,"id",cJSON_CreateString("wrong"));
    assert(panel::decode(root,data,now) && !data.devices[0].sidebarImage);
    const std::string longLabel=std::string(94,'x')+u8"\u00e9\u00e9\u00e9";
    cJSON_ReplaceItemInObject(device,"name",cJSON_CreateString(longLabel.c_str()));
    cJSON_ReplaceItemInObject(device,"model",cJSON_CreateString(longLabel.c_str()));
    assert(panel::decode(root,data,now));
    assert(data.devices[0].name==longLabel && data.devices[0].model==longLabel);
    cJSON_ReplaceItemInObject(device,"model",cJSON_CreateString((longLabel+"x").c_str()));
    assert(!panel::decode(root,data,now));
    cJSON_ReplaceItemInObject(device,"model",cJSON_CreateString("File server"));
    // Go escapes "<" as six bytes on the wire, including inside device names and models.
    const auto inventoryStart=report.find("\"devices\":[")+strlen("\"devices\":[");
    std::string fullFleet=report.substr(0,inventoryStart);
    fullFleet.replace(fullFleet.find("\"total\":1"),strlen("\"total\":1"),"\"total\":16");
    std::string escapedLabel;
    for (int i=0;i<100;++i) escapedLabel+="\\u003c";
    const std::string icon=std::string(2731,'A')+"=";
    for (int i=0;i<16;++i) {
        const std::string id=std::string(30,'a')+std::to_string(i);
        if (i) fullFleet+=",";
        fullFleet+="{\"id\":\""+id+"\",\"name\":\""+escapedLabel+"\",\"model\":\""+escapedLabel+
                  "\",\"watts\":1,\"controls\":true,\"widgets\":[\"power\",\"temperature\",\"fan\",\"controls\"],\"sidebar\":{\"id\":\""+id+"\",\"label\":\"server\",\"image\":\""+icon+"\"}}";
    }
    fullFleet+="]}";
    assert(fullFleet.size()>64*1024 && fullFleet.size()<=panel::maxSnapshotBytes);
    assert(panel::shallowJson(fullFleet.data(),fullFleet.size()));
    cJSON *fullSnapshot=cJSON_Parse(fullFleet.c_str());
    assert(fullSnapshot && panel::decode(fullSnapshot,data,now) && data.count==16 && data.devices[15].sidebarImage);
    cJSON_Delete(fullSnapshot);
    cJSON *captured=cJSON_GetObjectItem(root,"capturedAt");
    captured->valuedouble=now+0.5;
    assert(!panel::decode(root,data,now));
    captured->valuedouble=now;
    valid=panel::decode(root,data,now-31);
    assert(!valid);
    auto *month=cJSON_GetObjectItem(root,"month");
    cJSON_AddItemToArray(month,cJSON_CreateNull());
    assert(!panel::decode(root,data,now));
    cJSON_DeleteItemFromArray(month,120);
    cJSON_DeleteItemFromArray(cJSON_GetObjectItem(root,"day"),1);
    valid=panel::decode(root,data,now);
    assert(!valid);
    cJSON_Delete(root);
    assert(panel::validId("rack-1") && !panel::validId("") && !panel::validId("a/b") && !panel::validId("-a"));
    for (const std::string invalid : {R"({"id":"rack\u0000other"})", "{\"name\":\"a\nb\"}"})
        assert(!panel::shallowJson(invalid.data(),invalid.size()));
    const std::string literal=R"({"name":"literal \\u0000"})";
    assert(panel::shallowJson(literal.data(),literal.size()));
    assert(panel::shallowJson(report.data(),report.size()));
    assert(!panel::shallowJson("[[[[[[[]]]]]]]",14) && !panel::shallowJson("{\0}",3));
    static_assert(!panel::elapsed(7,UINT32_MAX-1,10),"deadline before rollover");
    static_assert(panel::elapsed(8,UINT32_MAX-1,10),"deadline after rollover");
}
