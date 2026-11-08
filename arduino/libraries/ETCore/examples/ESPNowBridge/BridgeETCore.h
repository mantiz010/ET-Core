#pragma once
#include <ETCore.h>
#include <math.h>
#include <errno.h>
#include <string.h>
namespace BridgeETCore {
static ETCore core;
static bool started=false, encrypted=false, otaBusy=false;
static const char* PROFILE="espnow_uart_v1";
static const char* PROFILE_HASH="3f1744b4c5fa9629";
static const char* KEYS[]={
    "bedroom_temperature",
    "bedroom_humidity",
    "bedroom_battery",
    "lounge_temperature",
    "lounge_humidity",
    "lounge_battery",
    "bedroom1_temperature",
    "bedroom1_humidity",
    "bedroom1_battery",
    "solar10a_panelv",
    "solar10a_paneli",
    "solar10a_batteryv",
    "solar10a_batteryi",
    "solar10a_loadv",
    "solar10a_loadi",
    "solar20a_panelv",
    "solar20a_paneli",
    "solar20a_batteryv",
    "solar20a_batteryi",
    "solar20a_loadv",
    "solar20a_loadi",
    "espnow_node6_dustdensity",
    "espnow_node6_voltage",
    "temperature_degreecelsius",
    "humidity_percentrelative",
    "barometer_hectopascal",
    "gas_gas",
    "voltage_volts",
    "garden_battery",
    "garden_solar",
    "garden_amps",
    "garden_lux",
    "garden_moisture1",
    "garden_moisture2",
    "garden_moisture3",
    "garden_moisture4",
    "garden_watercheck",
    "espph_ph",
    "espcf_cf",
    "pir_pirbatt_state1",
    "pirbatt_voltage",
    "espnow_lora1_battery",
    "espnow_lora1_water",
    "loranode2_temp",
    "loranode2_humd",
    "loranode2_battery",
    "garden_relaystate",
    "pump_state",
    "rgb2",
    "rgb3",
    "rgb1"
};
struct Entry { char value[192]; uint32_t seen; bool used,dirty; };
static Entry cache[sizeof(KEYS)/sizeof(KEYS[0])] = {};
static size_t cursor=0;
static uint32_t lastSend=0, lastSync=0;
inline void markDirty() { for (auto& e:cache) if(e.used)e.dirty=true; }
inline void hello(JsonObject p) { p["profile"]=PROFILE; p["profile_hash"]=PROFILE_HASH; p["model"]="ESP32 ESP-NOW UART/E220 Gateway"; }
inline void configure(ETCore::CommandHandler handler) {
    encrypted=core.enableEncryptionHex(et_core_key);
    core.setPort(5560); core.setLeaseInterval(2000); core.setLeaseTtl(10);
    core.onCommand(handler); core.onSync(markDirty); core.onHello(hello);
    if(!encrypted) Serial.println("[ET-Core] Invalid encryption key; transport disabled; UART/OTA remain available");
}
inline void startSession() {
    if(!encrypted)return;
    // begin closes/reopens the socket and creates a fresh authenticated boot/session.
    // Commands from the previous Wi-Fi session cannot act after reconnection.
    core.begin("espnow-wifi-master","bridge.espnow","ESP-NOW Wi-Fi Master","2026-11-08.1");
    started=true; markDirty(); lastSync=millis();
    Serial.println("[ET-Core] Encrypted session ready; UDP 5560");
}
inline bool enqueueState(const char* key,const char* value) {
    if(!key || !value || strlen(value)>=sizeof(Entry::value))return false;
    for(size_t i=0;i<sizeof(KEYS)/sizeof(KEYS[0]);++i)if(!strcmp(key,KEYS[i])) {
        strcpy(cache[i].value,value);cache[i].seen=millis();cache[i].used=cache[i].dirty=true;return true;
    }
    return false;
}
inline void service() {
    if(!started || otaBusy || WiFi.status()!=WL_CONNECTED)return;
    core.loop();
    const uint32_t now=millis();
    if(uint32_t(now-lastSync)>=30000U){markDirty();lastSync=now;}
    if(uint32_t(now-lastSend)<20U)return;
    JsonDocument doc;JsonObject values=doc["values"].to<JsonObject>();JsonObject ages=doc["age_ms"].to<JsonObject>();
    size_t chosen[4];size_t count=0;
    const size_t n=sizeof(KEYS)/sizeof(KEYS[0]);
    for(size_t scan=0;scan<n && count<4;++scan) {
        const size_t i=cursor++ % n;auto& e=cache[i];if(!e.used || !e.dirty)continue;
        const char* key=KEYS[i];
        if(!strncmp(key,"rgb",3)) {
            JsonDocument light;if(deserializeJson(light,e.value)){e.dirty=false;continue;}
            values[key].set(light.as<JsonObject>());
        }else if(!strcmp(key,"garden_relaystate") || !strcmp(key,"pump_state")) {
            if(!strcmp(e.value,"ON") || !strcmp(e.value,"1"))values[key]=true;
            else if(!strcmp(e.value,"OFF") || !strcmp(e.value,"0"))values[key]=false;
            else {e.dirty=false;continue;}
        }else {
            char* end=nullptr;errno=0;double x=strtod(e.value,&end);
            if(errno || end==e.value || *end || !isfinite(x)){e.dirty=false;continue;}
            values[key]=x;
        }
        ages[key]=uint32_t(now-e.seen);chosen[count++]=i;
    }
    if(!count)return;
    if(measureJson(doc)>900){Serial.println("[ET-Core] State packet exceeds bound");return;}
    core.sendState(doc.as<JsonObject>());for(size_t j=0;j<count;++j)cache[chosen[j]].dirty=false;lastSend=now;
}
}
inline bool publishState(const char* key,const char* value,bool) { return BridgeETCore::enqueueState(key,value); }
