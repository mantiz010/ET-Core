// ET-Core WS2812 / NeoPixel strip with 52 effects (class light.rgb).
//
// Home Assistant shows a colour light with brightness, an Effect list and an
// "Effect speed" slider. Every ET-Bus effect is here (same names, so automations
// keep working) plus 17 more. The last state is saved, so the strip comes back
// the way it was after a power cut.
//
// Needs the FastLED library (Library Manager: "FastLED").
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <FastLED.h>
#include <ETCore.h>

// -----------------------------
// WIFI + ET-CORE KEY (same key as in Home Assistant)
// -----------------------------
static const char* WIFI_SSID  = "YOUR_WIFI_SSID";
static const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
static const char* ETCORE_KEY = "";  // 64 hex characters
static const char* OTA_PASSWORD = "";  // password for WiFi uploads ("" = none)

// -----------------------------
// DEVICE
// -----------------------------
static const char* DEVICE_ID   = "ws2812_1";       // unique on your network
static const char* DEVICE_NAME = "WS2812 Strip";
static const char* FIRMWARE    = "ws2812-2.1";

// -----------------------------
// STRIP
// -----------------------------
#define LED_PIN      4          // data pin
#define NUM_LEDS     16         // number of LEDs
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
// #define MAX_MILLIAMPS 900    // uncomment to limit power on USB supplies

CRGB leds[NUM_LEDS];
ETCore etcore;
Preferences prefs;

// -----------------------------
// STATE (saved across power cuts)
// -----------------------------
static bool    ledOn = true;
static uint8_t ledR = 255, ledG = 140, ledB = 40;
static uint8_t ledBrightness = 180;
static uint8_t ledSpeed = 120;           // 1..255, higher = faster
static uint8_t fxIndex = 0;              // into EFFECTS[]

// Animation state
static uint8_t  gHue = 0;
static uint16_t step = 0;
static uint8_t  heat[NUM_LEDS];          // fire
static uint32_t lastFrameMs = 0;

static inline CRGB base() { return CRGB(ledR, ledG, ledB); }
static inline uint8_t bpm(uint8_t div) { return max<uint8_t>(1, ledSpeed / div); }
// Effects that change the brightness themselves stay below the user's setting.
static inline void scaleBrightness(uint8_t v) { FastLED.setBrightness(scale8(v, ledBrightness)); }

// =====================================================================
// EFFECTS. Each draws one frame. The first 35 are the ET-Bus set.
// =====================================================================

// ---- solid & white ----
static void fxSolid()      { fill_solid(leds, NUM_LEDS, base()); }
static void fxWhite()      { fill_solid(leds, NUM_LEDS, CRGB::White); }
static void fxWarmWhite()  { fill_solid(leds, NUM_LEDS, CRGB(255, 147, 41)); }
static void fxCoolWhite()  { fill_solid(leds, NUM_LEDS, CRGB(201, 226, 255)); }
static void fxNightLight() { fill_solid(leds, NUM_LEDS, CRGB(255, 100, 20)); scaleBrightness(50); }

// ---- rainbow & colour ----
static void fxRainbow() { fill_rainbow(leds, NUM_LEDS, gHue++, 7); }
static void fxRainbowCycle() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(gHue + (i * 255 / NUM_LEDS), 255, 255);
  gHue += 2;
}
static void fxColorWipe() {
  if (step % (NUM_LEDS * 4) == 0) fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[(step / 4) % NUM_LEDS] = base();
  step++;
}
static void fxColorChase() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[step++ % NUM_LEDS] = base();
}
static void fxColorBounce() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[beatsin16(bpm(2), 0, NUM_LEDS - 1)] = base();
}

