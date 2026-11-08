#include <Arduino.h>
#include <sys/socket.h>
#include <errno.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"
#include "Rf24Protocol.h"
#include "Rf24Channels.h"
#include <RF24.h>
#include <RF24Network.h>
#ifdef RF24_LEGACY_MQTT
#include <PubSubClient.h>
#else
#include <ETCore.h>
#endif
// Existing SPI wiring. Native USB CDC is disabled: GPIO20 is the OLED reset.
constexpr int RF_CE=4,RF_CSN=5,OLED_DC=21,OLED_CS=47,OLED_RST=20;
constexpr uint32_t SAMPLE_TTL=1800000;
RF24 radio(RF_CE,RF_CSN);
RF24Network network(radio);
Adafruit_SSD1306 display(128,64,&SPI,OLED_DC,OLED_RST,OLED_CS);
WiFiServer telnetServer(23);
WiFiClient telnetClient;
struct Reading {double value=0;uint32_t at=0;bool used=false,dirty=false;};
Reading readings[34];
bool radioReady=false,displayReady=false,otaActive=false,session=false,encrypted=false;
uint32_t validPackets=0,badPackets=0,lastRadioAttempt=0,lastWifiAttempt=0,lastDisplayAttempt=0;
uint32_t lastDisplay=0,lastSend=0,lastSnapshot=0,lastHealth=0;
unsigned displayStart=0,sendCursor=0;
static constexpr char HOSTNAME[]="NRF-Network";
#ifdef RF24_LEGACY_MQTT
static constexpr char VERSION[]="rf24-mqtt-recovery-2026.11.08.1";
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
uint32_t lastMqttAttempt=0;
#else
static constexpr char VERSION[]="rf24-etcore-2026.11.08.1";
ETCore core;
#endif
void logLine(const char* text){
 Serial.println(text);
 if(telnetClient && telnetClient.connected()){
  char line[192];int length=snprintf(line,sizeof(line),"%s\r\n",text);if(length<0 || length>=int(sizeof(line)))return;
  int sent=send(telnetClient.fd(),line,length,MSG_DONTWAIT);
  if(sent>=0 && sent!=length)telnetClient.stop(); // Never block RF24/OTA on a slow debug viewer.
 }
}
void dirtyAll(){for(auto& r:readings)if(r.used)r.dirty=true;}
void remember(unsigned key,double value){readings[key]={value,millis(),true,true};}
bool setSwitch(bool state){
 if(!radioReady || otaActive)return false;
 Rf24Protocol::SwitchData message={uint8_t(state)};
 RF24NetworkHeader header(Rf24Protocol::SWITCH_ADDRESS);
 bool ok=network.write(header,&message,sizeof(message));
 if(ok){remember(33,state?1:0);logLine("[RF24] Switch command transmitted; physical actuation unconfirmed");}
 else logLine("[RF24] Switch transmission failed; state not changed");
 return ok;
}
#ifndef RF24_LEGACY_MQTT
void hello(JsonObject p){p["profile"]="rf24_network_v1";p["profile_hash"]=PROFILE_HASH;p["model"]="ESP32-S3 RF24Network Gateway";}
void onCommand(const char*,JsonObject p){
 const char* command=p["command"]|"";
 bool ok=!strcmp(command,"rf24_switch_set") && p["on"].is<bool>() && setSwitch(p["on"].as<bool>());
 core.sendAck(p["cmd_id"]|"",ok);
}
#else
void onMqtt(char* topic,byte* payload,unsigned length){
 if(strcmp(topic,"home/node1/switch/set") || length!=1 || (payload[0]!='1' && payload[0]!='0'))return;
 setSwitch(payload[0]=='1');
}
#endif
void tryRadio(){
 lastRadioAttempt=millis();radioReady=radio.begin(&SPI);
 if(!radioReady){logLine("[RF24] Radio not detected; networking/OTA continue; retry in 30s");return;}
 network.begin(Rf24Protocol::CHANNEL,00);radio.setPALevel(RF24_PA_MAX);
 if(!radio.setDataRate(RF24_1MBPS)){radioReady=false;logLine("[RF24] Data rate configuration failed");return;}
 logLine("[RF24] Ready: channel 90, 1 Mbps, master 00; no boot switch command");
}
void tryDisplay(){
 lastDisplayAttempt=millis();displayReady=display.begin(SSD1306_SWITCHCAPVCC,0x3C,true,false);
 if(displayReady){display.clearDisplay();display.setTextColor(SSD1306_WHITE);display.setTextSize(1);display.display();}
 else logLine("[OLED] Allocation failed; RF24/OTA continue");
}
void setup(){
 Serial.begin(115200);Serial.printf("\n[BUILD] %s\n",VERSION);
 pinMode(RF_CSN,OUTPUT);digitalWrite(RF_CSN,HIGH);pinMode(OLED_CS,OUTPUT);digitalWrite(OLED_CS,HIGH);
 SPI.begin(12,13,11);tryRadio();tryDisplay();
 WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setHostname(HOSTNAME);WiFi.setAutoReconnect(true);
 WiFi.begin(ssid,password);lastWifiAttempt=millis();
 ArduinoOTA.setHostname(HOSTNAME);ArduinoOTA.setPassword(ota_password);
 ArduinoOTA.onStart([](){otaActive=true;
#ifndef RF24_LEGACY_MQTT
  core.sendBye();
#endif
  logLine("[OTA] Update started; radio handling paused");});
 ArduinoOTA.onEnd([](){logLine("[OTA] Complete; rebooting");});
 ArduinoOTA.onProgress([](unsigned progress,unsigned total){static unsigned last=101;unsigned pct=total?unsigned(uint64_t(progress)*100/total):0;if(pct!=last && pct%10==0){last=pct;Serial.printf("[OTA] %u%%\n",pct);}});
 ArduinoOTA.onError([](ota_error_t error){otaActive=false;Serial.printf("[OTA] Error %u; current application retained\n",unsigned(error));});
#ifdef RF24_LEGACY_MQTT
 wifiClient.setConnectionTimeout(2000);mqtt.setServer(mqttServer,1883);mqtt.setCallback(onMqtt);mqtt.setSocketTimeout(2);mqtt.setBufferSize(512);
#else
 encrypted=core.enableEncryptionHex(et_core_key);core.setPort(5560);core.setLeaseInterval(2000);core.setLeaseTtl(10);
 core.onHello(hello);core.onCommand(onCommand);core.onSync(dirtyAll);
 if(!encrypted)logLine("[ET-Core] Invalid key; transport disabled; OTA remains available");
#endif
}
void serviceWifi(uint32_t now){
 static bool otaStarted=false;
 if(WiFi.status()!=WL_CONNECTED){session=false;if(uint32_t(now-lastWifiAttempt)>=15000){lastWifiAttempt=now;WiFi.reconnect();}return;}
 if(!session){
  if(!otaStarted){ArduinoOTA.begin();telnetServer.begin();telnetServer.setNoDelay(true);otaStarted=true;}
#ifndef RF24_LEGACY_MQTT
  if(encrypted)core.begin("rf24-network-master","bridge.rf24","RF24 Network Master",VERSION);
#endif
  session=true;dirtyAll();Serial.printf("[READY] IP=%s RSSI=%d OTA=%s.local:3232\n",WiFi.localIP().toString().c_str(),WiFi.RSSI(),HOSTNAME);
 }
 ArduinoOTA.handle();
}
void serviceTelnet(){
 if(telnetServer.hasClient()){
  WiFiClient c=telnetServer.accept();
  if(!telnetClient || !telnetClient.connected()){telnetClient.stop();telnetClient=c;telnetClient.setNoDelay(true);logLine("NRF Network Telnet Debug");char status[160];snprintf(status,sizeof(status),"Version: %s IP: %s; press s for status",VERSION,WiFi.localIP().toString().c_str());logLine(status);}
  else c.stop();
 }
 for(unsigned n=0;n<32 && telnetClient && telnetClient.available();++n){char c=telnetClient.read();if(c=='s'||c=='S'){
  char status[160];snprintf(status,sizeof(status),"WiFi: %s RSSI: %d RF24: %s packets=%lu rejected=%lu OLED: %s",WiFi.status()==WL_CONNECTED?"CONNECTED":"DISCONNECTED",WiFi.RSSI(),radioReady?"READY":"FAILED",(unsigned long)validPackets,(unsigned long)badPackets,displayReady?"READY":"FAILED");logLine(status);
#ifdef RF24_LEGACY_MQTT
  logLine(mqtt.connected()?"MQTT: CONNECTED":"MQTT: DISCONNECTED");
#else
  logLine(encrypted?"ET-Core: ENCRYPTED; UDP 5560":"ET-Core: DISABLED");
#endif
 }}
}
void receiveRadio(){
 if(!radioReady)return;network.update();
 for(unsigned n=0;n<8 && network.available();++n){
  RF24NetworkHeader header;uint16_t size=network.peek(header);uint8_t bytes[32]={};
  uint16_t got=network.read(header,bytes,sizeof(bytes)); // Always drain, including unknown/bad packets.
  Rf24Protocol::Sample packet={};
  if(size>sizeof(bytes) || got!=size || !Rf24Protocol::decode(header.from_node,bytes,size,packet)){++badPackets;continue;}
  ++validPackets;for(unsigned i=0;i<packet.count;++i){remember(packet.first+i,packet.values[i]);char line[96];snprintf(line,sizeof(line),"[RF24] %s = %.2f",LABELS[packet.first+i],packet.values[i]);logLine(line);}
  Serial.printf("[RF24] node 0%o: %u readings\n",header.from_node,unsigned(packet.count));
 }
}
void serviceReports(uint32_t now){
 for(auto& r:readings)if(r.used && uint32_t(now-r.at)>SAMPLE_TTL){r.used=false;r.dirty=false;}
 if(uint32_t(now-lastSnapshot)>=30000){dirtyAll();lastSnapshot=now;}
 if(WiFi.status()!=WL_CONNECTED || otaActive)return;
#ifdef RF24_LEGACY_MQTT
 if(!mqtt.connected()){
  if(uint32_t(now-lastMqttAttempt)>=5000){lastMqttAttempt=now;if(mqtt.connect("RF24Network-Recovery",mqttUser,mqttPassword)){mqtt.subscribe("home/node1/switch/set");dirtyAll();}}
  return;
 }
 mqtt.loop();
#else
 if(!encrypted)return;core.loop();
 if(uint32_t(now-lastHealth)>=20000){JsonDocument d;d["rf24_health"]["radio_ready"]=radioReady;d["rf24_health"]["oled_ready"]=displayReady;d["rf24_health"]["packets"]=validPackets;d["rf24_health"]["rejected"]=badPackets;d["rf24_health"]["rssi"]=WiFi.RSSI();core.sendState(d.as<JsonObject>());lastHealth=now;}
#endif
 now=millis(); // Commands handled above may have just produced a newer sample.
 if(uint32_t(now-lastSend)<30)return;
#ifndef RF24_LEGACY_MQTT
 JsonDocument d;JsonObject values=d["values"].to<JsonObject>();JsonObject ages=d["age_ms"].to<JsonObject>();
#endif
 unsigned count=0;
 for(unsigned scan=0;scan<34 && count<4;++scan){unsigned i=sendCursor++%34;auto& r=readings[i];if(!r.used||!r.dirty)continue;
#ifdef RF24_LEGACY_MQTT
  String value=i==33?String(r.value?"ON":"OFF"):String(r.value,2);if(!mqtt.publish(LEGACY_TOPICS[i],value.c_str(),i==33))continue;
#else
  if(i==33)values[KEYS[i]]=bool(r.value);else values[KEYS[i]]=r.value;ages[KEYS[i]]=uint32_t(now-r.at);
#endif
  r.dirty=false;++count;
 }
#ifndef RF24_LEGACY_MQTT
 if(count)core.sendState(d.as<JsonObject>());
#endif
 lastSend=now;
}
void serviceDisplay(uint32_t now){
 if(!displayReady || uint32_t(now-lastDisplay)<1000)return;lastDisplay=now;
 display.clearDisplay();display.setTextSize(1);display.setTextColor(SSD1306_WHITE);display.setCursor(0,0);
#ifdef RF24_LEGACY_MQTT
 display.println("RF24 S3 MQTT Recovery");
#else
 display.println("RF24 S3 ET-Core");
#endif
 for(unsigned n=0;n<7;++n){unsigned i=(displayStart+n)%34;char line[24];
  if(!readings[i].used)snprintf(line,sizeof(line),"%-11.11s       --",LABELS[i]);
  else if(i==33)snprintf(line,sizeof(line),"%-11.11s %8s",LABELS[i],readings[i].value?"ON":"OFF");
  else snprintf(line,sizeof(line),"%-11.11s %8.1f",LABELS[i],readings[i].value);
  display.println(line);
 }
 display.display();displayStart=(displayStart+1)%34;
}
void loop(){
 uint32_t now=millis();serviceWifi(now);if(otaActive){delay(1);return;}
 serviceTelnet();if(!radioReady && uint32_t(now-lastRadioAttempt)>=30000)tryRadio();
 if(!displayReady && uint32_t(now-lastDisplayAttempt)>=30000)tryDisplay();
 receiveRadio();serviceReports(millis());serviceDisplay(millis());delay(1);
}
