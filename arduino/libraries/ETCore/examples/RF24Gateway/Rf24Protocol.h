#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
namespace Rf24Protocol {
#pragma pack(push,1)
struct Node1 {float temperature,humidity,pressure;uint16_t aqi,tvoc,eco2;};
struct Node2 {uint8_t nodeId;float lux;};
struct Node3 {float temperature,humidity;int32_t eco2,agsPPB;};
struct Node4 {float temperature,humidity,pressure;uint16_t eCO2,tvoc,aqi;};
struct Node5 {uint8_t nodeId;float lux,batteryVoltage;};
using Node6=Node5;
struct Node7 {uint8_t nodeId;float temperature,humidity;};
struct Node8 {float ahtTemperature,ahtHumidity,bmpTemperature,bmpPressure,batteryVoltage;};
struct Node9 {float temperature,humidity,pressure,gasResistance,batteryVoltage;};
struct SwitchData {uint8_t state;};
#pragma pack(pop)
static_assert(sizeof(float)==4 && sizeof(int32_t)==4,"Wire format needs 32-bit floats/integers");
static_assert(sizeof(Node1)==18 && sizeof(Node2)==5 && sizeof(Node3)==16 && sizeof(Node4)==18,"Legacy packet layout mismatch");
static_assert(sizeof(Node5)==9 && sizeof(Node6)==9 && sizeof(Node7)==9 && sizeof(Node8)==20 && sizeof(Node9)==20 && sizeof(SwitchData)==1,"Legacy packet layout mismatch");
static constexpr uint16_t ADDRESSES[]={01,02,03,04,05,033,034,011,013};
static constexpr uint16_t SWITCH_ADDRESS=012;
static constexpr uint8_t CHANNEL=90;
static constexpr unsigned SENSOR_COUNT=33;
struct Sample {uint8_t first,count;double values[6];};
inline bool between(double v,double low,double high){return isfinite(v) && v>=low && v<=high;}
inline bool decode(uint16_t address,const void* payload,size_t length,Sample& out) {
 if(!payload)return false;
 Sample s={};
 switch(address) {
 case 01:{if(length!=sizeof(Node1))return false;Node1 d;memcpy(&d,payload,sizeof(d));s={0,6,{d.temperature,d.humidity,d.pressure,double(d.aqi),double(d.tvoc),double(d.eco2)}};break;}
 case 02:{if(length!=sizeof(Node2))return false;Node2 d;memcpy(&d,payload,sizeof(d));s={6,1,{d.lux}};break;}
 case 03:{if(length!=sizeof(Node3))return false;Node3 d;memcpy(&d,payload,sizeof(d));s={7,4,{d.temperature,d.humidity,double(d.eco2),double(d.agsPPB)}};break;}
 case 04:{if(length!=sizeof(Node4))return false;Node4 d;memcpy(&d,payload,sizeof(d));s={11,6,{d.temperature,d.humidity,d.pressure,double(d.eCO2),double(d.tvoc),double(d.aqi)}};break;}
 case 05:case 033:{if(length!=sizeof(Node5))return false;Node5 d;memcpy(&d,payload,sizeof(d));s={uint8_t(address==05?17:19),2,{d.lux,d.batteryVoltage}};break;}
 case 034:{if(length!=sizeof(Node7))return false;Node7 d;memcpy(&d,payload,sizeof(d));s={21,2,{d.temperature,d.humidity}};break;}
 case 011:{if(length!=sizeof(Node8))return false;Node8 d;memcpy(&d,payload,sizeof(d));s={23,5,{d.ahtTemperature,d.ahtHumidity,d.bmpTemperature,d.bmpPressure,d.batteryVoltage}};break;}
 case 013:{if(length!=sizeof(Node9))return false;Node9 d;memcpy(&d,payload,sizeof(d));s={28,5,{d.temperature,d.humidity,d.pressure,d.gasResistance,d.batteryVoltage}};break;}
 default:return false;
 }
 // Reject the complete packet before changing any cached reading.
 for(unsigned i=0;i<s.count;++i){unsigned k=s.first+i;double v=s.values[i];if(!isfinite(v))return false;
  bool humidity=k==1 || k==8 || k==12 || k==22 || k==24 || k==29;
  bool temperature=k==0 || k==7 || k==11 || k==21 || k==23 || k==25 || k==28;
  bool battery=k==18 || k==20 || k==27 || k==32;
  if(humidity && !between(v,0,100))return false;
  if(temperature && !between(v,-100,200))return false;
  if(battery && !between(v,0,30))return false;
  if(!humidity && !temperature && !battery && !between(v,0,1e12))return false;
 }
 out=s;return true;
}
}
