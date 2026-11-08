#include "../../arduino/libraries/ETCore/examples/SMSGateway/SmsValidation.h"
#include <cassert>
#include <string>
int main(){using namespace SmsTransport;
 assert(reservationSpace(0,0,4));assert(!reservationSpace(16,0,4));assert(!reservationSpace(0,16,4));assert(!reservationSpace(10,0,0));
 assert(validId("01234567-89ab-cdef-0123-456789abcdef"));assert(!validId("0123"));assert(!validId(nullptr));
 assert(validBody("hello"));assert(validBody(std::string(160,'a').c_str()));assert(!validBody(std::string(161,'a').c_str()));
 assert(validBody(std::string(80,'^').c_str()));assert(!validBody(std::string(81,'^').c_str()));
 assert(!validBody("\x1a"));assert(!validBody("`"));assert(!validBody(""));assert(!validBody(nullptr));
 assert(validNumber("+640000000"));assert(validNumber(""));assert(!validNumber("+"));assert(!validNumber("123\""));
 assert(!validNumber(std::string(32,'1').c_str()));assert(!validNumber(nullptr));
 for(unsigned i=0;i<256;++i){char text[2]={char(i),0};if(i==0 || i<32 || i>126 || i==96)assert(!validBody(text));}
}
