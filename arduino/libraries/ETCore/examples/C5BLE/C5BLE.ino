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
struct Seen { char mac[18]; char name[33]; int rssi; uint32_t at; };
Seen seen[24] = {};
portMUX_TYPE seenLock=portMUX_INITIALIZER_UNLOCKED;
BLEScan* scanner=nullptr;
uint32_t lastScan=0;
char tracked[18]="";
Preferences prefs;
class BeaconCallbacks: public BLEAdvertisedDeviceCallbacks {
 void onResult(BLEAdvertisedDevice dev) override {
  String mac=dev.getAddress().toString();mac.toLowerCase();
  String name=dev.getName();uint32_t now=millis();
  portENTER_CRITICAL(&seenLock);
  int slot=0;uint32_t oldest=0;
  for(int i=0;i<24;i++) {
   if(strcmp(seen[i].mac,mac.c_str())==0 || seen[i].mac[0]==0){slot=i;break;}
   uint32_t age=now-seen[i].at;if(age>=oldest){oldest=age;slot=i;}
  }
  snprintf(seen[slot].mac,18,"%s",mac.c_str());
  snprintf(seen[slot].name,33,"%s",name.c_str());
  seen[slot].rssi=dev.getRSSI();seen[slot].at=now;
  portEXIT_CRITICAL(&seenLock);
 }
};
BeaconCallbacks beaconCallbacks;
void copySeen(Seen* out) {portENTER_CRITICAL(&seenLock);memcpy(out,seen,sizeof(seen));portEXIT_CRITICAL(&seenLock);}
bool validMac(String m) {
 if(m.length()!=17)return false;
 for(int i=0;i<17;i++){if(i%3==2){if(m[i]!=':')return false;}else if(!isxdigit((unsigned char)m[i]))return false;}
 return true;
}
void bleMetrics(JsonDocument& d) {
 Seen snapshot[24];copySeen(snapshot);uint32_t now=millis();int count=0;bool found=false;
 for(auto &s:snapshot){if(!s.mac[0])continue;uint32_t age=now-s.at;if(age<30000)count++;
  if(tracked[0] && strcmp(s.mac,tracked)==0){found=true;d["tracked_age_seconds"]=age/1000;d["tracked_present"]=age<30000;if(age<30000)d["tracked_rssi"]=s.rssi;}}
 d["nearby_devices"]=count;d["tracking_configured"]=tracked[0]!=0;
 if(tracked[0] && !found)d["tracked_present"]=false;
}
void nearbyReply(){JsonDocument d;Seen snapshot[24];copySeen(snapshot);JsonArray a=d["devices"].to<JsonArray>();
 for(auto&s:snapshot){if(!s.mac[0])continue;auto o=a.add<JsonObject>();o["mac"]=s.mac;o["name"]=s.name;o["rssi"]=s.rssi;o["age_seconds"]=(uint32_t)(millis()-s.at)/1000;}
 d["tracked_mac"]=tracked;String body;serializeJson(d,body);web.send(200,"application/json",body);}
void trackReply(){String m=web.arg("mac");m.toLowerCase();if(!m.isEmpty()&&!validMac(m)){web.send(400,"text/plain","Invalid MAC");return;}
 if(prefs.putString("tracked",m)==0 && !m.isEmpty()){web.send(500,"text/plain","Save failed");return;}
 snprintf(tracked,sizeof(tracked),"%s",m.c_str());web.send(200,"text/plain","Saved");publishTest();}
void pageReply(){web.send(200,"text/html",R"HTML(<!doctype html><meta name="viewport" content="width=device-width"><title>C5 BLE Presence</title><style>body{font:16px system-ui;background:#122031;color:#fff;padding:20px}button,input{padding:12px;margin:4px}td{padding:8px;word-break:break-word}table{width:100%}</style><h1>C5 BLE Presence</h1><p>Choose a beacon to track. Phones may change Bluetooth addresses. Presence means seen in the last 30 seconds; RSSI is not distance.</p><p id="tracked"></p><button onclick="track('')">Clear tracking</button><table id="list"></table><p><a href="/status">Gateway status</a></p><script>async function track(mac){await fetch('/track',{method:'POST',body:new URLSearchParams({mac})});refresh()}async function refresh(){let d=await(await fetch('/nearby')).json();document.querySelector('#tracked').textContent='Tracking: '+(d.tracked_mac||'nothing selected');let table=document.querySelector('#list');table.replaceChildren();for(let s of d.devices){let row=table.insertRow();for(let v of [s.name||'Unnamed',s.mac,s.rssi+' dBm',s.age_seconds+'s ago'])row.insertCell().textContent=v;let b=document.createElement('button');b.textContent='Track';b.onclick=()=>track(s.mac);row.insertCell().appendChild(b)}}refresh();setInterval(refresh,5000)</script>)HTML");}

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
  d["firmware"] = "c5-ble-presence-1.0";
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
    LOGLN("[SETUP] Edit WIFI_SSID, WIFI_PASSWORD and OTA_PASSWORD at the top of C5BLE.ino, then upload over USB.");
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
  LOGLN("\nET-Core C5 BLE presence gateway 1.0");
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
  ArduinoOTA.onStart([]() { LOGLN("[OTA] Update started; keep power connected"); });
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
  String saved=prefs.getString("tracked","");if(validMac(saved))snprintf(tracked,sizeof(tracked),"%s",saved.c_str());
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
          core.begin("c5-ble-presence", "sensor.ble_presence", "C5 BLE Presence", "c5-ble-presence-1.0");
          coreReady = true;
          web.on("/status", HTTP_GET, statusReply);
          web.on("/", HTTP_GET, pageReply);
          web.on("/nearby",HTTP_GET,nearbyReply);
          web.on("/track",HTTP_POST,trackReply);
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
