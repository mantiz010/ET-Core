#pragma once
#include "ETCore.h"

// Optional receiver. Only call handle() from ETCore's authenticated onCommand.
// Storage is injected so protocol failures can be tested without writing flash.
class ETCoreOTASink {
public:
 virtual ~ETCoreOTASink() {}
 virtual size_t capacity() const = 0;
 virtual bool begin(size_t size, const char* sha256) = 0;
 virtual bool write(const uint8_t* data, size_t size) = 0;
 virtual bool finish() = 0; // verifies digest and image, DOES NOT select boot slot
 virtual bool activate() = 0;
 virtual void abort() = 0;
 virtual void restart() = 0;
 virtual bool confirmHealthy() { return true; }
 virtual void poll() {}
};
class ETCoreOTA {
public:
 static constexpr size_t LEGACY_CHUNK = 192;
 static constexpr size_t CHUNK = 288;
 static constexpr uint8_t WINDOW = 2;
 ETCoreOTA(ETCore& core, ETCoreOTASink& sink, const char* board)
   : _core(core), _sink(sink), _board(board) {}
 void describe(JsonObject hello) {
  JsonObject ota=hello["ota"].to<JsonObject>();
  ota["ready"]=_core.encryptionEnabled() && _sink.capacity()>0;
  ota["protocol"]="etcore-chunk-v1";ota["board"]=_board;
  ota["max_chunk"]=CHUNK;ota["max_window"]=WINDOW;ota["max_image_size"]=(uint32_t)_sink.capacity();
 }
 bool active() const { return _phase==Receiving || _phase==Ready || _phase==Rebooting; }
 bool handle(const char* cls, JsonObject p) {
  if(strcmp(cls,"firmware.ota"))return false;
  const char* op=p["op"]|"";const char* id=p["transfer"]|"";
  bool ok=false;
  if(!_core.encryptionEnabled()){reject(p,"encryption_required");return true;}
  if(!strcmp(op,"health"))ok=!active()&&_core.hubConfirmed()&&_sink.confirmHealthy();
  else if(!strcmp(op,"begin"))ok=start(p);
  else if(!validId(id)||strcmp(id,_id))ok=false;
  else if(!strcmp(op,"chunk"))ok=chunk(p);
  else if(!strcmp(op,"finish")){
   if(_phase==Ready)ok=true;
   else if(_phase==Receiving && _received==_size){
    ok=_sink.finish();_phase=ok?Ready:Failed;
    if(!ok)_sink.abort();
   }
  }else if(!strcmp(op,"commit")){
   if(_phase==Rebooting)ok=true;
   else if(_phase==Ready){ok=_sink.activate();if(ok){_phase=Rebooting;_rebootAt=millis()+1500;}else{_sink.abort();_phase=Failed;}}
  }else if(!strcmp(op,"abort")){
   if(_phase!=Rebooting){_sink.abort();_phase=Idle;ok=true;}
  }
  if(ok){_lastActivity=millis();_core.sendAck(p["cmd_id"]|"",true);}
  else reject(p,"update_rejected");
  return true;
 }
 void loop(uint32_t now=millis()) {
  _sink.poll();
  if(_phase==Rebooting && (int32_t)(now-_rebootAt)>=0){_core.sendBye();_sink.restart();}
  else if((_phase==Receiving||_phase==Ready) && (uint32_t)(now-_lastActivity)>=60000){
   _sink.abort();_phase=Failed;_core.sendError("ota_timeout","Transfer stopped; previous boot slot retained");
  }
 }
private:
 enum Phase {Idle,Receiving,Ready,Rebooting,Failed};
 ETCore& _core;ETCoreOTASink& _sink;const char* _board;
 Phase _phase=Idle;char _id[33]={};char _hash[65]={};
 uint32_t _size=0,_received=0,_lastActivity=0,_rebootAt=0;
 size_t _chunkSize=LEGACY_CHUNK;uint8_t _window=1,_recentIndex=0;
 struct Block {bool used=false;uint32_t offset=0;size_t size=0;uint8_t data[CHUNK]={};};
 Block _pending[WINDOW],_recent[WINDOW];
 static bool validId(const char* s){return hex(s,32);}
 static bool hex(const char* s,size_t n){if(strlen(s)!=n)return false;for(size_t i=0;i<n;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return false;return true;}
 static int b64(char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;}
 static size_t decode(const char* s,uint8_t* out){
  size_t n=strlen(s),used=0;if(!n||n%4||n>((CHUNK+2)/3)*4)return 0;
  for(size_t i=0;i<n;i+=4){int a=b64(s[i]),b=b64(s[i+1]),c=b64(s[i+2]),d=b64(s[i+3]);
   bool pc=s[i+2]=='=',pd=s[i+3]=='=';
   if(a<0||b<0||(!pc&&c<0)||(!pd&&d<0)||(pc&&!pd)||((pc||pd)&&i+4!=n))return 0;
   if((pc&&(b&15))||(!pc&&pd&&(c&3)))return 0;
   out[used++]=(uint8_t)((a<<2)|(b>>4));
   if(!pc)out[used++]=(uint8_t)((b<<4)|(c>>2));
   if(!pd)out[used++]=(uint8_t)((c<<6)|d);
  }return used;
 }
 bool start(JsonObject p){
  const char* id=p["transfer"]|"";const char* hash=p["sha256"]|"";const char* board=p["board"]|"";
  if(!validId(id)||!hex(hash,64)||strcmp(board,_board)||!p["size"].is<uint32_t>())return false;
  uint32_t size=p["size"].as<uint32_t>();if(size<32||size>_sink.capacity())return false;
  if((!p["chunk_size"].isNull()&&!p["chunk_size"].is<uint32_t>())
     ||(!p["window"].isNull()&&!p["window"].is<uint32_t>()))return false;
  uint32_t chunkSize=p["chunk_size"]|((uint32_t)LEGACY_CHUNK);
  uint32_t window=p["window"]|1U;
  if((chunkSize!=LEGACY_CHUNK&&chunkSize!=CHUNK)||window<1||window>WINDOW)return false;
  if(active())return !strcmp(id,_id)&&!strcmp(hash,_hash)&&size==_size&&_phase==Receiving
                    &&_chunkSize==chunkSize&&_window==window;
  if(!_sink.begin(size,hash))return false;
  snprintf(_id,sizeof(_id),"%s",id);snprintf(_hash,sizeof(_hash),"%s",hash);
  _size=size;_received=0;_chunkSize=chunkSize;_window=(uint8_t)window;_recentIndex=0;
  for(uint8_t i=0;i<WINDOW;i++){_pending[i].used=false;_recent[i].used=false;}
  _phase=Receiving;return true;
 }
 bool chunk(JsonObject p){
  if(_phase!=Receiving||!p["offset"].is<uint32_t>())return false;
  uint32_t offset=p["offset"].as<uint32_t>();uint8_t data[CHUNK];
  size_t n=decode(p["data"]|"",data);
  if(!n||offset>=_size||offset%_chunkSize)return false;
  size_t expected=(_size-offset<_chunkSize)?_size-offset:_chunkSize;
  if(n!=expected)return false;
  if(offset<_received){
   for(uint8_t i=0;i<WINDOW;i++)if(_recent[i].used&&_recent[i].offset==offset)
    return _recent[i].size==n&&!memcmp(_recent[i].data,data,n);
   return false;
  }
  if(offset-_received>=_window*_chunkSize)return false;
  Block& slot=_pending[(offset/_chunkSize)%WINDOW];
  if(slot.used)return slot.offset==offset&&slot.size==n&&!memcmp(slot.data,data,n);
  slot.used=true;slot.offset=offset;slot.size=n;memcpy(slot.data,data,n);
  // Buffer acceptance may be acknowledged early. Boot selection still requires
  // every byte written in order, then the full image SHA/native verification.
  while(_received<_size){
   Block& next=_pending[(_received/_chunkSize)%WINDOW];
   if(!next.used||next.offset!=_received)break;
   if(!_sink.write(next.data,next.size)){_sink.abort();_phase=Failed;return false;}
   _recent[_recentIndex]=next;_recentIndex=(_recentIndex+1)%WINDOW;
   _received+=(uint32_t)next.size;next.used=false;
  }
  return true;
 }
 void reject(JsonObject p,const char* reason){_core.sendAck(p["cmd_id"]|"",false);_core.sendError("ota",reason);}
};
