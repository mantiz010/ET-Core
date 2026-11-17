#pragma once
#include "ETCoreTransport.h"
#include <OThreadUDP.h>
// Explicit unicast bootstrap. Does not join IPv4 discovery or create a network.
class ETCoreOpenThread : public ETCoreTransport {
public:
 explicit ETCoreOpenThread(IPAddress hub) : _bootstrap(hub) {}
 bool begin(uint16_t port) override { return _udp.begin(port)==1; }
 int receive(char* data,size_t max,IPAddress& from) override {
  int n=_udp.parsePacket();if(n<=0)return 0;
  if((size_t)n>max || n>OT_UDP_MAX_PACKET_SIZE){_udp.flush();return 0;}
  from=_udp.remoteIP();return _udp.read((uint8_t*)data,(size_t)n);
 }
 bool send(IPAddress to,uint16_t port,const char* data,size_t size) override {
  // Initial preview stays inside the installed OThreadUDP queue's configured datagram cap.
  // It does not claim one IEEE802.15.4 frame; 6LoWPAN may fragment it.
  if(size>OT_UDP_MAX_PACKET_SIZE||!_udp.beginPacket(to,port))return false;
  if(_udp.write((const uint8_t*)data,size)!=size)return false;
  return _udp.endPacket()==1;
 }
 IPAddress bootstrapAddress() const override {return _bootstrap;}
 const char* name() const override {return "thread";}
private:
 OThreadUDP _udp;
 IPAddress _bootstrap;
};
