#include <initializer_list>
#include <cstdint>
#include <cassert>
#define F(x) x
using time_t=long;
time_t epochValue=0;time_t time(time_t*){return epochValue;}
constexpr int WL_CONNECTED=3;
struct WiFiStub {int state=3;int status(){return state;}} WiFi;
struct DateTime {
 unsigned y,m,d,h,n,s;
 DateTime(unsigned yy=2026,unsigned mm=11,unsigned dd=8,unsigned hh=12,unsigned nn=0,unsigned ss=0):y(yy),m(mm),d(dd),h(hh),n(nn),s(ss){}
 unsigned year()const{return y;}unsigned month()const{return m;}unsigned day()const{return d;}
 unsigned hour()const{return h;}unsigned minute()const{return n;}unsigned second()const{return s;}
};
struct Rtc {unsigned writes=0;void adjust(DateTime){writes++;}DateTime now(){return {};}} rtc;
struct DebugStub {void println(const char*){}} Debug;
bool clockReady=false,clockSynced=false;
// INSERT_PRODUCTION_CLOCK
int main(){
 assert(validBucketClock(DateTime()));
 for(auto v:{DateTime(2025),DateTime(2106),DateTime(2026,0),DateTime(2026,13),DateTime(2026,1,0),DateTime(2026,1,32),DateTime(2026,1,1,24),DateTime(2026,1,1,1,60),DateTime(2026,1,1,1,1,60)})assert(!validBucketClock(v));
 for(auto e:{0L,43200L,1767225599L,4102444800L}){epochValue=e;serviceClock();assert(rtc.writes==0&&!clockReady);}
 epochValue=1794110400L;WiFi.state=0;serviceClock();assert(rtc.writes==0);
 WiFi.state=3;serviceClock();assert(rtc.writes==1&&clockReady&&clockSynced);
 serviceClock();assert(rtc.writes==1);
}
