#include <ETCoreOTA.h>
#include <cassert>
#include <vector>
#include <string>
#include <openssl/sha.h>
#include <openssl/evp.h>
WiFiStub WiFi;char g_sim_local_ip[16]="127.0.0.2";
static std::string hex(const std::vector<uint8_t>& b){uint8_t d[32];SHA256(b.data(),b.size(),d);char s[65];for(int i=0;i<32;i++)snprintf(s+2*i,3,"%02x",d[i]);return s;}
static std::string b64(const std::vector<uint8_t>& b){std::string s((b.size()+2)/3*4,' ');EVP_EncodeBlock((unsigned char*)s.data(),b.data(),b.size());return s;}
static std::vector<uint8_t> un64(const char* s){size_t n=strlen(s);std::vector<uint8_t>b(n);int k=EVP_DecodeBlock(b.data(),(const uint8_t*)s,n);while(n&&s[n-1]=='='){--n;--k;}b.resize(k);return b;}
class DryTransport:public ETCoreTransport{
public:std::vector<std::string> packets;std::string incoming,session;
 bool begin(uint16_t)override{return true;}int receive(char* b,size_t n,IPAddress& from)override{if(incoming.empty())return 0;assert(incoming.size()<=n);int size=incoming.size();memcpy(b,incoming.data(),size);incoming.clear();from=IPAddress(127,0,0,1);return size;}
 bool send(IPAddress,uint16_t,const char* p,size_t n)override{packets.emplace_back(p,n);return true;}
 IPAddress bootstrapAddress()const override{return IPAddress(127,0,0,1);}const char* name()const override{return "thread";}
 bool ack(){
  for(auto i=packets.rbegin();i!=packets.rend();++i){JsonDocument m;deserializeJson(m,*i);if(strcmp(m["type"]|"","ack"))continue;
   auto p=m["payload"].as<JsonObject>();if(!p["_enc"].isNull()){
    auto n=un64(p["n"]),c=un64(p["ct"]),t=un64(p["tag"]);std::vector<uint8_t>plain(c.size());uint8_t input[37],key[32];memset(input,0xab,32);memcpy(input+32,"bench",5);SHA256(input,37,key);
    char aad[80];snprintf(aad,sizeof(aad),"v1|ack|bench|%u",m["seq"].as<unsigned>());
    assert(ETChaCha20Poly1305::decrypt(key,n.data(),(uint8_t*)aad,strlen(aad),c.data(),c.size(),t.data(),plain.data()));JsonDocument d;assert(!deserializeJson(d,plain.data(),plain.size()));return d["ok"].as<bool>();
   }return p["ok"].as<bool>();
  }assert(false);return false;
 }
};
static void prime(DryTransport& t,ETCore& core){
 JsonDocument hello;deserializeJson(hello,t.packets.front());auto n=un64(hello["payload"]["n"]);char session[15];for(int i=0;i<7;i++)snprintf(session+2*i,3,"%02x",n[1+i]);
 t.session=session;
 JsonDocument p;p["hub"]="127.0.0.1";p["hello"]=false;p["_ds"]=session;std::string plain;serializeJson(p,plain);
 uint8_t input[37],key[32],nonce[12]={3,1,2,3,4,5,6,7,1,0,0,0},tag[16];memset(input,0xab,32);memcpy(input+32,"bench",5);SHA256(input,37,key);std::vector<uint8_t>ct(plain.size());const char* aad="v1|announce|bench|1";
 assert(ETChaCha20Poly1305::encrypt(key,nonce,(uint8_t*)aad,strlen(aad),(uint8_t*)plain.data(),plain.size(),ct.data(),tag));
 JsonDocument m;m["v"]=1;m["type"]="announce";m["id"]="bench";m["class"]="hub";m["seq"]=1;m["payload"]["_enc"]=1;m["payload"]["n"]=b64(std::vector<uint8_t>(nonce,nonce+12));m["payload"]["ct"]=b64(ct);m["payload"]["tag"]=b64(std::vector<uint8_t>(tag,tag+16));serializeJson(m,t.incoming);core.loop();assert(core.hubConfirmed());
}
class Sink:public ETCoreOTASink{
public:std::vector<uint8_t>bytes;std::string expected;int begins=0,writes=0,aborts=0,activations=0,restarts=0;int failAt=0;bool writeFail=false,verified=false;
 size_t capacity()const override{return 4096;}bool begin(size_t,const char* h)override{begins++;bytes.clear();expected=h;verified=false;return true;}
 bool write(const uint8_t* b,size_t n)override{writes++;if(writeFail||(failAt&&writes==failAt))return false;bytes.insert(bytes.end(),b,b+n);return true;}
 bool finish()override{return verified=hex(bytes)==expected;}bool activate()override{if(!verified)return false;activations++;return true;}
 void abort()override{aborts++;verified=false;}void restart()override{restarts++;}
};
static bool deliver(DryTransport& t,ETCore& core,uint32_t seq,const char* commandId,JsonDocument& p){
 p["cmd_id"]=commandId;p["_ds"]=t.session;std::string plain;serializeJson(p,plain);
 uint8_t input[37],key[32],nonce[12]={2,1,2,3,4,5,6,7,0,0,0,0},tag[16];
 for(int i=0;i<4;i++)nonce[8+i]=(uint8_t)(seq>>(i*8));
 memset(input,0xab,32);memcpy(input+32,"bench",5);SHA256(input,37,key);
 std::string aad="v1|cmd|bench|"+std::to_string(seq);std::vector<uint8_t>ct(plain.size());
 assert(ETChaCha20Poly1305::encrypt(key,nonce,(uint8_t*)aad.data(),aad.size(),(uint8_t*)plain.data(),plain.size(),ct.data(),tag));
 JsonDocument m;m["v"]=1;m["type"]="cmd";m["id"]="bench";m["class"]="firmware.ota";m["seq"]=seq;m["payload"]["_enc"]=1;m["payload"]["n"]=b64(std::vector<uint8_t>(nonce,nonce+12));m["payload"]["ct"]=b64(ct);m["payload"]["tag"]=b64(std::vector<uint8_t>(tag,tag+16));serializeJson(m,t.incoming);assert(t.incoming.size()<=1024);core.loop();return t.ack();
}
static ETCoreOTA* helloOTA=nullptr;
static const char* id="0123456789abcdef0123456789abcdef";
int main(){
 DryTransport t;ETCore core;assert(core.enableEncryptionHex("abababababababababababababababababababababababababababababababab"));assert(core.setTransport(&t));Sink sink;ETCoreOTA ota(core,sink,"esp32c6-dev-4m");helloOTA=&ota;core.onHello([](JsonObject p){helloOTA->describe(p);});core.begin("bench","sensor.test","Test","1.0");prime(t,core);
 std::vector<uint8_t> image(32);image[0]=0xe9;auto sha=hex(image);
 auto cmd=[&](const char* op){JsonDocument d;auto p=d.to<JsonObject>();p["op"]=op;p["transfer"]=id;p["cmd_id"]="test";return d;};
 auto start=[&](const std::string& hash){auto d=cmd("begin");d["size"]=32;d["sha256"]=hash;d["board"]="esp32c6-dev-4m";ota.handle("firmware.ota",d.as<JsonObject>());return t.ack();};
 auto chunk=[&](uint32_t offset,std::string data){auto d=cmd("chunk");d["offset"]=offset;d["data"]=data;ota.handle("firmware.ota",d.as<JsonObject>());return t.ack();};
 auto op=[&](const char* name){auto d=cmd(name);ota.handle("firmware.ota",d.as<JsonObject>());return t.ack();};
 {auto d=cmd("begin");d["size"]=32;d["sha256"]=sha;d["board"]="wrong-board";ota.handle("firmware.ota",d.as<JsonObject>());assert(!t.ack()&&sink.begins==0);}
 assert(!chunk(0,b64(image)));assert(!op("commit"));assert(op("health"));
 assert(start(sha));assert(start(sha)&&sink.begins==1);assert(!op("finish"));
 assert(!chunk(1,b64(image)));assert(!chunk(0,"!!!!"));assert(!chunk(0,"AA=A"));
 assert(chunk(0,b64(image)));assert(chunk(0,b64(image))&&sink.writes==1);
 auto changed=image;changed[1]=1;assert(!chunk(0,b64(changed)));assert(!chunk(32,b64(image)));
 assert(op("finish"));assert(sink.activations==0);assert(op("finish"));assert(op("commit"));assert(sink.activations==1);assert(op("commit")&&sink.activations==1);assert(!op("abort"));
 ota.loop(millis()+2000);assert(sink.restarts==1);
 Sink bad;ETCoreOTA ota2(core,bad,"esp32c6-dev-4m");
 auto d=cmd("begin");d["size"]=32;d["sha256"]=std::string(64,'0');d["board"]="esp32c6-dev-4m";ota2.handle("firmware.ota",d.as<JsonObject>());assert(t.ack());d=cmd("chunk");d["offset"]=0;d["data"]=b64(image);ota2.handle("firmware.ota",d.as<JsonObject>());assert(t.ack());d=cmd("finish");ota2.handle("firmware.ota",d.as<JsonObject>());assert(!t.ack()&&bad.activations==0&&bad.aborts==1);
 d=cmd("begin");d["size"]=32;d["sha256"]=sha;d["board"]="esp32c6-dev-4m";ota2.handle("firmware.ota",d.as<JsonObject>());assert(t.ack());ota2.loop(millis()+60001);assert(!ota2.active()&&bad.aborts==2&&bad.activations==0);
 ETCore plaintext;Sink noKey;ETCoreOTA protectedOTA(plaintext,noKey,"esp32c6-dev-4m");protectedOTA.handle("firmware.ota",d.as<JsonObject>());assert(noKey.begins==0);
 // Real encrypted SDK path: deliver the second chunk before the first, then
 // resend the first with a fresh command seq after its stale-counter reply.
 DryTransport t2;ETCore c2;Sink piped;ETCoreOTA o2(c2,piped,"esp32c6-dev-4m");helloOTA=&o2;
 assert(c2.enableEncryptionHex("abababababababababababababababababababababababababababababababab"));assert(c2.setTransport(&t2));
 c2.onHello([](JsonObject p){helloOTA->describe(p);});c2.onCommand([](const char* cls,JsonObject p){helloOTA->handle(cls,p);});c2.begin("bench","sensor.test","Window","1.1");prime(t2,c2);
 std::vector<uint8_t> big(576);big[0]=0xe9;for(size_t i=1;i<big.size();i++)big[i]=(uint8_t)i;
 auto begin=cmd("begin");begin["size"]=big.size();begin["sha256"]=hex(big);begin["board"]="esp32c6-dev-4m";begin["chunk_size"]=288;begin["window"]=2;
 assert(deliver(t2,c2,1,"begin",begin));
 auto second=cmd("chunk");second["offset"]=288;second["data"]=b64(std::vector<uint8_t>(big.begin()+288,big.end()));assert(deliver(t2,c2,3,"second",second));assert(piped.writes==0);
 auto first=cmd("chunk");first["offset"]=0;first["data"]=b64(std::vector<uint8_t>(big.begin(),big.begin()+288));assert(!deliver(t2,c2,2,"first",first));assert(piped.writes==0);
 assert(deliver(t2,c2,4,"first",first));assert(piped.writes==2&&piped.bytes==big);
 assert(deliver(t2,c2,5,"second",second));assert(piped.writes==2); // lost ACK retry
 auto finish=cmd("finish");assert(deliver(t2,c2,6,"finish",finish));assert(piped.verified&&piped.activations==0);
 // A buffered positive ACK cannot hide a later flash-write failure.
 Sink broken;broken.failAt=2;ETCoreOTA o3(c2,broken,"esp32c6-dev-4m");
 o3.handle("firmware.ota",begin.as<JsonObject>());assert(t2.ack());o3.handle("firmware.ota",second.as<JsonObject>());assert(t2.ack()&&broken.writes==0);
 o3.handle("firmware.ota",first.as<JsonObject>());assert(!t2.ack()&&broken.aborts==1);o3.handle("firmware.ota",finish.as<JsonObject>());assert(!t2.ack()&&broken.activations==0);
 // No negotiation fields means the exact legacy 192-byte serial profile.
 Sink legacy;ETCoreOTA oldProfile(c2,legacy,"esp32c6-dev-4m");std::vector<uint8_t> oldImage(384,7);oldImage[0]=0xe9;
 auto oldBegin=cmd("begin");oldBegin["size"]=384;oldBegin["sha256"]=hex(oldImage);oldBegin["board"]="esp32c6-dev-4m";oldProfile.handle("firmware.ota",oldBegin.as<JsonObject>());assert(t2.ack());
 for(uint32_t offset:{0U,192U}){auto c=cmd("chunk");c["offset"]=offset;c["data"]=b64(std::vector<uint8_t>(oldImage.begin()+offset,oldImage.begin()+offset+192));oldProfile.handle("firmware.ota",c.as<JsonObject>());assert(t2.ack());}
 oldProfile.handle("firmware.ota",finish.as<JsonObject>());assert(t2.ack()&&legacy.bytes==oldImage&&legacy.writes==2);
 // Bound the reorder window and reject conflicting duplicates without changing it.
 Sink bounded;ETCoreOTA boundedOTA(c2,bounded,"esp32c6-dev-4m");std::vector<uint8_t> longImage(864,3);longImage[0]=0xe9;
 auto boundedBegin=cmd("begin");boundedBegin["size"]=864;boundedBegin["sha256"]=hex(longImage);boundedBegin["board"]="esp32c6-dev-4m";boundedBegin["chunk_size"]=288;boundedBegin["window"]=3;
 boundedOTA.handle("firmware.ota",boundedBegin.as<JsonObject>());assert(!t2.ack()&&bounded.begins==0);boundedBegin["window"]=2;boundedOTA.handle("firmware.ota",boundedBegin.as<JsonObject>());assert(t2.ack());
 auto ahead=cmd("chunk");ahead["offset"]=576;ahead["data"]=b64(std::vector<uint8_t>(longImage.begin()+576,longImage.end()));boundedOTA.handle("firmware.ota",ahead.as<JsonObject>());assert(!t2.ack()&&bounded.writes==0);
 ahead["offset"]=288;boundedOTA.handle("firmware.ota",ahead.as<JsonObject>());assert(t2.ack()&&bounded.writes==0);auto conflict=std::vector<uint8_t>(288,4);ahead["data"]=b64(conflict);boundedOTA.handle("firmware.ota",ahead.as<JsonObject>());assert(!t2.ack()&&bounded.writes==0);
 puts("PASS profile negotiation: legacy serial transfer, bounded reorder range, invalid window and conflicting duplicate rejection");
 for(const auto& packet:t2.packets)assert(packet.size()<=1024);
 puts("PASS window2: encrypted reordering, stale-seq resend, duplicate ACK, sequential writes, flash failure cannot activate");
 for(const auto& packet:t.packets)assert(packet.size()<=1024);
 puts("PASS receiver: authenticated ACKs, board/hash/offset/duplicate checks, finish-before-commit, timeout, no plaintext flash");
}
