// USB-flash this first on a spare ESP32-C5, then use ArduinoOTA over WiFi.
// Edit these three values here. Never commit your real credentials to Git.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* OTA_PASSWORD = "CHOOSE_YOUR_OTA_PASSWORD";

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ETCore.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Preferences.h>
#include "BeaconIdentity.h"

// Mirror diagnostics to native USB and the UART USB adapter.
#if ARDUINO_USB_CDC_ON_BOOT
#define LOGF(...) do { Serial.printf(__VA_ARGS__); Serial0.printf(__VA_ARGS__); } while (0)
#define LOGLN(...) do { Serial.println(__VA_ARGS__); Serial0.println(__VA_ARGS__); } while (0)
#else
#define LOGF(...) Serial.printf(__VA_ARGS__)
#define LOGLN(...) Serial.println(__VA_ARGS__)
#endif

#if !defined(CONFIG_IDF_TARGET_ESP32C5)
#error "Select ESP32C5 Dev Module for this starter sketch."
#endif

const char* OTA_HOSTNAME = "etcore-c5-test";
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t STATUS_MS = 5000;
bool otaReady = false;
ETCore core;
WebServer web(80);
bool coreReady = false;
uint32_t testCounter = 0, lastReading = 0;
const char* TEST_KEY = "YOUR_SEPARATE_64_HEX_TEST_KEY";
void publishTest();
struct Seen {char mac[18],name[33],identity[50];int rssi;float filtered;uint32_t at,hits;};
Seen seen[24]={};portMUX_TYPE seenLock=portMUX_INITIALIZER_UNLOCKED;
BLEScan* scanner=nullptr;uint32_t lastScan=0;
char tracked[50]="";Preferences prefs;RoomPresence presence;
int enterRssi=-65,exitRssi=-75;
class BeaconCallbacks:public BLEAdvertisedDeviceCallbacks {
 void onResult(BLEAdvertisedDevice dev) override {
  String mac=dev.getAddress().toString();mac.toLowerCase();String name=dev.getName();
  char identity[50]="";String manufacturer=dev.getManufacturerData();
  beaconIdentity((const uint8_t*)manufacturer.c_str(),manufacturer.length(),identity,sizeof(identity));
  uint32_t now=millis();int rssi=dev.getRSSI();
  portENTER_CRITICAL(&seenLock);
  int slot=-1,empty=-1,oldestSlot=0;uint32_t oldest=0;
  for(int i=0;i<24;i++){
   if((identity[0]&&strcmp(seen[i].identity,identity)==0)||(!identity[0]&&strcmp(seen[i].mac,mac.c_str())==0)){slot=i;break;}
   if(!seen[i].mac[0]&&empty<0)empty=i;
   uint32_t age=now-seen[i].at;if(age>=oldest){oldest=age;oldestSlot=i;}
  }
  bool existing=slot>=0;if(!existing)slot=empty>=0?empty:oldestSlot;
  bool fresh=existing&&(uint32_t)(now-seen[slot].at)<30000;
  seen[slot].filtered=fresh?0.25f*rssi+0.75f*seen[slot].filtered:rssi;
  seen[slot].hits=fresh?seen[slot].hits+1:1;
  snprintf(seen[slot].mac,18,"%s",mac.c_str());snprintf(seen[slot].name,33,"%s",name.c_str());
  snprintf(seen[slot].identity,50,"%s",identity);seen[slot].rssi=rssi;seen[slot].at=now;
  portEXIT_CRITICAL(&seenLock);
 }
};
BeaconCallbacks beaconCallbacks;
void copySeen(Seen* out){portENTER_CRITICAL(&seenLock);memcpy(out,seen,sizeof(seen));portEXIT_CRITICAL(&seenLock);}
void bleMetrics(JsonDocument& d){Seen snapshot[24];copySeen(snapshot);uint32_t now=millis();int count=0;bool found=false;float filtered=-127;
 for(auto&s:snapshot){if(!s.mac[0])continue;uint32_t age=now-s.at;if(age<30000)count++;
  if(tracked[0]&&strcmp(s.identity,tracked)==0){d["tracked_age_seconds"]=age/1000;if(age<30000&&s.hits>=2){found=true;filtered=s.filtered;d["tracked_rssi"]=s.rssi;d["smoothed_rssi"]=filtered;}}}
 presence.update(now,found,filtered,enterRssi,exitRssi);
 d["nearby_devices"]=count;d["tracking_configured"]=tracked[0]!=0;d["room"]="Office";
 d["presence_status"]=!tracked[0]?"unconfigured":presence.present?"near_office":"not_near_office";
 if(tracked[0])d["office_present"]=presence.present;
 d["enter_rssi"]=enterRssi;d["exit_rssi"]=exitRssi;
}
void nearbyReply(){JsonDocument d;Seen snapshot[24];copySeen(snapshot);auto a=d["devices"].to<JsonArray>();
 for(auto&s:snapshot){if(!s.mac[0])continue;auto o=a.add<JsonObject>();o["mac"]=s.mac;o["name"]=s.name;o["identity"]=s.identity;o["rssi"]=s.rssi;o["age_seconds"]=(uint32_t)(millis()-s.at)/1000;}
 d["tracked_identity"]=tracked;bleMetrics(d);String body;serializeJson(d,body);web.send(200,"application/json",body);}
