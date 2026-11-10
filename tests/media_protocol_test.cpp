#include <cassert>
#include <cstdio>
#include <string>
#include "../arduino/libraries/ETCore/examples/MediaPlayer/media_protocol.h"
bool parse(const std::string& json) {
  JsonDocument d;assert(!deserializeJson(d,json));MediaCommand command{};
  return parseMediaCommand(d.as<JsonObject>(),command);
}
int main() {
  for (const char* op : {"play","pause","stop"}) assert(parse(std::string("{\"op\":\"")+op+"\"}"));
  assert(parse("{\"op\":\"power\",\"on\":false}"));
  assert(!parse("{\"op\":\"power\",\"on\":1}"));
  assert(!parse("{\"op\":\"power\"}"));
  for (const char* value : {"0","0.3","1"}) assert(parse(std::string("{\"op\":\"volume\",\"volume\":")+value+"}"));
  for (const char* value : {"-1","2","true","\"0.5\"","null"}) assert(!parse(std::string("{\"op\":\"volume\",\"volume\":")+value+"}"));
  assert(parse("{\"op\":\"play_media\",\"url\":\"http://audio.example/stream.mp3\"}"));
  assert(parse("{\"op\":\"play_media\",\"url\":\"https://audio.example/stream.mp3\"}"));
  for (const char* url : {"file:///test","http:///bad","http://?bad","http://user:pass@audio.example/a","https://a/\\r\\nx"}) assert(!parse(std::string("{\"op\":\"play_media\",\"url\":\"")+url+"\"}"));
  assert(parse("{\"op\":\"tts\",\"text\":\"Hello\",\"language\":\"en-NZ\"}"));
  assert(!parse("{\"op\":\"tts\",\"text\":\"Hello\",\"language\":\"-\"}"));
  assert(!parse("{\"op\":\"tts\",\"text\":\"Hello\",\"language\":true}"));
  assert(!parse("{\"op\":\"tts\",\"text\":\"\"}"));
  assert(!parse("{\"op\":\"reboot\"}"));
  assert(!parse("{}"));
  assert(parse(std::string("{\"op\":\"tts\",\"text\":\"")+std::string(640,'a')+"\"}"));
  assert(!parse(std::string("{\"op\":\"tts\",\"text\":\"")+std::string(641,'a')+"\"}"));
  puts("Native firmware media parser: all validation cases passed.");
}
