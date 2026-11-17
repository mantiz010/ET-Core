#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
// iBeacon manufacturer data includes Apple's little-endian company ID.
inline bool beaconIdentity(const uint8_t* p,size_t n,char* out,size_t cap) {
 if(n<25||cap<50||p[0]!=0x4c||p[1]!=0||p[2]!=2||p[3]!=0x15)return false;
 snprintf(out,cap,"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x:%u:%u",p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15],p[16],p[17],p[18],p[19],(unsigned)((p[20]<<8)|p[21]),(unsigned)((p[22]<<8)|p[23]));return true;
}
inline bool validBeaconIdentity(const char* s) {
 size_t n=strlen(s);if(n<40||n>48)return false;
 for(size_t i=0;i<36;i++){if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}else if(!isxdigit((unsigned char)s[i]))return false;}
 if(s[36]!=':')return false;
 size_t at=37;
 for(int part=0;part<2;part++){unsigned v=0;size_t start=at;while(s[at]>='0'&&s[at]<='9'){v=v*10+(s[at++]-'0');if(v>65535||at-start>5)return false;}if(at==start)return false;if(part==0){if(s[at++]!=':')return false;}else if(s[at]!=0)return false;}return true;
}
struct RoomPresence {
 bool present=false;uint32_t strongAt=0,weakAt=0;bool strong=false,weak=false;
 void reset(){present=false;strong=false;weak=false;}
 void update(uint32_t now,bool recent,float rssi,int enter=-65,int exit=-75){
  if(!recent){reset();return;}
  if(!present){weak=false;if(rssi>=enter){if(!strong){strong=true;strongAt=now;}if((uint32_t)(now-strongAt)>=3000)present=true;}else strong=false;}
  else {strong=false;if(rssi<exit){if(!weak){weak=true;weakAt=now;}if((uint32_t)(now-weakAt)>=15000)reset();}else weak=false;}
 }
};
