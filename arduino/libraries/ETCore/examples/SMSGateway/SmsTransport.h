#pragma once
#include <ETCore.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <atomic>
#include "gateway_secrets.h"
#include "SmsValidation.h"
namespace SmsTransport {
struct Request { char id[37], number[32], message[161]; uint8_t recipient; };
struct Event { char kind[12], request[37], status[24], sender[32], message[641]; };
static QueueHandle_t requests=nullptr, events=nullptr;
static ETCore core;
static Preferences prefs;
static std::atomic<bool> modemReady{false};
static bool storageOK=false;
static uint32_t eventCounter=0, journalCursor=0;
static String journal[32];
struct Pending { String id, json; };
static Pending outbox[16];
static bool syncNeeded=true;
static const char* fwVersion="";
inline bool init() {
  requests=xQueueCreate(4,sizeof(Request));events=xQueueCreate(16,sizeof(Event));
  return requests && events;
}
inline void emit(const Event& e) {
  if (xQueueSend(events,&e,pdMS_TO_TICKS(50))!=pdTRUE)
    Serial.println("[SMS] Event queue full; event could not be stored");
}
inline void result(const char* id,const char* status) {
  Event e={};strlcpy(e.kind,"result",sizeof(e.kind));strlcpy(e.request,id,sizeof(e.request));
  strlcpy(e.status,status,sizeof(e.status));emit(e);
}
inline void incoming(const char* sender,const char* message) {
  if(strlen(sender)>31 || strlen(message)>640) { Serial.println("[SMS] Incoming event exceeds bound");return; }
  Event e={};strlcpy(e.kind,"incoming",sizeof(e.kind));strlcpy(e.sender,sender,sizeof(e.sender));
  strlcpy(e.message,message,sizeof(e.message));emit(e);
}
inline void hello(JsonObject p) {p["profile"]="sms_gateway_v1";p["model"]="ESP32 SIM7600SA-H SMS Gateway";}
inline void sync(){syncNeeded=true;}
inline void command(const char*,JsonObject p) {
  const char* cmd=p["command"]|"";const char* cid=p["cmd_id"]|"";
  if(!strcmp(cmd,"ack_event")) {
    const char* id=p["event_id"]|"";
    for(unsigned i=0;i<16;++i)if(outbox[i].id==id && *id) {
      char k[8];snprintf(k,sizeof(k),"e%u",i);
      if(prefs.remove(k)){outbox[i]={};}else {core.sendAck(cid,false);return;}
    }
    return;
  }
  const char* id=p["request_id"]|"";const char* body=p["message"]|"";const char* number=p["number"]|"";
  int recipient=p["recipient"]|1;
  if(strcmp(cmd,"send_sms") || !validId(id) || !validBody(body) || !validNumber(number) ||
     !p["message"].is<const char*>() || p["message"].as<JsonString>().size()!=strlen(body) ||
     (!p["number"].isNull() && (!p["number"].is<const char*>() || p["number"].as<JsonString>().size()!=strlen(number))) ||
     (recipient!=1 && recipient!=2) || !storageOK) {core.sendAck(cid,false);return;}
  for(auto& old:journal)if(old==id){return;} // Persistent reservation; never resubmit after reboot.
  unsigned pending=0;for(auto& e:outbox)if(e.id.length())++pending;
  if(!modemReady || !reservationSpace(pending,uxQueueMessagesWaiting(events),uxQueueSpacesAvailable(requests)) || uxQueueSpacesAvailable(requests)==0) {core.sendAck(cid,false);return;}
  char k[8];unsigned slot=journalCursor%32;snprintf(k,sizeof(k),"r%u",slot);
  if(!prefs.putString(k,id)){core.sendAck(cid,false);return;}
  journal[slot]=id;++journalCursor;
  if(prefs.putUInt("cursor",journalCursor)!=sizeof(uint32_t)){storageOK=false;core.sendAck(cid,false);return;}
  Request r={};strlcpy(r.id,id,sizeof(r.id));strlcpy(r.message,body,sizeof(r.message));strlcpy(r.number,number,sizeof(r.number));r.recipient=recipient;
  if(xQueueSend(requests,&r,0)!=pdTRUE){core.sendAck(cid,false);return;}
  result(id,"queued"); // Transport acceptance, not carrier or handset delivery.
}
inline void saveEvent(const Event& e) {
  int slot=-1;for(unsigned i=0;i<16;++i)if(!outbox[i].id.length()){slot=i;break;}
  if(slot<0){Serial.println("[SMS] Persistent outbox full; event not stored");return;}
  uint32_t next=eventCounter+1;
  if(!next || prefs.putUInt("event_seq",next)!=sizeof(uint32_t)){storageOK=false;return;}
  eventCounter=next;
  char mac[20];snprintf(mac,sizeof(mac),"%012llx",(unsigned long long)ESP.getEfuseMac());
  String id=String(mac)+"-"+String(eventCounter);
  JsonDocument d;d["event_id"]=id;d["kind"]=e.kind;
  if(!strcmp(e.kind,"incoming")){d["sender"]=e.sender;d["message"]=e.message;}
  else {d["request_id"]=e.request;d["status"]=e.status;}
  String json;serializeJson(d,json);char k[8];snprintf(k,sizeof(k),"e%d",slot);
  if(prefs.putString(k,json)){outbox[slot]={id,json};}else storageOK=false;
}
inline void run(SemaphoreHandle_t gate,std::atomic<bool>& otaActive,const char* hostname,const char* otaPassword,const char* version) {
  fwVersion=version;storageOK=prefs.begin("sms-etcore",false);
  if(storageOK) {
    eventCounter=prefs.getUInt("event_seq",0);journalCursor=prefs.getUInt("cursor",0);
    for(unsigned i=0;i<32;++i){char k[8];snprintf(k,sizeof(k),"r%u",i);journal[i]=prefs.getString(k,"");}
    for(unsigned i=0;i<16;++i){char k[8];snprintf(k,sizeof(k),"e%u",i);String j=prefs.getString(k,"");
      JsonDocument d;if(j.length() && !deserializeJson(d,j)){outbox[i]={String(d["event_id"]|""),j};}}
  }
  bool encrypted=core.enableEncryptionHex(et_core_key);
  core.setPort(5560);core.setLeaseInterval(2000);core.setLeaseTtl(10);
  core.onHello(hello);core.onSync(sync);core.onCommand(command);
  bool connected=false,otaStarted=false;uint32_t lastHealth=0,lastEvent=0;unsigned cursor=0;
  for(;;) {
    bool wifi=WiFi.status()==WL_CONNECTED;
    if(wifi && !connected) {
      if(encrypted)core.begin("sim7600-sms-gateway","bridge.sms","SIM7600 SMS Gateway",version);
      if(!otaStarted) {
        ArduinoOTA.setHostname(hostname);ArduinoOTA.setPassword(otaPassword);
        ArduinoOTA.onStart([&otaActive](){otaActive=true;core.sendBye();Serial.println("[OTA] Updating");});
        ArduinoOTA.onError([&otaActive](ota_error_t e){otaActive=false;Serial.printf("[OTA] Error %u\n",unsigned(e));});
        ArduinoOTA.begin();otaStarted=true;
      }
      Serial.printf("[READY] IP=%s OTA=%s.local:3232 ET-Core=%s\n",WiFi.localIP().toString().c_str(),hostname,encrypted?"encrypted":"DISABLED invalid key");
      syncNeeded=true;
    }
    connected=wifi;
    if(wifi && xSemaphoreTake(gate,0)==pdTRUE){ArduinoOTA.handle();xSemaphoreGive(gate);}
    if(!otaActive) {
      if(wifi && encrypted)core.loop();
      Event e;if(storageOK && xQueueReceive(events,&e,0)==pdTRUE)saveEvent(e);
      uint32_t now=millis();
      if(wifi && encrypted && (syncNeeded || uint32_t(now-lastHealth)>=20000)) {
        JsonDocument d;d["sms_health"]["modem_ready"]=modemReady.load();d["sms_health"]["storage_ok"]=storageOK;
        d["sms_health"]["rssi"]=WiFi.RSSI();d["sms_health"]["uptime_s"]=now/1000;
        core.sendState(d.as<JsonObject>());lastHealth=now;syncNeeded=false;
      }
      if(wifi && encrypted && uint32_t(now-lastEvent)>=500) {
        for(unsigned scan=0;scan<16;++scan){auto& e=outbox[cursor++%16];if(!e.id.length())continue;
          JsonDocument body,d;if(!deserializeJson(body,e.json)){d["sms_event"].set(body.as<JsonObject>());core.sendState(d.as<JsonObject>());}break;}
        lastEvent=now;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}
