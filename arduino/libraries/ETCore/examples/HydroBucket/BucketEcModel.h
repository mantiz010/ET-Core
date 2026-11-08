#pragma once
#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>

namespace BucketEC {
// Wire contract: ORIGINAL ECSensor::readSensor(T), sent as raw,T in Celsius.
// The upstream source and installed library match commit a64e3ae66f322aa7b9f56ead0767255a77e93c71.
constexpr float SENDER_ALPHA = 0.020f;
constexpr float SOLUTION_ALPHA = 0.020f;
constexpr uint32_t MAGIC = 0x45433601;
struct Point {float senderRaw; float temperature; float ec25;};
struct Calibration {
  uint32_t magic, active;
  float gain, offset; // Offset is in uncompensated circuit timing cycles.
  Point point14, point27, pending14, pending27;
  uint32_t pendingMask, checksum;
};
static_assert(sizeof(Calibration)==72, "Calibration storage layout changed");
inline float factor(float t,float alpha) {return 1.0f+alpha*(t-25.0f);}
inline bool validTemperature(float t) {return isfinite(t)&&t>0.0f&&t<50.0f;}
inline bool decodeTiming(float raw,float t,float &cycles) {
  if(!isfinite(raw)||raw<=0||raw>500000||!validTemperature(t))return false;
  cycles=raw*factor(t,SENDER_ALPHA); // undo the sender's division, once
  return isfinite(cycles)&&cycles>0;
}
inline bool validPoint(const Point &p) {
  float cycles;
  return decodeTiming(p.senderRaw,p.temperature,cycles)&&isfinite(p.ec25)&&p.ec25>=.05f&&p.ec25<=15;
}
inline uint32_t digest(const Calibration &c) {
  uint32_t result=2166136261u;
  const uint8_t *bytes=reinterpret_cast<const uint8_t *>(&c);
  for(size_t i=0;i<offsetof(Calibration,checksum);i++)result=(result^bytes[i])*16777619u;
  return result;
}
inline void seal(Calibration &c) {c.checksum=digest(c);}
inline Calibration emptyCalibration() {Calibration c={};c.magic=MAGIC;seal(c);return c;}
inline bool fitPair(const Point &a,const Point &b,float &gain,float &offset) {
  if(!validPoint(a)||!validPoint(b))return false;
  float ca,cb;
  if(!decodeTiming(a.senderRaw,a.temperature,ca)||!decodeTiming(b.senderRaw,b.temperature,cb))return false;
  const float xa=1.0f/(a.ec25*factor(a.temperature,SOLUTION_ALPHA));
  const float xb=1.0f/(b.ec25*factor(b.temperature,SOLUTION_ALPHA));
  if(fabsf(xa-xb)<.01f*fmaxf(xa,xb))return false;
  const float g=(ca-cb)/(xa-xb), o=ca-g*xa;
  if(!isfinite(g)||g<=0||g>=7500000||!isfinite(o)||o<0||o>=500000||o>=.90f*fminf(ca,cb))return false;
  gain=g;offset=o;return true;
}
inline bool valid(const Calibration &c) {
  if(c.magic!=MAGIC||c.checksum!=digest(c)||c.active>1||c.pendingMask>3)return false;
  if((c.pendingMask&1)&&(!validPoint(c.pending14)||c.pending14.ec25!=1.413f))return false;
  if((c.pendingMask&2)&&(!validPoint(c.pending27)||c.pending27.ec25!=2.770f))return false;
  if(!c.active)return c.gain==0&&c.offset==0;
  if(c.point14.ec25!=1.413f||c.point27.ec25!=2.770f)return false;
  float g,o;
  if(!fitPair(c.point14,c.point27,g,o))return false;
  return isfinite(c.gain)&&isfinite(c.offset)&&fabsf(c.gain-g)<=.0001f*fmaxf(1,g)&&
    fabsf(c.offset-o)<=.0001f*fmaxf(1,o);
}
inline bool calibrated(const Calibration &c) {return valid(c)&&c.active==1;}
inline const char *modeName(const Calibration &c) {
  if(!valid(c))return "invalid-calibration";
  if(c.active)return c.pendingMask?"two-point; replacement pending":"two-point";
  return c.pendingMask?"second-standard-required":"calibration-required";
}
inline bool convert(float cycles,float t,const Calibration &c,float &ec25) {
  if(!calibrated(c)||!isfinite(cycles)||cycles<=c.offset||!validTemperature(t))return false;
  // Circuit offset does not follow the solution's temperature coefficient.
  const float value=c.gain/((cycles-c.offset)*factor(t,SOLUTION_ALPHA));
  if(!isfinite(value)||value<.05f||value>15)return false;
  ec25=value;return true;
}
inline bool capturePoint(Calibration &c,unsigned point,float raw,float t) {
  if(!valid(c)||(point!=1&&point!=2))return false;
  Point captured={raw,t,point==1?1.413f:2.770f};
  if(!validPoint(captured))return false;
  Calibration next=c;
  if(point==1)next.pending14=captured;else next.pending27=captured;
  next.pendingMask|=point;
  if(next.pendingMask==3) {
    if(!fitPair(next.pending14,next.pending27,next.gain,next.offset))return false;
    next.point14=next.pending14;next.point27=next.pending27;next.active=1;
    next.pending14={};next.pending27={};next.pendingMask=0;
  }
  seal(next);if(!valid(next))return false;
  c=next;return true;
}
inline bool reuseLegacy(Calibration &c,float raw14,float t14,float raw27,float t27) {
  Calibration next=emptyCalibration();
  if(!capturePoint(next,1,raw14,t14)||!capturePoint(next,2,raw27,t27))return false;
  c=next;return true;
}
inline bool parseSample(const char *line,uint32_t &raw,float &temperature) {
  if(!line||*line<'0'||*line>'9')return false;
  char *end=nullptr;errno=0;unsigned long parsed=strtoul(line,&end,10);
  if(errno||parsed==0||parsed>500000||!end||*end!=',')return false;
  const char *start=end+1;errno=0;float t=strtof(start,&end),cycles;
  if(errno||end==start||!end||*end!='\0'||!decodeTiming(float(parsed),t,cycles))return false;
  raw=uint32_t(parsed);temperature=t;return true;
}
inline bool parseTemperature(const char *text,float &temperature) {
  if(!text||!*text)return false;
  char *end=nullptr;errno=0;float t=strtof(text,&end);
  if(errno||end==text||*end!='\0'||!validTemperature(t))return false;
  temperature=t;return true;
}
inline bool fresh(bool validSample,uint32_t samples,uint32_t now,uint32_t last,uint32_t timeout) {
  return validSample&&samples>=3&&uint32_t(now-last)<=timeout;
}
// Calibration snapshots retain complete raw/T pairs. No calibrated EC is needed
// to bootstrap a missing/invalid calibration. Eight samples span ~28 seconds.
struct RawWindow {
  Point samples[8];uint32_t at[8];unsigned count=0,index=0;
  void reset(){count=0;index=0;}
  bool push(float raw,float t,uint32_t now) {
    float cycles;if(!decodeTiming(raw,t,cycles)){reset();return false;}
    if(count&&uint32_t(now-at[(index+7)%8])>6000)reset();
    samples[index]={raw,t,0};at[index]=now;index=(index+1)%8;if(count<8)count++;
    return true;
  }
  bool freshRaw(uint32_t now) const {
    return count>=3&&uint32_t(now-at[(index+7)%8])<=6000;
  }
  bool stablePoint(uint32_t now,Point &out) const {
    if(count!=8||!freshRaw(now)||uint32_t(now-at[index])<24000||uint32_t(now-at[index])>60000)return false;
    unsigned order[8];float cycles[8],loT=50,hiT=0;
    for(unsigned i=0;i<8;i++) {
      order[i]=i;if(!decodeTiming(samples[i].senderRaw,samples[i].temperature,cycles[i]))return false;
      loT=fminf(loT,samples[i].temperature);hiT=fmaxf(hiT,samples[i].temperature);
    }
    if(hiT-loT>.5f)return false;
    for(unsigned i=0;i<7;i++)for(unsigned j=i+1;j<8;j++)if(cycles[order[j]]<cycles[order[i]]){
      unsigned tmp=order[i];order[i]=order[j];order[j]=tmp;
    }
    const float median=.5f*(cycles[order[3]]+cycles[order[4]]);
    if(cycles[order[7]]-cycles[order[0]]>.02f*median)return false;
    out=samples[order[3]];return true;
  }
};
}