// ---- animated patterns ----
static void fxCylon() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  leds[beatsin16(bpm(2), 0, NUM_LEDS - 1)] = CRGB::Red;
}
static void fxScanner() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  leds[beatsin16(bpm(2), 0, NUM_LEDS - 1)] = base();
}
static void fxTheaterChase() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ((i + step) % 3 == 0) ? base() : CRGB::Black;
  step++;
}
static void fxTwinkle() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  if (random8() < 80) leds[random16(NUM_LEDS)] = base();
}
static void fxSparkle() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[random16(NUM_LEDS)] = CRGB::White;
}
static void fxConfetti() {
  fadeToBlackBy(leds, NUM_LEDS, 10);
  leds[random16(NUM_LEDS)] += CHSV(gHue++ + random8(64), 200, 255);
}
static void fxJuggle() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  for (int i = 0; i < 8; i++) leds[beatsin16(i + 7, 0, NUM_LEDS - 1)] |= CHSV(gHue + i * 32, 200, 255);
  gHue++;
}
static void fxPulse() {
  fill_solid(leds, NUM_LEDS, base());
  scaleBrightness(beatsin8(bpm(10), 50, 255));
}
static void fxHeartbeat() {
  uint16_t b = beat16(bpm(2));
  bool beat = b < 10000 || (b > 15000 && b < 25000);
  fill_solid(leds, NUM_LEDS, base());
  scaleBrightness(beat ? 255 : 50);
}
static void fxStrobe() { fill_solid(leds, NUM_LEDS, (step++ % 2) ? base() : CRGB::Black); }

// ---- running & chasing ----
static void fxRunningLights() {
  for (int i = 0; i < NUM_LEDS; i++) { leds[i] = base(); leds[i].nscale8(sin8((i + step) * 16)); }
  step++;
}
static void fxComet() {
  fadeToBlackBy(leds, NUM_LEDS, 50);
  leds[step++ % NUM_LEDS] = base();
}
static void fxMeteor() {
  fadeToBlackBy(leds, NUM_LEDS, 64);
  for (int i = 0; i < 5; i++) {
    int pos = (int)((uint32_t)(step + NUM_LEDS * 8 - i) % NUM_LEDS);
    leds[pos] = base();
    leds[pos].nscale8(255 - i * 50);
  }
  step++;
}
static void fxChase() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i = 0; i < 3; i++) leds[(step + i * 3) % NUM_LEDS] = CHSV(gHue + i * 80, 255, 255);
  step++; gHue++;
}
// Fire2012 (Mark Kriegsman): real rising flames instead of random flicker.
static void fxFire() {
  for (int i = 0; i < NUM_LEDS; i++) heat[i] = qsub8(heat[i], random8(0, ((55 * 10) / NUM_LEDS) + 2));
  for (int k = NUM_LEDS - 1; k >= 2; k--) heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  if (random8() < 120) { int y = random8(min(7, NUM_LEDS)); heat[y] = qadd8(heat[y], random8(160, 255)); }
  for (int j = 0; j < NUM_LEDS; j++) leds[j] = HeatColor(heat[j]);
}

// ---- advanced ----
static void fxAurora() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(sin8(i * 10 + gHue) + 100, 200, 255);
  gHue++;
}
static void fxPlasma() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(sin8(i * 16 + gHue) + sin8(i * 8 + gHue * 2), 255, 255);
  gHue++;
}
static void fxPride() {
  static uint16_t pseudotime = 0, lastMs = 0, hue16 = 0;
  uint16_t now = (uint16_t)millis(), dt = now - lastMs;
  lastMs = now;
  pseudotime += dt * bpm(10);
  hue16 += dt * 8;
  uint16_t theta = pseudotime;
  for (int i = 0; i < NUM_LEDS; i++) {
    theta += 32;
    uint8_t bri = (uint8_t)(((uint32_t)(sin16(theta) + 32768) * 255UL) / 65535UL);
    leds[i] = CHSV((hue16 + i * 256) / 256, 255, bri);
  }
}
static void fxLava() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ColorFromPalette(LavaColors_p, sin8(i * 8 + gHue) / 2 + sin8(i * 4 + gHue * 3) / 2);
  gHue++;
}
static void fxOcean() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ColorFromPalette(OceanColors_p, sin8(i * 10 + gHue) / 2 + sin8(gHue * 2) / 2);
  gHue++;
}

// ---- seasonal & special ----
static void fxChristmas() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ((i + step / 8) % 2) ? CRGB::Red : CRGB::Green;
  step++;
}
static void fxHalloween() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ((i + step / 8) % 2) ? CRGB(255, 60, 0) : CRGB(110, 0, 160);
  step++;
}
static void fxPolice() {
  bool flip = (step++ / 6) % 2;
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ((i < NUM_LEDS / 2) != flip) ? CRGB::Red : CRGB::Blue;
}
static void fxAmbulance() { fill_solid(leds, NUM_LEDS, (step++ / 3 % 2) ? CRGB::Red : CRGB::White); }
static void fxParty() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ColorFromPalette(PartyColors_p, gHue + i * 8);
  gHue += 2;
}

