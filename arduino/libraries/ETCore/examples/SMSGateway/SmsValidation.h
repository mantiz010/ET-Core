#pragma once
#include <cstring>
#include <cctype>
namespace SmsTransport {
inline bool validId(const char* s) {
  if(!s || strlen(s)!=36)return false;
  for(size_t i=0;i<36;++i)if((i==8 || i==13 || i==18 || i==23) ? s[i]!='-' : !isxdigit((unsigned char)s[i]))return false;
  return true;
}
inline bool validBody(const char* s) {
  if(!s || !*s || strlen(s)>160)return false;
  unsigned units=0;
  for(const char* c=s;*c;++c) {
    if((unsigned char)*c<32 || (unsigned char)*c>126 || *c=='`')return false;
    units+=strchr("^{}[]~|\\",*c)?2:1;
  }
  return units<=160;
}
inline bool validNumber(const char* s) {
  if(!s || strlen(s)>31)return false;
  if(!*s)return true;
  if(*s=='+')++s;
  return *s && strspn(s,"0123456789")==strlen(s);
}
}
