#include <ETCore.h>
#include <cassert>
#include <openssl/sha.h>
#include <string>
WiFiStub WiFi;char g_sim_local_ip[16]="127.0.0.2";
struct ESPFake{uint32_t getFlashChipSize(){return 4*1024*1024;}void restart(){}} ESP;
uint32_t fakeNow=0;uint32_t testMillis(){return fakeNow;}
#define millis testMillis
#include <ETCoreOTAESP32.h>
#undef millis
std::string hash(const uint8_t* b,size_t n){uint8_t d[32];SHA256(b,n,d);char h[65];for(int i=0;i<32;i++)snprintf(h+2*i,3,"%02x",d[i]);return h;}
int main(){
 uint8_t image[64]={0xe9,13,2};auto sha=hash(image,sizeof(image));
 {ETCoreOTAESP32 sink;assert(sink.capacity()==1310720);assert(sink.begin(64,sha.c_str()));assert(sink.write(image,64));assert(activated==0);assert(sink.finish());assert(activated==0);assert(sink.activate());assert(activated==1);}
 {ETCoreOTAESP32 sink;assert(sink.begin(64,std::string(64,'0').c_str()));assert(sink.write(image,64));assert(!sink.finish());assert(!sink.activate());assert(activated==1);}
 {ETCoreOTAESP32 sink;assert(sink.begin(64,sha.c_str()));image[1]=9;assert(!sink.write(image,64));assert(flashBytes.empty());assert(!sink.activate());image[1]=13;}
 {ETCoreOTAESP32 sink;assert(sink.begin(64,sha.c_str()));image[2]=3;assert(!sink.write(image,64));assert(flashBytes.empty());image[2]=2;}
 {ETCoreOTAESP32 sink;assert(sink.begin(64,sha.c_str()));assert(sink.write(image,32));assert(!sink.finish());assert(!sink.activate());}
 for(int fail:{1,2,3}){failure=fail;ETCoreOTAESP32 sink;if(fail<3)assert(sink.capacity()==0);assert(!sink.begin(64,sha.c_str()));}
 failure=4;{ETCoreOTAESP32 sink;assert(sink.begin(64,sha.c_str()));assert(!sink.write(image,64));assert(!sink.activate());}
 failure=5;{ETCoreOTAESP32 sink;assert(sink.begin(64,sha.c_str()));assert(sink.write(image,64));assert(!sink.finish());assert(!sink.activate());}
 failure=0;bootState=ESP_OTA_IMG_PENDING_VERIFY;fakeNow=180001;{ETCoreOTAESP32 sink;sink.poll();sink.poll();assert(rolledBack==1);}
 bootState=ESP_OTA_IMG_PENDING_VERIFY;{ETCoreOTAESP32 sink;assert(sink.confirmHealthy());assert(bootState==ESP_OTA_IMG_VALID);sink.poll();sink.poll();assert(rolledBack==1);}
 puts("PASS flash backend: inactive slot only, SHA before activation, chip/flash checks, failures, pending-boot health and rollback deadline");
}
