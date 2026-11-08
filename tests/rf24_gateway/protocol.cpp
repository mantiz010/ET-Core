#include "../../arduino/libraries/ETCore/examples/RF24Gateway/Rf24Protocol.h"
#include <cassert>
#include <random>
#include <limits>
using namespace Rf24Protocol;
int main(){Sample out={};
 Node1 a={22,50,1013,1,4,500};assert(decode(01,&a,sizeof(a),out));assert(out.first==0 && out.count==6 && out.values[5]==500);
 Node2 b={2,123};assert(decode(02,&b,sizeof(b),out) && out.first==6);
 Node3 c={21,40,450,2};assert(decode(03,&c,sizeof(c),out) && out.first==7);
 Node4 d={20,55,1010,600,7,2};assert(decode(04,&d,sizeof(d),out) && out.first==11 && out.values[3]==600);
 Node5 e={5,100,3.8};assert(decode(05,&e,sizeof(e),out) && out.first==17);assert(decode(033,&e,sizeof(e),out) && out.first==19);
 Node7 f={7,18,80};assert(decode(034,&f,sizeof(f),out) && out.first==21);
 Node8 g={22,60,23,1009,4.0};assert(decode(011,&g,sizeof(g),out) && out.first==23);
 Node9 h={19,61,1007,25000,3.9};assert(decode(013,&h,sizeof(h),out) && out.first==28 && out.count==5);
 for(unsigned n=0;n<sizeof(a)+2;++n)if(n!=sizeof(a))assert(!decode(01,&a,n,out));
 Sample previous=out;a.humidity=101;assert(!decode(01,&a,sizeof(a),out));assert(out.first==previous.first && out.values[0]==previous.values[0]);
 a.humidity=50;a.temperature=std::numeric_limits<float>::quiet_NaN();assert(!decode(01,&a,sizeof(a),out));
 assert(!decode(077,&h,sizeof(h),out));assert(!decode(01,nullptr,18,out));
 std::mt19937 random(7);uint8_t bytes[40];for(unsigned i=0;i<100000;++i){for(auto& x:bytes)x=uint8_t(random());decode(uint16_t(random()%64),bytes,random()%41,out);}
}
