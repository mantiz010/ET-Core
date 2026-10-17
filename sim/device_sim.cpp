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

static void publish() {
  JsonDocument d;
  JsonObject p = d.to<JsonObject>();
  p["on"] = g_on;
  core.sendState(p);
}

static void onCommand(const char* cls, JsonObject payload) {
  char buf[512];
  serializeJson(payload, buf, sizeof buf);
  printf("CMD %s\n", buf);
  fflush(stdout);
  if (strcmp(cls, g_class) != 0) return;
  if (payload["on"].is<bool>()) g_on = payload["on"].as<bool>();
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