// =====================================================================
// MORE EFFECTS (not in ET-Bus)
// =====================================================================
static void fxCandle() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(255, 120 + random8(30), 20).nscale8(160 + random8(96));
}
static void fxLightning() {
  static uint32_t next = 0;
  fadeToBlackBy(leds, NUM_LEDS, 80);
  if (millis() > next) {
    int start = random16(NUM_LEDS), len = 1 + random16(max(1, NUM_LEDS / 2));
    for (int i = start; i < min(NUM_LEDS, start + len); i++) leds[i] = CRGB(220, 220, 255);
    next = millis() + random16(80, 2000 - ledSpeed * 6);
  }
}
static void fxFireworks() {
  static int16_t pos = -1; static uint8_t life = 0, hue = 0;
  fadeToBlackBy(leds, NUM_LEDS, 40);
  if (life == 0 && random8() < 30) { pos = random16(NUM_LEDS); life = 12; hue = random8(); }
  if (life) {
    uint8_t r = 12 - life;
    if (pos - r >= 0) leds[pos - r] += CHSV(hue, 200, life * 20);
    if (pos + r < NUM_LEDS) leds[pos + r] += CHSV(hue, 200, life * 20);
    life--;
  }
}
static void fxBpm() {
  uint8_t beat = beatsin8(bpm(2), 64, 255);
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ColorFromPalette(PartyColors_p, gHue + i * 2, beat - gHue + i * 10);
  gHue++;
}
static void fxSinelon() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  leds[beatsin16(bpm(4), 0, NUM_LEDS - 1)] += CHSV(gHue++, 255, 192);
}
static void fxClouds() {
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = ColorFromPalette(CloudColors_p, inoise8(i * 40, step * 4));
  step++;
}
static void fxGradient() {
  CHSV a = rgb2hsv_approximate(base()), b = a;
  b.hue += 96;
  fill_gradient(leds, NUM_LEDS, a, b, SHORTEST_HUES);
}
static void fxColorLoop() { fill_solid(leds, NUM_LEDS, CHSV(gHue++, 255, 255)); }
static void fxDualScan() {
  fadeToBlackBy(leds, NUM_LEDS, 30);
  int p = beatsin16(bpm(2), 0, NUM_LEDS - 1);
  leds[p] = base();
  leds[NUM_LEDS - 1 - p] = base();
}
static void fxRain() {
  for (int i = NUM_LEDS - 1; i > 0; i--) leds[i] = leds[i - 1];
  leds[0] = random8() < 40 ? CRGB(60, 120, 255) : CRGB::Black;
  leds[0].nscale8(200);
}
static void fxBouncingBalls() {
  static float h[3] = {0, 0, 0}, v[3] = {0, 0, 0};
  static uint32_t last = 0;
  float dt = min(0.05f, (millis() - last) / 1000.0f);
  last = millis();
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int b = 0; b < 3; b++) {
    v[b] -= 9.81f * dt;
    h[b] += v[b] * dt;
    if (h[b] <= 0) { h[b] = 0; v[b] = 4.0f + b * 0.9f; }
    int pos = constrain((int)(h[b] / 1.3f * (NUM_LEDS - 1)), 0, NUM_LEDS - 1);
    leds[pos] += CHSV(gHue + b * 85, 255, 255);
  }
}
static void fxSunrise() {
  // ~2 minutes from deep red to warm white at medium speed, then holds.
  static uint16_t level = 0;
  if (step == 0) level = 0;
  if (level < 1000) level += max<uint8_t>(1, ledSpeed / 24);
  step = 1;
  uint8_t t = level * 255UL / 1000;
  fill_solid(leds, NUM_LEDS, ColorFromPalette(HeatColors_p, scale8(t, 240)));
  scaleBrightness(max<uint8_t>(10, t));
}
static void fxRainbowGlitter() {
  fill_rainbow(leds, NUM_LEDS, gHue++, 7);
  if (random8() < 80) leds[random16(NUM_LEDS)] += CRGB::White;
}
static void fxRipple() {
  static int center = 0; static uint8_t r = 255, hue = 0;
  fadeToBlackBy(leds, NUM_LEDS, 40);
  if (r > NUM_LEDS) { center = random16(NUM_LEDS); r = 0; hue = random8(); }
  uint8_t v = 255 - min(255, r * 255 / max(1, NUM_LEDS / 2));
  if (center + r < NUM_LEDS) leds[center + r] = CHSV(hue, 255, v);
  if (center - r >= 0) leds[center - r] = CHSV(hue, 255, v);
  r++;
}
static void fxMatrix() {
  for (int i = NUM_LEDS - 1; i > 0; i--) leds[i] = leds[i - 1];
  leds[0] = random8() < 50 ? CRGB(0, 255, 40) : CRGB(leds[1]).nscale8(150);
}
static void fxBreatheRainbow() {
  fill_solid(leds, NUM_LEDS, CHSV(gHue, 255, 255));
  scaleBrightness(beatsin8(bpm(10), 30, 255));
  if (step++ % 4 == 0) gHue++;
}
static void fxWave() {
  for (int i = 0; i < NUM_LEDS; i++) { leds[i] = base(); leds[i].nscale8(cubicwave8(i * 24 + step * 4)); }
  step++;
}

