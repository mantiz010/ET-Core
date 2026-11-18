// Development preview. No WiFi credentials, sensors, GPIO or automatic network creation.
// Enter the existing Thread operational dataset privately; never publish it.
const char* THREAD_DATASET_HEX = "YOUR_THREAD_DATASET_HEX";
const char* HUB_IPV6 = "YOUR_REACHABLE_HUB_IPV6";
const char* ETCORE_TEST_KEY = "YOUR_SEPARATE_64_HEX_TEST_KEY";
#include <Arduino.h>
#include <OThread.h>
#include <openthread/dataset.h>
#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <ETCore.h>
#include <ETCoreOpenThread.h>
#include <ETCoreOTAESP32.h>
// Defer boot acceptance until the authenticated HA health check.
extern "C" bool verifyRollbackLater() { return true; }
#if !defined(CONFIG_IDF_TARGET_ESP32C6)
#error "This preview targets ESP32C6 Dev Module."
#endif
#if ARDUINO_USB_CDC_ON_BOOT
#define LOGF(...) do { Serial.printf(__VA_ARGS__); Serial0.printf(__VA_ARGS__); } while(0)
#define LOGLN(...) do { Serial.println(__VA_ARGS__); Serial0.println(__VA_ARGS__); } while(0)
#else
#define LOGF(...) Serial.printf(__VA_ARGS__)
#define LOGLN(...) Serial.println(__VA_ARGS__)
#endif
ETCore core;
ETCoreOTAESP32 otaStorage;
ETCoreOTA ota(core,otaStorage,"esp32c6-dev-4m");
void otaHello(JsonObject p){ota.describe(p);}
void command(const char* cls,JsonObject p){
 if(!ota.handle(cls,p))core.sendAck(p["cmd_id"]|"",false);
}
ETCoreOpenThread* threadTransport=nullptr;
bool configured=false,started=false;
uint32_t lastReport=0,lastStatus=0,reading=0;
bool hexBytes(const char* src,uint8_t* dst,size_t cap,size_t& used){
 size_t n=strlen(src);if(!n||n%2||n/2>cap)return false;
 for(size_t i=0;i<n;i++)if(!isxdigit((unsigned char)src[i]))return false;
 for(size_t i=0;i<n/2;i++){char b[3]={src[i*2],src[i*2+1],0};dst[i]=(uint8_t)strtoul(b,nullptr,16);}used=n/2;return true;
}
void report(){JsonDocument d;d["test_only"]=true;d["value"]=reading;d["transport"]= "thread";core.sendState(d.as<JsonObject>());}
void setup(){
 Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
 Serial0.begin(115200);
#endif
 delay(500);LOGLN("ET-Core C6 Thread OTA receiver preview 0.3; WiFi disabled; encrypted update commands");
 LOGF("[HUB] Direct HA endpoint=[%s]:15562; configure HA IPv6, not a relay\n", HUB_IPV6);
 IPAddress hub;
 otOperationalDatasetTlvs dataset={};size_t used=0;
 if(!hub.fromString(HUB_IPV6)||hub.type()!=IPv6||!hexBytes(THREAD_DATASET_HEX,dataset.mTlvs,sizeof(dataset.mTlvs),used)||!core.enableEncryptionHex(ETCORE_TEST_KEY)){
  LOGLN("[SETUP] Edit THREAD_DATASET_HEX, HUB_IPV6 and ETCORE_TEST_KEY in this sketch. No network started.");return;
 }
 dataset.mLength=(uint8_t)used;OThread.begin(false);
 if(!OThread){LOGLN("[ERROR] OpenThread failed to start");return;}
 if(!esp_openthread_lock_acquire(pdMS_TO_TICKS(1000))){LOGLN("[ERROR] Thread lock unavailable");return;}
 otError err=otDatasetSetActiveTlvs(OThread.getInstance(),&dataset);
 if(err==OT_ERROR_NONE)otThreadSetRouterEligible(OThread.getInstance(),false);
 esp_openthread_lock_release();
 memset(&dataset,0,sizeof(dataset));
 if(err!=OT_ERROR_NONE){LOGF("[ERROR] Existing dataset rejected: %d\n",err);return;}
 threadTransport=new ETCoreOpenThread(hub);core.setPort(15562);
 if(!core.setTransport(threadTransport)){LOGLN("[ERROR] Adapter rejected");return;}
 core.onHello(otaHello);core.onCommand(command);
 core.setLeaseInterval(5000);core.setLeaseTtl(30);core.onSync(report);
 OThread.networkInterfaceUp();OThread.start();configured=true;
}
void loop(){uint32_t now=millis();ota.loop();
 if(configured){
  bool attached=OThread.otGetDeviceRole()>=OT_ROLE_CHILD;
  if(attached&&!started){core.begin("c6-thread-test","sensor.test","C6 Thread TEST ONLY","thread-preview-0.3");started=core.transportReady();if(started){LOGF("[OTA] Encrypted Thread receiver ready; inactive capacity=%lu bytes\n",(unsigned long)otaStorage.capacity());report();}}
  if(attached&&started){core.loop();if(now-lastReport>=10000){lastReport=now;++reading;report();LOGF("[TEST] counter=%lu transport=Thread\n",(unsigned long)reading);}}
  if(now-lastStatus>=5000){lastStatus=now;LOGF("[THREAD] role=%s attached=%d adapter=%d send_failures=%lu uptime=%lu s\n",OThread.otGetStringDeviceRole(),attached,started,(unsigned long)core.transportSendFailures(),(unsigned long)(now/1000));LOGF("[EUI64] %s\n",OThread.getEui64().c_str());for(auto ip:OThread.getAllUnicastAddresses())LOGF("[IPV6] %s\n",ip.toString().c_str());}
 }else if(now-lastStatus>=5000){lastStatus=now;LOGLN("[SETUP] Not configured; no sensor readings invented. See settings at top of C6Thread.ino.");}
 delay(2);
}