void trackReply(){String m=web.arg("identity");m.trim();m.toLowerCase();if(!m.isEmpty()&&!validBeaconIdentity(m.c_str())){web.send(400,"text/plain","Use UUID:major:minor from the phone BLE transmitter");return;}
 if(prefs.putString("beacon",m)==0&&!m.isEmpty()){web.send(500,"text/plain","Save failed");return;}snprintf(tracked,50,"%s",m.c_str());presence.reset();web.send(200,"text/plain","Saved");publishTest();}
void thresholdReply(){String e=web.arg("enter"),x=web.arg("exit");int a=e.toInt(),b=x.toInt();
 if(a<-90||a>-30||b<-100||b>=a-2){web.send(400,"text/plain","Enter -90 to -30; exit must be at least 3 dB weaker");return;}
 JsonDocument cfg;cfg["enter"]=a;cfg["exit"]=b;String body;serializeJson(cfg,body);
 if(!prefs.putString("thresholds",body)){web.send(500,"text/plain","Save failed");return;}enterRssi=a;exitRssi=b;presence.reset();web.send(200,"text/plain","Saved");}
void pageReply(){web.send(200,"text/html",R"HTML(<!doctype html><meta name="viewport" content="width=device-width"><title>Office Presence</title><style>body{font:16px system-ui;background:#122031;color:#fff;padding:16px;max-width:850px;margin:auto}input,button{font:inherit;padding:10px;margin:4px;max-width:95%;box-sizing:border-box}input{width:95%}.device{padding:12px;background:#20324a;border-radius:10px;margin:10px 0;overflow-wrap:anywhere}a{color:#87caff}</style><h1>Office Presence</h1><p>Tracks your phone's iBeacon identity, even when its Bluetooth address changes. Enable BLE transmitter in the Home Assistant Android app.</p><h2 id="state">Loading…</h2><p id="details"></p><p>One receiver estimates “near the office”. It cannot prove which side of a wall you are on.</p><form id="manual"><label>Phone beacon UUID:major:minor<input id="identity" placeholder="UUID:major:minor"></label><button>Track phone beacon</button></form><button id="clear">Clear tracking</button><p id="message" role="status"></p><details><summary>Room signal thresholds</summary><p>Starting values only. Check readings inside and outside the office before tuning.</p><form id="thresholds"><label>Enter RSSI<input id="enter" type="number" value="-65"></label><label>Exit RSSI<input id="exit" type="number" value="-75"></label><button>Save thresholds</button></form></details><h2>Nearby devices</h2><div id="list"></div><a href="/status">Gateway status</a><script>
async function save(path,data){let r=await fetch(path,{method:'POST',body:new URLSearchParams(data)});let msg=await r.text();document.querySelector('#message').textContent=msg;if(r.ok)await refresh()}
document.querySelector('#manual').onsubmit=e=>{e.preventDefault();save('/track',{identity:document.querySelector('#identity').value})};document.querySelector('#clear').onclick=()=>save('/track',{identity:''});document.querySelector('#thresholds').onsubmit=e=>{e.preventDefault();save('/thresholds',{enter:document.querySelector('#enter').value,exit:document.querySelector('#exit').value})};
async function refresh(){try{let r=await fetch('/nearby');if(!r.ok)throw Error('Gateway not responding');let d=await r.json();document.querySelector('#state').textContent=d.presence_status==='unconfigured'?'Choose your phone beacon':d.office_present?'Phone near office':'Phone not detected near office';document.querySelector('#details').textContent=(d.tracked_identity||'No beacon selected')+(d.smoothed_rssi!==undefined?' · '+Math.round(d.smoothed_rssi)+' dBm filtered':'');let list=document.querySelector('#list');list.replaceChildren();for(let s of d.devices){let box=document.createElement('div');box.className='device';let label=document.createElement('p');label.textContent=(s.name||'Unnamed')+' · '+s.rssi+' dBm · '+s.age_seconds+'s ago';box.appendChild(label);let id=document.createElement('p');id.textContent=s.identity||'No iBeacon identity · '+s.mac;box.appendChild(id);if(s.identity){let b=document.createElement('button');b.textContent='Track this beacon';b.onclick=()=>save('/track',{identity:s.identity});box.appendChild(b)}list.appendChild(box)}}catch(e){document.querySelector('#state').textContent='Gateway unavailable';document.querySelector('#message').textContent=e.message}}
refresh();setInterval(refresh,5000);</script>)HTML");}

void publishTest() {
  JsonDocument d;
  bleMetrics(d);

  d["wifi_rssi"] = WiFi.RSSI();
  d["channel"] = WiFi.channel();
  d["uptime_seconds"] = millis()/1000;
  core.sendState(d.as<JsonObject>());
}
void statusReply() {
  JsonDocument d;
  d["firmware"] = "c5-office-presence-1.1";
  d["ip"] = WiFi.localIP().toString();
  d["mac"] = WiFi.macAddress();
  d["channel"] = WiFi.channel();
  d["rssi"] = WiFi.RSSI();
  d["flash_bytes"] = ESP.getFlashChipSize();
  d["psram_bytes"] = ESP.getPsramSize();
  d["uptime_seconds"] = millis()/1000;
  bleMetrics(d);
  d["ota"] = otaReady;
  String body; serializeJson(d,body);web.send(200,"application/json",body);
}
bool configured = false;
uint32_t lastWifiAttempt = 0;
uint32_t lastStatus = 0;

void printStatus() {
  if (!configured) {
    LOGLN("[SETUP] Edit WIFI_SSID, WIFI_PASSWORD and OTA_PASSWORD at the top of C5Office.ino, then upload over USB.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOGF("[WAITING] SSID=%s WiFi status=%d NO IP YET\n", WIFI_SSID, (int)WiFi.status());
    return;
  }
  LOGF("[READY] IP=%s MAC=%s RSSI=%d channel=%d OTA=%s.local:3232 (%s) uptime=%lu s\n",
                WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str(),
                (int)WiFi.RSSI(), (int)WiFi.channel(), OTA_HOSTNAME, otaReady ? "password protected" : "starting",
                (unsigned long)(millis() / 1000));
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial0.begin(115200);
#endif
  delay(1000);  // bounded USB enumeration grace period; never wait for a monitor
  LOGLN("\nET-Core Office BLE presence 1.1");
  LOGF("[BOARD] ESP32-C5 flash=%lu bytes PSRAM=%lu bytes\n", (unsigned long)ESP.getFlashChipSize(), (unsigned long)ESP.getPsramSize());
  configured = WIFI_SSID[0] && WIFI_PASSWORD[0] && OTA_PASSWORD[0] &&
               strcmp(WIFI_SSID, "YOUR_WIFI_SSID") != 0 &&
               strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0 &&
               strcmp(OTA_PASSWORD, "CHOOSE_YOUR_OTA_PASSWORD") != 0;
  if (!configured) {
    printStatus();
    return;
  }

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { if(scanner)scanner->stop(); LOGLN("[OTA] Update started; keep power connected"); });
  ArduinoOTA.onEnd([]() { LOGLN("\n[OTA] Update complete; rebooting"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 101;
    const unsigned int percent = total ? (unsigned int)((uint64_t)progress * 100 / total) : 0;
    if (percent != lastPercent) {
      lastPercent = percent;
      LOGF("[OTA] %u%%\n", percent);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    const char* reason = "unknown";
    switch (error) {
      case OTA_AUTH_ERROR: reason = "authentication failed"; break;
      case OTA_BEGIN_ERROR: reason = "cannot start update; check OTA partitions and image size"; break;
      case OTA_CONNECT_ERROR: reason = "connection failed"; break;
      case OTA_RECEIVE_ERROR: reason = "receive failed"; break;
      case OTA_END_ERROR: reason = "final verification failed"; break;
    }
    LOGF("[OTA] Error %u: %s\n", (unsigned int)error, reason);
  });

  prefs.begin("c5ble",false);
  String saved=prefs.getString("beacon","");if(validBeaconIdentity(saved.c_str()))snprintf(tracked,sizeof(tracked),"%s",saved.c_str());
  JsonDocument cfg;if(!deserializeJson(cfg,prefs.getString("thresholds","{}"))){int a=cfg["enter"]|-65,b=cfg["exit"]|-75;if(a>=-90&&a<=-30&&b>=-100&&b<a-2){enterRssi=a;exitRssi=b;}}
  BLEDevice::init("");scanner=BLEDevice::getScan();
  scanner->setAdvertisedDeviceCallbacks(&beaconCallbacks,true);
  scanner->setActiveScan(false);scanner->setInterval(100);scanner->setWindow(30);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // this is an always-awake OTA starter, not a battery test
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  printStatus();
}

void loop() {
  const uint32_t now = millis();
  if (configured) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!otaReady) {
        ArduinoOTA.begin();
        otaReady = true;
        printStatus();
      }
      ArduinoOTA.handle();
      if (!coreReady) {
        core.setPort(15561);
        core.onSync(publishTest);
        if (core.enableEncryptionHex(TEST_KEY)) {
          core.begin("c5-ble-presence", "sensor.ble_presence", "Office BLE Presence", "c5-office-presence-1.1");
          coreReady = true;
          web.on("/status", HTTP_GET, statusReply);
          web.on("/", HTTP_GET, pageReply);
          web.on("/nearby",HTTP_GET,nearbyReply);
          web.on("/track",HTTP_POST,trackReply);
          web.on("/thresholds",HTTP_POST,thresholdReply);
          web.begin();
          publishTest();
        }
      }
      if (coreReady) {
        core.loop();web.handleClient();
        if (now-lastReading >= 5000) {lastReading=now;++testCounter;publishTest();}
      }
    } else {
      if (otaReady) {
        ArduinoOTA.end();
        otaReady = false;
        LOGLN("[WiFi] Connection lost; reconnecting");
      }
      if (now - lastWifiAttempt >= WIFI_RETRY_MS) {
        lastWifiAttempt = now;
        WiFi.reconnect();
      }
    }
  }
  if (scanner && !scanner->isScanning() && now-lastScan>=5000) {
    lastScan=now;scanner->clearResults();scanner->start(3,[](BLEScanResults){},false);
  }
  if (now - lastStatus >= STATUS_MS) {
    lastStatus = now;
    printStatus();
  }
  delay(2);
}
