#pragma once
// UDP over Linux sockets. Each simulated device binds its own 127.0.0.x for
// unicast, plus a socket bound to the multicast group for group traffic.
// Multicast is sent via loopback with TTL 0 so nothing reaches the real LAN.
#include "Arduino.h"
#include "IPAddress.h"
#include "WiFi.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <vector>

class WiFiUDP : public Stream {
public:
  ~WiFiUDP() { if (_uc >= 0) close(_uc); if (_mc >= 0) close(_mc); }
  uint8_t beginMulticast(IPAddress group, uint16_t port) {
    _port = port;
    _uc = _sock();
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); inet_pton(AF_INET, g_sim_local_ip, &a.sin_addr);
    if (bind(_uc, (sockaddr*)&a, sizeof a) != 0) return 0;
    in_addr lo{}; inet_pton(AF_INET, "127.0.0.1", &lo);
    setsockopt(_uc, IPPROTO_IP, IP_MULTICAST_IF, &lo, sizeof lo);
    unsigned char ttl = 0; setsockopt(_uc, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    _mc = _sock();
    sockaddr_in g{}; g.sin_family = AF_INET; g.sin_port = htons(port); g.sin_addr.s_addr = htonl(group.be());
    if (bind(_mc, (sockaddr*)&g, sizeof g) != 0) return 0;
    ip_mreq m{}; m.imr_multiaddr.s_addr = htonl(group.be()); m.imr_interface = lo;
    setsockopt(_mc, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m);
    return 1;
  }
  int parsePacket() {
    _buf.clear(); _pos = 0;
    for (int fd : {_uc, _mc}) {
      if (fd < 0) continue;
      uint8_t tmp[2048]; sockaddr_in from{}; socklen_t fl = sizeof from;
      ssize_t n = recvfrom(fd, tmp, sizeof tmp, MSG_DONTWAIT, (sockaddr*)&from, &fl);
      if (n > 0) {
        // Ignore our own multicast echoes.
        char src[16]; inet_ntop(AF_INET, &from.sin_addr, src, sizeof src);
        if (fd == _mc && strcmp(src, g_sim_local_ip) == 0) continue;
        _buf.assign(tmp, tmp + n); _remote = IPAddress(ntohl(from.sin_addr.s_addr));
        return (int)n;
      }
    }
    return 0;
  }
  IPAddress remoteIP() const { return _remote; }
  int available() override { return (int)(_buf.size() - _pos); }
  int read() override { return _pos < _buf.size() ? _buf[_pos++] : -1; }
  int beginPacket(IPAddress ip, uint16_t port) { _out.clear(); _dst = ip; _dport = port; return 1; }
  size_t write(uint8_t c) override { _out.push_back(c); return 1; }
  size_t write(const uint8_t* b, size_t n) override { _out.insert(_out.end(), b, b + n); return n; }
  int endPacket() {
    sockaddr_in d{}; d.sin_family = AF_INET; d.sin_port = htons(_dport); d.sin_addr.s_addr = htonl(_dst.be());
    return sendto(_uc, _out.data(), _out.size(), 0, (sockaddr*)&d, sizeof d) == (ssize_t)_out.size();
  }
private:
  static int _sock() {
    int s = socket(AF_INET, SOCK_DGRAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
    return s;
  }
  int _uc = -1, _mc = -1; uint16_t _port = 0, _dport = 0;
  std::vector<uint8_t> _buf, _out; size_t _pos = 0;
  IPAddress _remote, _dst;
};
