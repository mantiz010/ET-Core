#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

constexpr size_t MAX_MEDIA_TEXT = 640;
enum class Operation : uint8_t { PLAY, PAUSE, STOP, VOLUME, POWER, URL, TTS };
struct MediaCommand {
  Operation op;
  float volume;
  bool on;
  char text[MAX_MEDIA_TEXT + 1];
  char language[17];
};
inline bool mediaHttpUrl(const char* value) {
  if (!value || !(strncmp(value,"http://",7)==0 || strncmp(value,"https://",8)==0)) return false;
  const char* host = strstr(value, "://") + 3;
  if (!*host || *host == '/' || *host=='?' || *host=='#') return false;
  for (const char* p=value; *p; p++) if ((uint8_t)*p < 32 || (uint8_t)*p == 127) return false;
  const char* end = strpbrk(host,"/?#");
  for (const char* p=host; *p && (!end || p<end); p++) if (*p=='@') return false;
  return strlen(value) <= MAX_MEDIA_TEXT;
}
inline bool mediaLanguage(const char* value) {
  if (!value || !*value || strlen(value)>16) return false;
  bool letter=false;
  for (const char* p=value; *p; p++) {
    const bool alpha=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z');
    if (!alpha && *p!='-') return false;
    letter=letter||alpha;
  }
  return letter;
}
inline bool parseMediaCommand(JsonObject payload, MediaCommand& out) {
  out=MediaCommand{};
  const char* op=payload["op"] | "";
  if (!strcmp(op,"play")) out.op=Operation::PLAY;
  else if (!strcmp(op,"pause")) out.op=Operation::PAUSE;
  else if (!strcmp(op,"stop")) out.op=Operation::STOP;
  else if (!strcmp(op,"power")) {
    if (!payload["on"].is<bool>()) return false;
    out.op=Operation::POWER;out.on=payload["on"].as<bool>();
  } else if (!strcmp(op,"volume")) {
    if (payload["volume"].is<bool>() || !(payload["volume"].is<float>() || payload["volume"].is<int>())) return false;
    out.op=Operation::VOLUME;out.volume=payload["volume"].as<float>();
    if (!std::isfinite(out.volume) || out.volume<0 || out.volume>1) return false;
  } else if (!strcmp(op,"play_media") || !strcmp(op,"tts")) {
    out.op=!strcmp(op,"tts") ? Operation::TTS : Operation::URL;
    const char* text=payload[out.op==Operation::TTS ? "text" : "url"] | "";
    if (!*text || strlen(text)>MAX_MEDIA_TEXT) return false;
    if (!payload["language"].isNull() && !payload["language"].is<const char*>()) return false;
    const char* language=payload["language"] | "en";
    if (out.op==Operation::TTS ? !mediaLanguage(language) : !mediaHttpUrl(text)) return false;
    memcpy(out.text,text,strlen(text)+1);
    // Language is only meaningful for speech; URL commands use the safe default.
    if (out.op==Operation::TTS) memcpy(out.language,language,strlen(language)+1);
    else memcpy(out.language,"en",3);
  } else return false;
  return true;
}
