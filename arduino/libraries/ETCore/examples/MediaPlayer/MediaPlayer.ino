/* ET-Core native audio player: ESP32-WROVER, MAX98357A, encrypted control + OTA.
 * Audio remains HTTP/HTTPS. See README.md for exact dependency and OTA partition.
 * GPIO 26=BCLK, 25=LRC, 22=DOUT. No SD card is mounted or required.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ETCore.h>
#include <Audio.h>
#include <esp_ota_ops.h>
#include <math.h>
#include <atomic>
#include "media_protocol.h"

// Configure these inside the sketch; never publish personal credentials or site keys.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* ETCORE_PSK_HEX = "YOUR_64_HEX_CHARACTER_ETCORE_SITE_KEY";
const char* OTA_PASSWORD = "CHANGE_TO_A_UNIQUE_OTA_PASSWORD";
const char* DEVICE_ID = "audio-example";
const char* DEVICE_NAME = "ET-Core Audio";
const char* OTA_HOSTNAME = "etcore-audio";
const char* FIRMWARE_VERSION = "media-0.1.0";
constexpr char DEVICE_CLASS[] = "media_player.audio";
constexpr int PIN_I2S_BCLK = 26, PIN_I2S_LRC = 25, PIN_I2S_DOUT = 22;

ETCore etcore;
Audio audio;
enum class Playback : uint8_t { OFF, IDLE, PLAYING, PAUSED, BUFFERING };
struct MediaState {
  Playback state;
  float volume;
  uint32_t position, duration;
  char url[MAX_MEDIA_TEXT + 1];
  char error[40];
};
QueueHandle_t commands = nullptr, reports = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
std::atomic<bool> otaInProgress{false}, audioStoppedForOta{false};
volatile uint8_t lastWifiReason = 0;
volatile bool wifiReasonPending = false;
bool transportStarted = false, otaStarted = false, encryptionReady = false;
MediaState lastReport{};
bool haveReport = false;

const char* playbackName(Playback s) {
  switch (s) {
    case Playback::OFF: return "off";
    case Playback::PLAYING: return "playing";
    case Playback::PAUSED: return "paused";
    case Playback::BUFFERING: return "buffering";
    default: return "idle";
  }
}
void publishState() {
  if (!transportStarted || !haveReport) return;
  JsonDocument document;
  JsonObject state = document.to<JsonObject>();
  state["state"] = playbackName(lastReport.state);
  state["volume"] = lastReport.volume;
  if (lastReport.state == Playback::PLAYING || lastReport.state == Playback::PAUSED) {
    state["position"] = lastReport.position;
    if (lastReport.duration) state["duration"] = lastReport.duration;
    else state["duration"] = nullptr;
  } else { state["position"] = nullptr; state["duration"] = nullptr; }
  if (lastReport.url[0]) state["url"] = lastReport.url;
  else state["url"] = nullptr;
  state["error"] = lastReport.error[0] ? lastReport.error : nullptr;
  etcore.sendState(state);
}
void hello(JsonObject payload) {
  payload["model"] = "ESP32-WROVER I2S Audio";
  JsonArray caps = payload["media_capabilities"].to<JsonArray>();
  for (const char* item : {"play_media","play","pause","stop","volume_set","power","tts"}) caps.add(item);
  JsonObject ota = payload["ota"].to<JsonObject>();
  ota["type"] = "arduino";
  ota["ready"] = otaStarted;
  ota["hostname"] = OTA_HOSTNAME;
  ota["port"] = 3232;
  ota["password_protected"] = true;
}
void commandReceived(const char* cls, JsonObject payload) {
  // This callback validates and queues only; audio/network decoding has one owner task.
  const char* id = payload["cmd_id"] | "";
  if (strcmp(cls, DEVICE_CLASS) != 0 || otaInProgress || !commands) { etcore.sendAck(id,false); return; }
  MediaCommand command{};
  const bool valid=parseMediaCommand(payload,command);
  const bool accepted = valid && xQueueSend(commands,&command,0) == pdTRUE;
  etcore.sendAck(id,accepted); // Acceptance is not playback success: state follows execution.
}
void audioTask(void*) {
  MediaState state{}; state.state = Playback::IDLE; state.volume = 0.30f;
  audio.setPinout(PIN_I2S_BCLK,PIN_I2S_LRC,PIN_I2S_DOUT);
  audio.setVolume(6); // Original player's initial 30% on its 0..21 hardware scale.
  uint32_t lastReportAt = 0;
  for (;;) {
    if (otaInProgress) {
      if (!audioStoppedForOta) { audio.stopSong(); state.state=Playback::IDLE; audioStoppedForOta=true; xQueueOverwrite(reports,&state); }
      vTaskDelay(pdMS_TO_TICKS(10)); continue;
    }
    audioStoppedForOta = false;
    MediaCommand command{};
    bool changed = false;
    if (xQueueReceive(commands,&command,0)==pdTRUE) {
      state.error[0]=0; changed=true;
      switch(command.op) {
        case Operation::VOLUME:
          state.volume=roundf(command.volume*100.0f)/100.0f;
          audio.setVolume((uint8_t)roundf(state.volume*21.0f)); break;
        case Operation::POWER:
          if (!command.on) audio.stopSong();
          if (!command.on) state.state=Playback::OFF;
          else if (state.state==Playback::OFF) state.state=Playback::IDLE;
          break;
        case Operation::STOP: audio.stopSong(); state.state=Playback::IDLE; break;
        case Operation::PAUSE:
          if (audio.isRunning()) { audio.pauseResume(); state.state=Playback::PAUSED; } break;
        case Operation::PLAY:
          if (state.state==Playback::PAUSED) {
            state.state=audio.pauseResume() ? Playback::PLAYING : Playback::IDLE;
          } else if (!audio.isRunning() && state.url[0]) {
            state.state=Playback::BUFFERING; xQueueOverwrite(reports,&state);
            const bool ok=audio.connecttohost(state.url);
            state.state=ok ? Playback::PLAYING : Playback::IDLE;
            if (!ok) strlcpy(state.error,"stream_connect_failed",sizeof state.error);
          } else if (state.state==Playback::OFF) state.state=Playback::IDLE;
          break;
        case Operation::URL: case Operation::TTS: {
          audio.stopSong(); state.state=Playback::BUFFERING; xQueueOverwrite(reports,&state);
          const bool speech=command.op==Operation::TTS;
          if (speech) state.url[0]=0; else strlcpy(state.url,command.text,sizeof state.url);
          const bool ok=speech ? audio.connecttospeech(command.text,command.language) : audio.connecttohost(command.text);
          state.state=ok ? Playback::PLAYING : Playback::IDLE;
          if (!ok) strlcpy(state.error,speech ? "tts_connect_failed" : "stream_connect_failed",sizeof state.error);
          break;
        }
      }
    }
    audio.loop();
    if (state.state==Playback::PLAYING && !audio.isRunning()) { state.state=Playback::IDLE; changed=true; }
    state.position=audio.getAudioCurrentTime(); state.duration=audio.getAudioFileDuration();
    const uint32_t now=millis();
    const uint32_t interval=state.state==Playback::PLAYING ? 1000 : 10000;
    if (changed || (uint32_t)(now-lastReportAt)>=interval) { xQueueOverwrite(reports,&state); lastReportAt=now; }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
void startOta() {
  if (otaStarted || WiFi.status()!=WL_CONNECTED) return;
  if (!strcmp(OTA_PASSWORD,"CHANGE_TO_A_UNIQUE_OTA_PASSWORD") || strlen(OTA_PASSWORD)<12) {
    Serial.println("[OTA] Not started: configure a unique password of at least 12 characters"); return;
  }
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next) { Serial.println("[OTA] Not started: first upload needs the dual-slot OTA partition layout"); return; }
  ArduinoOTA.setHostname(OTA_HOSTNAME); ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([](){
    otaInProgress=true; audioStoppedForOta=false; etcore.sendBye();
    MediaCommand discarded{};
    for (int i=0;i<4;i++) if (xQueueReceive(commands,&discarded,0)!=pdTRUE) break;
    const uint32_t begin=millis();
    while (!audioStoppedForOta && (uint32_t)(millis()-begin)<200) delay(5);
    Serial.println("[OTA] Update started");
  });
  ArduinoOTA.onEnd([](){ Serial.println("[OTA] Update finished"); });
  ArduinoOTA.onError([](ota_error_t error){ otaInProgress=false; Serial.printf("[OTA] Error=%u\n",(unsigned)error); });
  ArduinoOTA.begin(); otaStarted=true;
  Serial.printf("[OTA] Ready: %s.local:3232 (password protected), next slot=%lu bytes\n",OTA_HOSTNAME,(unsigned long)next->size);
}
void wifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) { lastWifiReason=info.wifi_sta_disconnected.reason; wifiReasonPending=true; }
}
void setup() {
  Serial.begin(115200); delay(200);
  Serial.println("ET-Core WROVER native media + OTA starting");
  Serial.printf("[BOARD] Flash=%lu PSRAM=%s\n",(unsigned long)ESP.getFlashChipSize(),psramFound()?"present":"missing");
  if (!psramFound()) { Serial.println("[AUDIO] PSRAM is required; audio player not started"); return; }
  commands=xQueueCreate(4,sizeof(MediaCommand)); reports=xQueueCreate(1,sizeof(MediaState));
  if (!commands || !reports) { Serial.println("[AUDIO] Queue allocation failed"); return; }
  if (xTaskCreatePinnedToCore(audioTask,"etcore-audio",16384,nullptr,1,&audioTaskHandle,1)!=pdPASS) {
    Serial.println("[AUDIO] Task allocation failed"); return;
  }
  encryptionReady=etcore.enableEncryptionHex(ETCORE_PSK_HEX);
  if (!encryptionReady) Serial.println("[ET-Core] Invalid encryption key; transport is disabled");
  etcore.onHello(hello); etcore.onCommand(commandReceived); etcore.onSync(publishState);
  WiFi.onEvent(wifiEvent); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  Serial.printf("[WiFi] Joining %s; MAC=%s\n",WIFI_SSID,WiFi.macAddress().c_str());
}
void loop() {
  static uint32_t lastRetry=0,lastStatus=0;
  const uint32_t now=millis();
  if (wifiReasonPending) { wifiReasonPending=false; Serial.printf("[WiFi] Disconnected reason=%u\n",lastWifiReason); }
  if (reports && xQueueReceive(reports,&lastReport,0)==pdTRUE) { haveReport=true; publishState(); }
  if (WiFi.status()==WL_CONNECTED) {
    if (!transportStarted && encryptionReady && audioTaskHandle) {
      etcore.begin(DEVICE_ID,DEVICE_CLASS,DEVICE_NAME,FIRMWARE_VERSION); transportStarted=true;
      Serial.printf("[WiFi] IP=%s RSSI=%d\n",WiFi.localIP().toString().c_str(),WiFi.RSSI()); publishState();
    }
    if (transportStarted) etcore.loop();
    if (encryptionReady && !otaStarted && (uint32_t)(now-lastStatus)>=5000) startOta();
    if (otaStarted) ArduinoOTA.handle();
  } else if ((uint32_t)(now-lastRetry)>=30000) { lastRetry=now; WiFi.reconnect(); }
  if ((uint32_t)(now-lastStatus)>=5000) {
    lastStatus=now;
    Serial.printf("[HEALTH] WiFi=%d IP=%s RSSI=%d ET-Core=%s OTA=%s\n",(int)WiFi.status(),WiFi.localIP().toString().c_str(),WiFi.RSSI(),transportStarted?"started":"not started",otaStarted?"ready":"not ready");
  }
  delay(2);
}
