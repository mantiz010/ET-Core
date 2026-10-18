// One simulated ET-Core device running the real ETCore library on Linux.
// Usage: device_sim <id> <class> <127.0.0.x> [psk_hex|-] [lease_ms] [ttl_s]
// Prints one line per event on stdout so tests can follow it:
//   READY <id>            after begin()
//   CMD <payload json>    for every command the library hands to the sketch
#include <csignal>
#include <ETCore.h>

WiFiStub WiFi;
char g_sim_local_ip[16] = "127.0.0.2";

static ETCore core;
static const char* g_class = "";
static bool g_on = false;
static volatile bool g_run = true;
static bool g_noack = false;  // ETCORE_SIM_NOACK=1: sketch never calls sendAck

// ETCORE_SIM_RICH=1: behave like the example sketches for the class (light.rgb,
// fan.speed, fan.preset, sensor.*, switch.multi), so a Home Assistant panel or
// entity test can run against every device type. Without it: plain {"on"}.
static bool g_rich = false;
static uint8_t g_r = 255, g_g = 120, g_b = 0, g_bri = 180, g_speed = 128, g_pct = 50;
static char g_effect[32] = "Solid";
static char g_preset[16] = "low";
static bool g_sw[3] = {false, false, false};
static float g_value = 21.5f;
static bool is(const char* prefix) { return strncmp(g_class, prefix, strlen(prefix)) == 0; }

static void publish() {
  if (g_rich && strcmp(g_class, "light.rgb") == 0) {
    core.sendRgbStateFx(g_on, g_r, g_g, g_b, g_bri, g_effect, g_speed);
  } else if (g_rich && strcmp(g_class, "fan.speed") == 0) {
    core.sendFanSpeed(g_on, g_pct);
  } else if (g_rich && strcmp(g_class, "fan.preset") == 0) {
    core.sendFanPreset(g_on, g_preset);
  } else if (g_rich && strcmp(g_class, "switch.multi") == 0) {
    core.sendSwitchesState(g_sw, 3);
  } else if (g_rich && is("sensor.")) {
    core.sendValue(g_value, strstr(g_class, "humid") ? "%" : "°C");
  } else {
    JsonDocument d;
    JsonObject p = d.to<JsonObject>();
    p["on"] = g_on;
    core.sendState(p);
  }
}

static void onHello(JsonObject p) {
  if (!g_rich) return;
  if (strcmp(g_class, "fan.preset") == 0) {
    JsonArray a = p["presets"].to<JsonArray>();
    a.add("low"); a.add("medium"); a.add("high");
  }
  if (strcmp(g_class, "switch.multi") == 0) {
    JsonArray a = p["switches"].to<JsonArray>();
    const char* names[3] = {"Pump", "Lamp", "Heater"};
    for (int i = 0; i < 3; i++) {
      JsonObject o = a.add<JsonObject>();
      char sid[4]; snprintf(sid, sizeof sid, "%d", i + 1);
      o["id"] = sid;
      o["name"] = names[i];
    }
  }
}

static void applyRich(JsonObject p) {
  if (p["brightness"].is<int>()) g_bri = p["brightness"];
  if (p["r"].is<int>() && p["g"].is<int>() && p["b"].is<int>()) { g_r = p["r"]; g_g = p["g"]; g_b = p["b"]; }
  if (p["effect"].is<const char*>()) snprintf(g_effect, sizeof g_effect, "%s", p["effect"].as<const char*>());
  if (p["speed"].is<int>()) {
    if (is("fan.")) g_pct = p["speed"]; else g_speed = p["speed"];
  }
  if (p["preset"].is<const char*>()) snprintf(g_preset, sizeof g_preset, "%s", p["preset"].as<const char*>());
  if (p["switch_id"].is<const char*>()) {
    int i = atoi(p["switch_id"]) - 1;
    if (i >= 0 && i < 3 && p["on"].is<bool>()) g_sw[i] = p["on"];
  }
}

static void onCommand(const char* cls, JsonObject payload) {
  char buf[512];
  serializeJson(payload, buf, sizeof buf);
  printf("CMD %s\n", buf);
  fflush(stdout);
  if (strcmp(cls, g_class) != 0) return;
  if (payload["on"].is<bool>() && !payload["switch_id"].is<const char*>()) g_on = payload["on"].as<bool>();
  if (g_rich) applyRich(payload);
  if (!g_noack) core.sendAck(payload["cmd_id"] | "", true);
  publish();
}

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: device_sim id class 127.0.0.x [psk|-] [lease_ms] [ttl]\n"); return 2; }
  signal(SIGTERM, [](int) { g_run = false; });
  signal(SIGINT, [](int) { g_run = false; });
  g_noack = getenv("ETCORE_SIM_NOACK") != nullptr;
  const char* id = argv[1];
  g_class = argv[2];
  snprintf(g_sim_local_ip, sizeof g_sim_local_ip, "%s", argv[3]);
  if (argc > 4 && strcmp(argv[4], "-") != 0) core.enableEncryptionHex(argv[4]);
  if (argc > 5) core.setLeaseInterval((uint32_t)atoi(argv[5]));
  if (argc > 6) core.setLeaseTtl((uint16_t)atoi(argv[6]));
  if (getenv("ETCORE_SIM_HUB_SILENT_MS")) core.setHubSilentTimeout((uint32_t)atoi(getenv("ETCORE_SIM_HUB_SILENT_MS")));
  if (getenv("ETCORE_SIM_SETPORT")) core.setPort((uint16_t)atoi(getenv("ETCORE_SIM_SETPORT")));
  core.begin(id, g_class, id, "sim-1");
  core.onCommand(onCommand);
  core.onSync(publish);
  g_rich = getenv("ETCORE_SIM_RICH") != nullptr;
  core.onHello(onHello);
  publish();
  printf("READY %s\n", id);
  bool sentError = false;
  fflush(stdout);
  uint32_t t0 = millis();
  while (g_run) {
    core.loop();
    // ETCORE_SIM_ERROR=1: report a device problem 1.5 s after start, every 0.3 s after
    if (getenv("ETCORE_SIM_ERROR") && millis() - t0 > 1500) {
      static uint32_t last = 0;
      if (!sentError || millis() - last > 300) {
        core.sendError("sensor", "CO2 sensor not found");
        sentError = true;
        last = millis();
      }
    }
    delay(2);
  }
  core.sendBye();  // SIGTERM = planned restart; kill -9 = pulled plug
  return 0;
}