// =====================================================================
// EFFECT TABLE: names are what Home Assistant shows (ET-Bus names first).
// =====================================================================
struct Effect { const char* name; void (*draw)(); bool animated; };
static const Effect EFFECTS[] = {
  {"solid", fxSolid, false}, {"white", fxWhite, false}, {"warm_white", fxWarmWhite, false},
  {"cool_white", fxCoolWhite, false}, {"night_light", fxNightLight, false},
  {"rainbow", fxRainbow, true}, {"rainbow_cycle", fxRainbowCycle, true}, {"color_wipe", fxColorWipe, true},
  {"color_chase", fxColorChase, true}, {"color_bounce", fxColorBounce, true},
  {"cylon", fxCylon, true}, {"scanner", fxScanner, true}, {"theater_chase", fxTheaterChase, true},
  {"twinkle", fxTwinkle, true}, {"sparkle", fxSparkle, true}, {"confetti", fxConfetti, true},
  {"juggle", fxJuggle, true}, {"pulse", fxPulse, true}, {"heartbeat", fxHeartbeat, true}, {"strobe", fxStrobe, true},
  {"running_lights", fxRunningLights, true}, {"comet", fxComet, true}, {"meteor", fxMeteor, true},
  {"chase", fxChase, true}, {"fire", fxFire, true},
  {"aurora", fxAurora, true}, {"plasma", fxPlasma, true}, {"pride", fxPride, true}, {"lava", fxLava, true},
  {"ocean", fxOcean, true},
  {"christmas", fxChristmas, true}, {"halloween", fxHalloween, true}, {"police", fxPolice, true},
  {"ambulance", fxAmbulance, true}, {"party", fxParty, true},
  // more than ET-Bus
  {"candle", fxCandle, true}, {"lightning", fxLightning, true}, {"fireworks", fxFireworks, true},
  {"bpm", fxBpm, true}, {"sinelon", fxSinelon, true}, {"clouds", fxClouds, true},
  {"gradient", fxGradient, false}, {"color_loop", fxColorLoop, true}, {"dual_scan", fxDualScan, true},
  {"rain", fxRain, true}, {"bouncing_balls", fxBouncingBalls, true}, {"sunrise", fxSunrise, true},
  {"rainbow_glitter", fxRainbowGlitter, true}, {"ripple", fxRipple, true}, {"matrix", fxMatrix, true},
  {"breathe_rainbow", fxBreatheRainbow, true}, {"wave", fxWave, true},
};
static const uint8_t EFFECT_COUNT = sizeof(EFFECTS) / sizeof(EFFECTS[0]);

static int findEffect(const char* name) {
  for (uint8_t i = 0; i < EFFECT_COUNT; i++) if (strcmp(EFFECTS[i].name, name) == 0) return i;
  return -1;
}

// =====================================================================
// ENGINE
// =====================================================================
static uint16_t frameMs() { return map(ledSpeed, 1, 255, 120, 10); }

static void drawFrame() {
  if (!ledOn) {
    FastLED.clear();
    FastLED.show();
    return;
  }
  FastLED.setBrightness(ledBrightness);
  EFFECTS[fxIndex].draw();
  FastLED.show();
}

static void serviceLeds() {
  uint32_t now = millis();
  // Still effects only need a redraw now and then; animated ones every frame.
  uint16_t every = (ledOn && EFFECTS[fxIndex].animated) ? frameMs() : 500;
  if (now - lastFrameMs < every) return;
  lastFrameMs = now;
  drawFrame();
}

// =====================================================================
// SAVE / RESTORE (debounced: a slider drag is one flash write, not fifty)
// =====================================================================
static uint32_t saveAt = 0;
static void saveSoon() { saveAt = millis() + 2000; if (!saveAt) saveAt = 1; }
static void serviceSave() {
  if (!saveAt || (int32_t)(millis() - saveAt) < 0) return;
  saveAt = 0;
  prefs.putBool("on", ledOn);
  prefs.putUChar("r", ledR); prefs.putUChar("g", ledG); prefs.putUChar("b", ledB);
  prefs.putUChar("bri", ledBrightness);
  prefs.putUChar("speed", ledSpeed);
  prefs.putString("fx", EFFECTS[fxIndex].name);
  Serial.println("[STATE] saved");
}
static void restoreState() {
  ledOn = prefs.getBool("on", ledOn);
  ledR = prefs.getUChar("r", ledR); ledG = prefs.getUChar("g", ledG); ledB = prefs.getUChar("b", ledB);
  ledBrightness = prefs.getUChar("bri", ledBrightness);
  ledSpeed = prefs.getUChar("speed", ledSpeed);
  int i = findEffect(prefs.getString("fx", "solid").c_str());
  fxIndex = i < 0 ? 0 : i;
}

// =====================================================================
// ET-CORE
// =====================================================================
static void publishState() {
  etcore.sendRgbStateFx(ledOn, ledR, ledG, ledB, ledBrightness, EFFECTS[fxIndex].name, ledSpeed);
}

// The hello lists our effects: Home Assistant offers exactly these.
static void onHello(JsonObject hello) {
  JsonArray fx = hello["effects"].to<JsonArray>();
  for (uint8_t i = 0; i < EFFECT_COUNT; i++) fx.add(EFFECTS[i].name);
}

static void onCommand(const char* cls, JsonObject p) {
  if (strcmp(cls, "light.rgb") != 0) return;
  if (p["on"].is<bool>()) ledOn = p["on"];
  if (p["r"].is<int>()) ledR = constrain((int)p["r"], 0, 255);
  if (p["g"].is<int>()) ledG = constrain((int)p["g"], 0, 255);
  if (p["b"].is<int>()) ledB = constrain((int)p["b"], 0, 255);
  if (p["brightness"].is<int>()) ledBrightness = constrain((int)p["brightness"], 1, 255);
  if (p["speed"].is<int>()) ledSpeed = constrain((int)p["speed"], 1, 255);
  if (p["effect"].is<const char*>()) {
    int i = findEffect(p["effect"]);
    if (i >= 0 && i != fxIndex) { fxIndex = i; step = 0; }
  }
  lastFrameMs = 0;   // draw the change now
  serviceLeds();
  publishState();    // the library sends the ack for us
  saveSoon();
}

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] connecting to %s", WIFI_SSID);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    serviceLeds();   // the strip keeps running while WiFi connects
    delay(10);
    if (millis() - start > 20000) { Serial.println("\n[WIFI] timeout, restarting"); ESP.restart(); }
  }
  Serial.printf("\n[WIFI] connected, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== ET-Core WS2812, %d effects ===\n", EFFECT_COUNT);

  prefs.begin("etcore-ws", false);
  restoreState();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
#ifdef MAX_MILLIAMPS
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
#endif
  FastLED.clear(true);
  drawFrame();

  connectWiFi();

  if (strlen(ETCORE_KEY) == 64) etcore.enableEncryptionHex(ETCORE_KEY);
  else Serial.println("[ETCORE] WARNING: no 64-character key set; if Home Assistant has a key it will ignore this device");
  etcore.onHello(onHello);
  etcore.onCommand(onCommand);
  etcore.onSync(publishState);
  etcore.begin(DEVICE_ID, "light.rgb", DEVICE_NAME, FIRMWARE);
  publishState();

  // WiFi uploads: in the Arduino IDE pick Tools > Port > the network port named
  // after DEVICE_ID, then Upload as usual.
  ArduinoOTA.setHostname(DEVICE_ID);
  if (OTA_PASSWORD[0]) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { FastLED.clear(true); Serial.println("[OTA] update starting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] failed (%u)\n", e); });
  ArduinoOTA.begin();
  Serial.printf("[OTA] ready: %s.local\n", DEVICE_ID);
}

void loop() {
  ArduinoOTA.handle();
  etcore.loop();
  serviceLeds();
  serviceSave();

  static uint32_t lastWifi = 0;
  if (millis() - lastWifi > 5000) {
    lastWifi = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WIFI] lost, reconnecting");
      connectWiFi();
      publishState();
    }
  }
}
