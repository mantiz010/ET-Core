// ============================================================
//  EC pH Controller 8.9  (BUCKET EC TEMP-FIX, ASCII ONLY)
//  - RAW EC is 32-bit end-to-end (Serial + EEPROM + Web JSON)
//  - Original sender transmits temperature-adjusted RC timing, not EC25
//  - Undo sender timing adjustment, convert inverse response, normalize EC once
//  - CF for Home Assistant is extra filtered (EMA) for smooth graphs
//  - ph_raw removed from /data JSON and main web UI
//  - Heavy debug snapshot on every raw_rx removed to avoid hangs
// ============================================================

#include <SPI.h>
#include "Adafruit_GFX.h"
//#include "Adafruit_ILI9341.h"
#include <Wire.h>
#include "RTClib.h"
#include "EEPROM.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <ETCore.h>
#include <time.h>
#include <ArduinoOTA.h>
#include <TFT_eSPI.h>
#include "SerialTransfer.h"
#include <math.h>
#include <Preferences.h>
#include "BucketEcModel.h"
#include "bucket_credentials.h"

// ================= FIRMWARE VERSION =================
#define FW_VERSION "8.9-ecfix6-etcore3"

// ================= pH DOSE STATE =================
bool phDoseActive = false;
bool cfDoseActive = false;

unsigned long phDoseStartMs = 0;
unsigned long cfDoseStartMs = 0;

// latch so you only trigger once per schedule minute
uint32_t lastPhSlotMinute = 0;
uint32_t lastCfSlotMinute = 0;

// ================= SMART AUTO DOSING =================
bool autoDosePH = true;
bool autoDoseCF = true;

// human minutes: 10/20/30/40/50/60
int phDoseIntervalMin = 10;
int cfDoseIntervalMin = 10;

// NOTE: in "simple" mode we don't use smart re-dose,
// but we keep these constants in case you ever want to re-enable.
const float PH_REDOSE_RISE = 0.30f; // (unused in simple mode)
const float CF_REDOSE_DROP = 1.0f;  // (unused in simple mode)

// last-dose references + time (also unused in simple mode)
float    lastPhDoseRefPH = NAN;
float    lastCfDoseRefCF = NAN;
uint32_t lastPhDoseEpoch = 0;
uint32_t lastCfDoseEpoch = 0;

// ================= MAINTENANCE MODE =================
bool maintenanceMode = false;

// ================= POST-DOSE CHECK =================
const uint32_t POST_DOSE_CHECK_DELAY_MS = 5UL * 60UL * 1000UL;

volatile bool     phPostCheckPending = false;
volatile uint32_t phPostCheckDueMs   = 0;
volatile float    phPostCheckBase    = NAN;

volatile bool     cfPostCheckPending = false;
volatile uint32_t cfPostCheckDueMs   = 0;
volatile float    cfPostCheckBase    = NAN;

// ================= pH FILTER =================
#define PH_FILTER_SIZE 100
float phReadings[PH_FILTER_SIZE];
int   phReadIndex = 0;
float phFiltered  = 0;
bool  phFilterInitialized = false;

#define PH_OUTLIER_PERCENT 15
#define PH_MIN_VALID 4.0
#define PH_MAX_VALID 8.0

// pH Temperature Compensation (kept as-is)
#define PH_TEMP_COEF 0.03
#define PH_REF_TEMP  25.0
bool phTempCompEnabled = true;

// ================= EC FILTER =================
#define EC_FILTER_SIZE 30
#define EC_OUTLIER_PERCENT 15
#define EC_MIN_VALID 0.05
#define EC_MAX_VALID 15.0

float ecReadings[EC_FILTER_SIZE];
int   ecReadIndex = 0;
bool  ecFilterInitialized = false;

float ecFiltered = 0;     // filtered EC25 (mS/cm)
float cfFiltered = 0;

// RAW EC is now 32-bit end-to-end
volatile uint32_t raw_ec = 0;

// Debug pH globals
float phBeforeComp      = 0;
float phTempOffset      = 0;
int   phOutliersRemoved = 0;

// ================= EC 2-POINT (HYDRO CF) =================
const float EC14_uS_25 = 1413.0;   // 1.413 mS/cm @25 C
const float EC27_uS_25 = 2770.0;   // 2.770 mS/cm @25 C
const float CF_COEF    = 10.0;     // CF = EC(mS/cm) * 10

bool ecInverse = false;

// 2-point RAWs are now 32-bit
uint32_t ecRaw14 = 0;
uint32_t ecRaw27 = 0;

// Reused as LINEAR terms to keep UI/JSON naming unchanged:
// EC_P  = slope (uS per raw count)
// EC_K2 = intercept (uS)
float EC_P  = 1.0f;
float EC_K2 = 0.0f;
bool  ec2ptValid = false;

// ================= EC TEMPERATURE MODEL =================
static const float EC_ALPHA = BucketEC::SOLUTION_ALPHA;
static const float EC_REF_TEMP = 25.0f;
BucketEC::Calibration ecCalibration = BucketEC::emptyCalibration();
portMUX_TYPE ecModelMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool ecSampleValid = false;
volatile uint32_t lastValidEcMs = 0;
volatile uint32_t ecGoodSamples = 0;
// The latest paired sample is shared only under ecModelMux.
float ecLastCycles = NAN;
float ecLastTemperature = NAN;
uint32_t ecModelGeneration = 0;
uint32_t ecFilterGeneration = 0; // used only on the sampling task
uint32_t ecPublishedGeneration = 0;
BucketEC::RawWindow ecRawWindow;
bool ecStorageInvalid = false;
volatile bool otaInProgress = false;
static const uint32_t EC_STALE_MS = 20000;
bool ecFresh();
bool ecRawFresh();
void invalidateEcRaw();
void invalidateEC();
bool handleEcCalibration(const String &header, WiFiClient &client);
void loadEcCalibration();

float ecNow_mS = 0.0f;   // display-only estimate at current water temp
float ec25_mS  = 0.0f;   // EC25 used for CF/control/telemetry

// ================= CF EXTRA FILTER FOR TELEMETRY =================
float cf = 0; // internal CF used for control
float cfMqttFiltered   = 0.0f;
bool  cfMqttInitialized = false;

// ================= TFT =================
TFT_eSPI tft = TFT_eSPI();

// ================= TELNET MIRROR =================
WiFiServer telnetServer(23);
WiFiClient telnetClient;

class DebugStream : public Print {
public:
  size_t write(uint8_t c) override {
    Serial.write(c);
    if (telnetClient && telnetClient.connected()) telnetClient.write(c);
    return 1;
  }
  size_t write(const uint8_t *buffer, size_t size) override {
    Serial.write(buffer, size);
    if (telnetClient && telnetClient.connected()) telnetClient.write(buffer, size);
    return size;
  }
};
DebugStream Debug;

// ================= TIME =================
WiFiUDP ntpUDP;
// NZ Standard Time (UTC+12)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600 * 12, 60000);

// ================= SERIAL / PINS =================
#define RXD2 16
#define TXD2 17
#define RXD3 25
#define TXD3 26

#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

// ================= WIFI / ET-CORE =================

WiFiServer   server(80);
ETCore core;
bool coreEncrypted = false, coreStarted = false;
volatile bool coreSession = false;
bool reportPending = true;
bool clockReady = false, clockSynced = false;
wifi_ps_type_t bucketWifiSleep = WIFI_PS_MIN_MODEM;
uint32_t lastWifiAttempt = 0;

unsigned long lastCorePublish = 0;
const long    coreInterval    = 9000;

// >>> ADDED for maintenance state tracking <<<
unsigned long lastMaintMs   = 0;
bool          lastMaintSent = false;
// <<< END added globals >>>

unsigned long currentTime   = 0;
unsigned long previousTime  = 0;
const long    timeoutTime   = 2000;

TaskHandle_t Task1;

// ================= RTC =================
RTC_DS3231 rtc;

// ================= IO / MENU VARS =================
String header;

int Minute = 0;
int Hour   = 0;

// pH pump LOWERS pH (acid)
int  Ph_pump  = 5;
// CF pump RAISES CF
int  Cf_pumpA = 32;
int  Cf_pumpB = 33;

const int lcdup    = 13;
const int lcddown  = 35;
const int up_lcd   = 2;
const int down_lcd = 19;

int currentDay = 0;
int newDay     = 0;

int lcd_up = 0;
int lcd    = 0;
int sel    = 0;
int main_meun = 0;
int period    = 9000;

int sel_cal  = 0;
int sel_dose = 0;
int sel_ph   = 0;
int sel_cf   = 0;
int sel_force   = 0;
int sel_ph_cal  = 0;
int sel_cf_cal  = 0;
int sel_ph4_cal = 0;
int sel_ph7_cal = 0;

int ph_on = 0;
int cf_on = 0;

int ph_dose_time = 5;
int cf_dose_time = 5;

int ph_DoseWait = 0;
int cf_DoseWait = 0;
int Dose_ON_cf  = 0;
int Dose_ON_ph  = 0;

int ph_on_time   = 0;
int cf_on_timeA  = 0;
int cf_on_timeB  = 0;
int cf_on_timeC  = 0;

unsigned long read_now = 0;

float CalibrationEC = 2.2;
float Temperature   = 10;

float TemperatureFinish = 0;
float TemperatureStart  = 0;
float Rc1 = 0;
float kt  = 0;

int   i      = 0;
float buffer = 0;
float c      = 0;
float d      = 0;

int ph_set = 0;
int cf_set = 0;
int set    = 0;

int ppm = 0;

int   R1 = 1000;
int   Ra = 25;
float PPMconversion   = 0.7;
float TemperatureCoef = 0.019;

float EC26 = 0;
float EC2  = 0;

float ec = 0;   // DISPLAY EC25 (mS/cm)

int   cf_setpoint   = 27;
float ph_setpoint   = 5.8;
float K             = 0;

float Vin   = 5;
float Vdrop = 0;
float raw1  = 0;

int ph_dose_count = 0;
int cf_dose_count = 0;

int ph_dose_max = 20;
int cf_dose_max = 20;

float cf1 = 0;

int up   = 0;
int down = 0;

int lcd_menu     = 0;
int lcd_menu_sel = 0;
int cal_menu     = 0;

// pH
const int adcPin = 34;
float Vph4       = 0;
float Vph7       = 0;

float Po = 0;

float pH7      = 7;
float pH4      = 4;
float ph       = 0;
float ph_cal_live = 0.0f;
float po_live_v   = 0.0f;
float ph_raw_live = 0.0f;

// EEPROM addresses
const int ph4_Address           = 10;
const int ph7_Address           = 20;
const int cf_Address            = 30;
const int ph_dose_max_Address   = 40;
const int cf_dose_max_Address   = 50;
const int ph_dose_time_Address  = 60;
const int cf_dose_time_Address  = 70;
const int ph_setpoint_Address   = 80;
const int cf_setpoint_Address   = 90;

// NEW SAFE EEPROM LAYOUT FOR 32-bit RAWs
const int ec14_raw_Address      = 100;                                             // uint32_t
const int ec27_raw_Address      = ec14_raw_Address + (int)sizeof(uint32_t);       // 104

// NEW EEPROM (SMART AUTO DOSING)
const int ph_auto_Address       = 140; // uint8_t
const int cf_auto_Address       = 150; // uint8_t
const int ph_interval_Address   = 160; // int
const int cf_interval_Address   = 170; // int

// NEW EEPROM (MAINTENANCE MODE)
const int maint_mode_Address    = 180; // uint8_t

// ================= SERIALTRANSFER =================
SerialTransfer revTransfer;
SerialTransfer sendTransfer;

struct STRUCT1 {
  float t;
  float w;
  float x;
  float y;
  float z;
} revStruct;

uint16_t rev_arr[6] = {};

struct STRUCT2 {
  float a;
  float b;
} sendStruct;

uint16_t send_arr[3] = {0, 1, 2};

unsigned long lastUpdate = 0;

// ================= EXTRA DEBUG STATE =================
static uint32_t lastRawRxMs     = 0;
static uint32_t lastHeartbeatMs = 0;
static const uint32_t HEARTBEAT_MS = 30000; // 30 s

// forward declarations
void Task1code(void * pvParameters);
void serviceCore();
void sendCoreData();
void handleRequest(String header, WiFiClient &client);

float getFilteredPH();
void  updatePhCalLive();
bool  getFilteredEC(uint32_t raw);

void debugPrintSnapshot(const DateTime &now, const char* tag);
void debugPrintBootBanner();

// ================= EEPROM HELPERS =================
static bool saneFloat(float v, float lo, float hi) {
  return isfinite(v) && v >= lo && v <= hi;
}

static uint32_t saneRawU32(uint32_t v) {
  if (v == 0) return 0;
  if (v > 500000UL) return 0;
  return v;
}

static int saneIntervalMin(int v) {
  if (v == 10 || v == 20 || v == 30 || v == 40 || v == 50 || v == 60) return v;
  return 10;
}

void saveAllToEEPROM() {
  EEPROM.put(ph4_Address, Vph4);
  EEPROM.put(ph7_Address, Vph7);

  EEPROM.put(ph_dose_max_Address, ph_dose_max);
  EEPROM.put(cf_dose_max_Address, cf_dose_max);

  EEPROM.put(ph_dose_time_Address, ph_dose_time);
  EEPROM.put(cf_dose_time_Address, cf_dose_time);

  EEPROM.put(ph_setpoint_Address, ph_setpoint);
  EEPROM.put(cf_setpoint_Address, cf_setpoint);

  EEPROM.put(ec14_raw_Address, ecRaw14);
  EEPROM.put(ec27_raw_Address, ecRaw27);

  uint8_t pAuto = autoDosePH ? 1 : 0;
  uint8_t cAuto = autoDoseCF ? 1 : 0;
  EEPROM.put(ph_auto_Address, pAuto);
  EEPROM.put(cf_auto_Address, cAuto);

  phDoseIntervalMin = saneIntervalMin(phDoseIntervalMin);
  cfDoseIntervalMin = saneIntervalMin(cfDoseIntervalMin);
  EEPROM.put(ph_interval_Address, phDoseIntervalMin);
  EEPROM.put(cf_interval_Address, cfDoseIntervalMin);

  uint8_t mMode = maintenanceMode ? 1 : 0;
  EEPROM.put(maint_mode_Address, mMode);

  EEPROM.commit();

  Debug.print(F("[EEPROM] Saved ALL. FW="));
  Debug.println(FW_VERSION);
}

void loadAllFromEEPROM() {
  EEPROM.get(ph4_Address, Vph4);
  EEPROM.get(ph7_Address, Vph7);

  EEPROM.get(ph_dose_max_Address, ph_dose_max);
  EEPROM.get(cf_dose_max_Address, cf_dose_max);

  EEPROM.get(ph_dose_time_Address, ph_dose_time);
  EEPROM.get(cf_dose_time_Address, cf_dose_time);

  EEPROM.get(ph_setpoint_Address, ph_setpoint);
  EEPROM.get(cf_setpoint_Address, cf_setpoint);

  EEPROM.get(ec14_raw_Address, ecRaw14);
  EEPROM.get(ec27_raw_Address, ecRaw27);

  uint8_t pAuto = 1, cAuto = 1;
  EEPROM.get(ph_auto_Address, pAuto);
  EEPROM.get(cf_auto_Address, cAuto);
  autoDosePH = (pAuto == 1);
  autoDoseCF = (cAuto == 1);

  EEPROM.get(ph_interval_Address, phDoseIntervalMin);
  EEPROM.get(cf_interval_Address, cfDoseIntervalMin);
  phDoseIntervalMin = saneIntervalMin(phDoseIntervalMin);
  cfDoseIntervalMin = saneIntervalMin(cfDoseIntervalMin);

  uint8_t mMode = 0;
  EEPROM.get(maint_mode_Address, mMode);
  maintenanceMode = (mMode == 1);

  if (!saneFloat(Vph4, 0.0f, 5.0f)) Vph4 = 0.0f;
  if (!saneFloat(Vph7, 0.0f, 5.0f)) Vph7 = 0.0f;

  if (!saneFloat(ph_setpoint, 4.0f, 8.0f)) ph_setpoint = 5.8f;
  if (cf_setpoint < 0 || cf_setpoint > 300) cf_setpoint = 27;

  if (ph_dose_time < 1 || ph_dose_time > 120) ph_dose_time = 5;
  if (cf_dose_time < 1 || cf_dose_time > 120) cf_dose_time = 5;

  if (ph_dose_max < 0 || ph_dose_max > 999) ph_dose_max = 20;
  if (cf_dose_max < 0 || cf_dose_max > 999) cf_dose_max = 20;

  ecRaw14 = saneRawU32(ecRaw14);
  ecRaw27 = saneRawU32(ecRaw27);

  debugPrintBootBanner();
}

// ============================================================
// EC 2-POINT CALIBRATION (PURE LINEAR AT 25 C REFERENCE)
//      RAW14 -> 1413 uS
//      RAW27 -> 2770 uS
// ============================================================
void updateEC2ptCalibration() {
  ec2ptValid = false;

  if (ecRaw14 == 0 || ecRaw27 == 0 || ecRaw14 == ecRaw27) {
    Debug.println(F("[EC2PT] FAIL: raw14/raw27 invalid"));
    return;
  }

  ecInverse = (ecRaw27 < ecRaw14);

  const float r14 = (float)ecRaw14;
  const float r27 = (float)ecRaw27;

  const float u14 = EC14_uS_25;
  const float u27 = EC27_uS_25;

  const float slope     = (u27 - u14) / (r27 - r14);
  const float intercept = u14 - (slope * r14);

  if (!isfinite(slope) || !isfinite(intercept) || fabsf(slope) < 1e-9f) {
    Debug.println(F("[EC2PT] FAIL: slope/intercept invalid"));
    return;
  }

  EC_P  = slope;      // uS per raw
  EC_K2 = intercept;  // uS

  ec2ptValid          = true;
  ecFilterInitialized = false; // re-init filter so it locks in immediately

  Debug.print(F("[EC2PT] OK "));
  Debug.print(ecInverse ? F("(INVERSE RAW)") : F("(DIRECT RAW)"));
  Debug.print(F(" slope="));      Debug.print(EC_P, 6);
  Debug.print(F(" intercept="));  Debug.print(EC_K2, 2);
  Debug.print(F(" raw14="));      Debug.print(ecRaw14);
  Debug.print(F(" raw27="));      Debug.println(ecRaw27);
}

// ================= pH FILTER (SAFE) =================
float getFilteredPH() {
  analogReadResolution(12);

  float Po_local = analogRead(adcPin) * 5.0f / 4095.0f;

  if (Vph7 <= 0 || Vph4 <= 0 || fabsf(Vph7 - Vph4) < 0.05f) {
    return phFiltered;
  }

  const float m = (pH7 - pH4) / (Vph7 - Vph4);
  float phRaw   = 7.0f - (Vph7 - Po_local) * m;

  if (phRaw < PH_MIN_VALID || phRaw > PH_MAX_VALID) {
    return phFiltered;
  }

  phBeforeComp = phRaw;
  phTempOffset = 0;

  if (phTempCompEnabled && Temperature > 0 && Temperature < 50) {
    float tempDiff = Temperature - PH_REF_TEMP;
    phTempOffset   = PH_TEMP_COEF * tempDiff * (phRaw - 7.0f);
    phRaw         -= phTempOffset;
  }

  phRaw       = constrain(phRaw, PH_MIN_VALID, PH_MAX_VALID);
  ph_raw_live = phRaw;

  if (!phFilterInitialized) {
    for (int i = 0; i < PH_FILTER_SIZE; i++) phReadings[i] = phRaw;
    phFiltered        = phRaw;
    phReadIndex       = 0;
    phFilterInitialized = true;
    return phFiltered;
  }

  phReadings[phReadIndex] = phRaw;
  phReadIndex             = (phReadIndex + 1) % PH_FILTER_SIZE;

  float sortedReadings[PH_FILTER_SIZE];
  memcpy(sortedReadings, phReadings, sizeof(sortedReadings));

  for (int i = 0; i < PH_FILTER_SIZE - 1; i++) {
    for (int j = 0; j < PH_FILTER_SIZE - i - 1; j++) {
      if (sortedReadings[j] > sortedReadings[j + 1]) {
        float t              = sortedReadings[j];
        sortedReadings[j]    = sortedReadings[j + 1];
        sortedReadings[j + 1]= t;
      }
    }
  }

  int trim = (PH_FILTER_SIZE * PH_OUTLIER_PERCENT) / 100;
  if (trim < 1) trim = 1;

  int start = trim;
  int end   = PH_FILTER_SIZE - trim;

  float sum = 0;
  for (int i = start; i < end; i++) sum += sortedReadings[i];

  phFiltered        = sum / (end - start);
  phOutliersRemoved = trim * 2;

  return phFiltered;
}

void updatePhCalLive() {
  analogReadResolution(12);
  float Po_local = analogRead(adcPin) * 5.0f / 4095.0f;
  po_live_v      = Po_local;

  float m_local  = 0.0f;
  if (Vph7 > 0 && Vph4 > 0 && fabsf(Vph7 - Vph4) >= 0.05f) {
    m_local       = (pH7 - pH4) / (Vph7 - Vph4);
  }

  ph_cal_live = (m_local != 0.0f) ? (7.0f - (Vph7 - Po_local) * m_local) : ph;
}

// ============================================================
// EC FILTER - RAW FILTER + 2PT MAP => EC25
//   Pair each timing sample with its temperature BEFORE filtering.
//   EC25 uses the inverse RC response and a measured reference calibration.
// ============================================================
bool getFilteredEC(uint32_t raw) {
  const float sampleTemperature = Temperature;
  float cycles = NAN;
  if (!BucketEC::decodeTiming(float(raw), sampleTemperature, cycles)) return false;
  BucketEC::Calibration model;
  uint32_t generation;
  portENTER_CRITICAL(&ecModelMux);
  ecRawWindow.push(float(raw), sampleTemperature, millis());
  ecLastCycles = cycles; ecLastTemperature = sampleTemperature;
  model = ecCalibration; generation = ecModelGeneration;
  portEXIT_CRITICAL(&ecModelMux);
  float rawF = NAN;
  if (!BucketEC::convert(cycles, sampleTemperature, model, rawF)) return false;
  if (generation != ecFilterGeneration) {
    ecFilterInitialized = false; cfMqttInitialized = false;
    ecSampleValid = false; ecGoodSamples = 0;
    ecFilterGeneration = generation;
  }

  // Filter individually corrected EC25 samples; never mix timings at different temperatures.
  if (!ecFilterInitialized) {
    for (int i = 0; i < EC_FILTER_SIZE; i++) ecReadings[i] = rawF;
    ecReadIndex         = 0;
    ecFilterInitialized = true;
  } else {
    ecReadings[ecReadIndex] = rawF;
    ecReadIndex             = (ecReadIndex + 1) % EC_FILTER_SIZE;
  }

  float sortedRaw[EC_FILTER_SIZE];
  memcpy(sortedRaw, ecReadings, sizeof(sortedRaw));

  for (int i = 0; i < EC_FILTER_SIZE - 1; i++) {
    for (int j = 0; j < EC_FILTER_SIZE - i - 1; j++) {
      if (sortedRaw[j] > sortedRaw[j + 1]) {
        float t           = sortedRaw[j];
        sortedRaw[j]      = sortedRaw[j + 1];
        sortedRaw[j + 1]  = t;
      }
    }
  }

  int trim = (EC_FILTER_SIZE * EC_OUTLIER_PERCENT) / 100;
  if (trim < 1) trim = 1;

  int start = trim;
  int end   = EC_FILTER_SIZE - trim;

  float sumRaw = 0.0f;
  for (int i = start; i < end; i++) sumRaw += sortedRaw[i];

  float rawFiltered = sumRaw / (float)(end - start);
  if (!isfinite(rawFiltered) || rawFiltered <= 0.0f) return false;

  ec25_mS = rawFiltered;
  ecNow_mS = ec25_mS * BucketEC::factor(sampleTemperature, EC_ALPHA);

  // System values use EC25.
  ecFiltered = ec25_mS;
  ec         = ecFiltered;

  cfFiltered = ecFiltered * CF_COEF;
  cf         = cfFiltered;

  // EXTRA EMA just for Home Assistant graph smoothing
  if (!cfMqttInitialized) {
    cfMqttFiltered   = cf;
    cfMqttInitialized = true;
  } else {
    cfMqttFiltered = 0.8f * cfMqttFiltered + 0.2f * cf;
  }

  portENTER_CRITICAL(&ecModelMux);
  const bool sameModel = generation == ecModelGeneration;
  if (sameModel) {
    ecPublishedGeneration = generation;
    lastValidEcMs = millis(); ecSampleValid = true;
    if (ecGoodSamples < 1000000) ecGoodSamples = ecGoodSamples + 1;
  }
  portEXIT_CRITICAL(&ecModelMux);
  return sameModel;
}

// Never write an invalid NTP result into the dosing RTC. SNTP runs in the
// background: a failed lookup cannot block pump timers or turn 1970 into 2106.
bool validBucketClock(const DateTime& value) {
  return value.year() >= 2026 && value.year() <= 2099 && value.month() >= 1 &&
    value.month() <= 12 && value.day() >= 1 && value.day() <= 31 &&
    value.hour() <= 23 && value.minute() <= 59 && value.second() <= 59;
}
void serviceClock() {
  if (clockSynced || WiFi.status()!=WL_CONNECTED) return;
  const time_t epoch=time(nullptr);
  if (epoch < 1767225600LL || epoch >= 4102444800LL) return;
  // Preserve this controller's original UTC+12 RTC convention.
  rtc.adjust(DateTime(uint32_t(epoch)+43200UL));
  clockReady=validBucketClock(rtc.now()); clockSynced=clockReady;
  if(clockReady) Debug.println(F("[CLOCK] RTC synchronised from valid SNTP time"));
}

// ================= ET-CORE =================
// The background task exclusively owns the ET-Core socket. Sensor/dosing loops
// and the original web controls remain independent of the network transport.
void coreHello(JsonObject p) { p["model"]="Hydroponics Bucket EC/pH"; p["profile"]="hydro_bucket_ecfix6_v1"; }
void coreSync() { reportPending = true; }
void serviceCore() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    coreStarted = false; coreSession = false;
    if (uint32_t(now-lastWifiAttempt) >= 15000) { lastWifiAttempt=now; WiFi.reconnect(); }
    return;
  }
  if (!coreEncrypted) return; // Never fall back to plaintext.
  if (!coreStarted) {
    core.begin("hydro-bucket", "sensor.hydro_bucket", "Hydroponics Bucket", FW_VERSION);
    // Preserve the existing controller power behaviour for its analogue pH input.
    WiFi.setSleep(bucketWifiSleep);
    coreStarted=true; reportPending=true;
  }
  core.loop();
  coreSession=coreStarted && core.encryptionEnabled(); // Sender ready; hub reception is verified in HA.
  if (reportPending || uint32_t(now-lastCorePublish) >= uint32_t(coreInterval)) {
    reportPending=false; lastCorePublish=now; sendCoreData();
  }
}
void sendCoreData() {
  ph = getFilteredPH();
  const bool ecReady=ecFresh(), rawReady=ecRawFresh();
  const float cfOut = cfMqttInitialized ? cfMqttFiltered : cf;
  JsonDocument doc;
  JsonObject p=doc.to<JsonObject>();
  if (isfinite(ph)) p["ph"]=roundf(ph*100)/100; else p["ph"]=nullptr;
  if (ecReady && isfinite(cfOut)) p["cf"]=roundf(cfOut*10)/10; else p["cf"]=nullptr;
  if (ecReady && isfinite(ec)) p["ec"]=roundf(ec*1000)/1000; else p["ec"]=nullptr;
  if (rawReady && isfinite(Temperature)) p["temperature"]=roundf(Temperature*10)/10; else p["temperature"]=nullptr;
  p["ph_dose_count"]=ph_dose_count; p["cf_dose_count"]=cf_dose_count;
  p["ec_valid"]=ecReady; p["raw_valid"]=rawReady; p["maintenance"]=maintenanceMode; p["clock_valid"]=clockReady;
  p["ph_active"]=phDoseActive; p["cf_active"]=cfDoseActive;
  core.sendState(p);
}

// ================= WEB PARSER =================
static String getParam(const String &hdr, const String &key) {
  int i = hdr.indexOf(key + "=");
  if (i < 0) return "";
  i += key.length() + 1;
  int end = hdr.indexOf('&', i);
  int sp  = hdr.indexOf(' ', i);
  if (end < 0 || (sp >= 0 && sp < end)) end = sp;
  if (end < 0) end = hdr.length();
  return hdr.substring(i, end);
}

// ================= JSON SAFE PRINT HELPERS =================
static void jsonPrintNum(Print &out, float v, int decimals) {
  if (!isfinite(v)) { out.print("null"); return; }
  out.print(String(v, decimals));
}
static void jsonPrintInt(Print &out, int v) { out.print(v); }
static void jsonPrintU32(Print &out, uint32_t v) { out.print(v); }

void handleRequest(String header, WiFiClient &client) {
  if (handleEcCalibration(header, client)) return;
  bool saveNow = (header.indexOf("save=1") >= 0);

  String v;

  v = getParam(header, "phwebset");
  if (v.length()) ph_setpoint = v.toFloat();

  v = getParam(header, "cfwebset");
  if (v.length()) cf_setpoint = v.toInt();

  v = getParam(header, "phwebtime");
  if (v.length()) ph_dose_time = v.toInt();

  v = getParam(header, "cfwebtime");
  if (v.length()) cf_dose_time = v.toInt();

  v = getParam(header, "vph4");
  if (v.length()) Vph4 = v.toFloat();

  v = getParam(header, "vph7");
  if (v.length()) Vph7 = v.toFloat();

  v = getParam(header, "phmax");
  if (v.length()) ph_dose_max = v.toInt();

  v = getParam(header, "cfmax");
  if (v.length()) cf_dose_max = v.toInt();

  v = getParam(header, "ec14raw");
  if (v.length()) {
    ecRaw14 = (uint32_t)v.toInt();
  }

  v = getParam(header, "ec27raw");
  if (v.length()) {
    ecRaw27 = (uint32_t)v.toInt();
  }

  v = getParam(header, "phauto");
  if (v.length()) autoDosePH = (v.toInt() == 1);

  v = getParam(header, "cfauto");
  if (v.length()) autoDoseCF = (v.toInt() == 1);

  v = getParam(header, "phinterval");
  if (v.length()) phDoseIntervalMin = saneIntervalMin(v.toInt());

  v = getParam(header, "cfinterval");
  if (v.length()) cfDoseIntervalMin = saneIntervalMin(v.toInt());

  v = getParam(header, "maint");
  if (v.length()) maintenanceMode = (v.toInt() == 1);

  updateEC2ptCalibration();

  if (saveNow) {
    saveAllToEEPROM();
  }

  Debug.print(F("[WEB] Updated. saveNow="));
  Debug.println(saveNow ? F("YES") : F("NO"));

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/plain");
  client.println("Connection: close");
  client.println();
  client.println("OK");
}

// ================= EXTRA DEBUG HELPERS =================
static void printDateTime(DebugStream &out, const DateTime &now) {
  out.print(now.year()); out.print(F("-"));
  if (now.month() < 10) out.print("0");
  out.print(now.month()); out.print(F("-"));
  if (now.day() < 10) out.print("0");
  out.print(now.day()); out.print(F(" "));
  if (now.hour() < 10) out.print("0");
  out.print(now.hour()); out.print(F(":"));
  if (now.minute() < 10) out.print("0");
  out.print(now.minute()); out.print(F(":"));
  if (now.second() < 10) out.print("0");
  out.print(now.second());
}

void debugPrintBootBanner() {
  Debug.println(F("========== BOOT =========="));
  Debug.print(F("[FW] Version: ")); Debug.println(FW_VERSION);
  Debug.print(F("[EEPROM] Vph4=")); Debug.println(Vph4, 3);
  Debug.print(F("[EEPROM] Vph7=")); Debug.println(Vph7, 3);
  Debug.print(F("[EEPROM] ph_set=")); Debug.println(ph_setpoint, 2);
  Debug.print(F("[EEPROM] cf_set=")); Debug.println(cf_setpoint);
  Debug.print(F("[EEPROM] ph_time=")); Debug.println(ph_dose_time);
  Debug.print(F("[EEPROM] cf_time=")); Debug.println(cf_dose_time);
  Debug.print(F("[EEPROM] ph_max=")); Debug.println(ph_dose_max);
  Debug.print(F("[EEPROM] cf_max=")); Debug.println(cf_dose_max);
  Debug.print(F("[EEPROM] raw14=")); Debug.println(ecRaw14);
  Debug.print(F("[EEPROM] raw27=")); Debug.println(ecRaw27);
  Debug.print(F("[EEPROM] autoDosePH=")); Debug.println(autoDosePH ? F("ON") : F("OFF"));
  Debug.print(F("[EEPROM] autoDoseCF=")); Debug.println(autoDoseCF ? F("ON") : F("OFF"));
  Debug.print(F("[EEPROM] phIntervalMin=")); Debug.println(phDoseIntervalMin);
  Debug.print(F("[EEPROM] cfIntervalMin=")); Debug.println(cfDoseIntervalMin);
  Debug.print(F("[EEPROM] maintenanceMode=")); Debug.println(maintenanceMode ? F("ON") : F("OFF"));
  Debug.print(F("[EC] Two-point calibration with paired temperatures. alpha=")); Debug.println(EC_ALPHA, 4);
  Debug.println(F("=========================="));
}

void debugPrintSnapshot(const DateTime &now, const char* tag) {
  Debug.println();
  Debug.print(F("========== DEBUG SNAPSHOT (")); Debug.print(tag); Debug.println(F(") =========="));

  Debug.print(F("FW Version        : ")); Debug.println(FW_VERSION);
  Debug.print(F("DateTime          : ")); printDateTime(Debug, now); Debug.println();

  Debug.print(F("WiFi Connected    : ")); Debug.println(WiFi.status() == WL_CONNECTED ? F("YES") : F("NO"));
  Debug.print(F("WiFi RSSI (dBm)   : ")); Debug.println(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  Debug.print(F("IP                : ")); Debug.println(WiFi.localIP());
  Debug.print(F("Free Heap (bytes) : ")); Debug.println(ESP.getFreeHeap());
  Debug.print(F("Uptime (s)        : ")); Debug.println((uint32_t)(millis() / 1000UL));

  Debug.print(F("ET-Core sender    : ")); Debug.println(coreSession ? F("YES") : F("NO"));
  Debug.print(F("Telnet Connected  : ")); Debug.println((telnetClient && telnetClient.connected()) ? F("YES") : F("NO"));

  Debug.print(F("Temperature (C)   : ")); Debug.println(Temperature, 2);

  Debug.print(F("pH Filtered       : ")); Debug.println(ph, 2);
  Debug.print(F("pH Raw Live       : ")); Debug.println(ph_raw_live, 3);
  Debug.print(F("pH Setpoint       : ")); Debug.println(ph_setpoint, 2);

  Debug.print(F("CF (Hydro)        : ")); Debug.println(cf, 1);
  Debug.print(F("EC Now (mS/cm)    : ")); Debug.println(ecNow_mS, 3);
  Debug.print(F("EC25 (mS/cm)      : ")); Debug.println(ec, 3);

  Debug.print(F("Vph4              : ")); Debug.println(Vph4, 3);
  Debug.print(F("Vph7              : ")); Debug.println(Vph7, 3);
  Debug.print(F("Po Live (V)       : ")); Debug.println(po_live_v, 3);
  Debug.print(F("pH Cal Live       : ")); Debug.println(ph_cal_live, 2);

  Debug.print(F("RAW EC Live       : ")); Debug.println((uint32_t)raw_ec);
  Debug.print(F("Last RAW age (s)  : "));
  uint32_t ageS = (lastRawRxMs == 0) ? 0 : (uint32_t)((millis() - lastRawRxMs) / 1000UL);
  Debug.println(ageS);

  Debug.print(F("RAW14 (1.413)     : ")); Debug.println(ecRaw14);
  Debug.print(F("RAW27 (2.770)     : ")); Debug.println(ecRaw27);
  Debug.print(F("EC_P (slope)      : ")); Debug.println(EC_P, 6);
  Debug.print(F("EC_K2 (intercept) : ")); Debug.println(EC_K2, 2);
  Debug.print(F("EC Direction      : ")); Debug.println(ecInverse ? F("INVERSE RAW") : F("DIRECT RAW"));
  Debug.print(F("EC2OK             : ")); Debug.println(ec2ptValid ? F("YES") : F("NO"));
  Debug.print(F("EC Temp Fix       : ")); Debug.println(F("ON - inverse timing; paired temperature normalization"));

  Debug.print(F("Auto Dose PH      : ")); Debug.println(autoDosePH ? F("ON") : F("OFF"));
  Debug.print(F("Auto Dose CF      : ")); Debug.println(autoDoseCF ? F("ON") : F("OFF"));
  Debug.print(F("Dose Interval PH  : ")); Debug.print(phDoseIntervalMin); Debug.println(F(" min"));
  Debug.print(F("Dose Interval CF  : ")); Debug.print(cfDoseIntervalMin); Debug.println(F(" min"));

  Debug.print(F("pH Dose Time (s)  : ")); Debug.println(ph_dose_time);
  Debug.print(F("CF Dose Time (s)  : ")); Debug.println(cf_dose_time);
  Debug.print(F("pH Dose Max       : ")); Debug.println(ph_dose_max);
  Debug.print(F("CF Dose Max       : ")); Debug.println(cf_dose_max);

  Debug.print(F("pH Dose Count     : ")); Debug.println(ph_dose_count);
  Debug.print(F("CF Dose Count     : ")); Debug.println(cf_dose_count);

  Debug.print(F("pH Dose Active    : ")); Debug.println(phDoseActive ? F("YES") : F("NO"));
  Debug.print(F("CF Dose Active    : ")); Debug.println(cfDoseActive ? F("YES") : F("NO"));

  Debug.print(F("Maintenance Mode  : ")); Debug.println(maintenanceMode ? F("ON") : F("OFF"));

  Debug.println(F("======================================================="));
}

// ================= SETUP =================
void setup() {
  digitalWrite(Cf_pumpA, LOW); digitalWrite(Cf_pumpB, LOW); digitalWrite(Ph_pump, LOW);
  analogReadResolution(12);

  pinMode(Cf_pumpA, OUTPUT);
  pinMode(Cf_pumpB, OUTPUT);
  pinMode(Ph_pump,  OUTPUT);

  pinMode(2,  INPUT_PULLUP);
  pinMode(13, INPUT_PULLUP);
  pinMode(19, INPUT_PULLUP);
  pinMode(35, INPUT_PULLUP);

  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial1.setTimeout(30);
  Serial2.begin(9600, SERIAL_8N1, RXD3, TXD3);

  revTransfer.begin(Serial1);
  sendTransfer.begin(Serial2);

  EEPROM.begin(512);
  rtc.begin();

  WiFi.mode(WIFI_STA);
  bucketWifiSleep=WiFi.getSleep();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);

  R1 = (R1 + Ra);

  Debug.print(F("Connecting to "));
  Debug.println(ssid);
  WiFi.begin(ssid, password);
  const uint32_t wifiStarted = millis();
  while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - wifiStarted) < 20000) {
    delay(500);
    Debug.print(F("."));
  }

  Debug.println();
  Debug.println(F("WiFi connected."));
  Debug.print(F("IP address: "));
  Debug.println(WiFi.localIP());

  ArduinoOTA.setHostname("ecph-controller");
  ArduinoOTA.onStart([]() {
    otaInProgress = true; maintenanceMode = true;
    digitalWrite(Ph_pump, LOW); digitalWrite(Cf_pumpA, LOW); digitalWrite(Cf_pumpB, LOW);
    phDoseActive = false; cfDoseActive = false;
    Debug.println(F("Start OTA - pumps stopped"));
  });
  ArduinoOTA.onEnd([]() { Debug.println(F("\nEnd OTA")); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Debug.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false; // Maintenance stays enabled after an aborted upload.
    Debug.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)    Debug.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR)   Debug.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Debug.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Debug.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR)     Debug.println(F("End Failed"));
  });
  ArduinoOTA.begin();
  Debug.println(F("OTA Ready"));

  server.begin();
  coreEncrypted=core.enableEncryptionHex(et_core_key);
  core.setPort(5560); core.setLeaseInterval(2000); core.setLeaseTtl(10);
  core.onHello(coreHello); core.onSync(coreSync);
  if (!coreEncrypted) Debug.println(F("[ET-Core] Invalid key: transport disabled"));

  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Debug.println(F("Telnet server started on port 23."));

  loadAllFromEEPROM();
  updateEC2ptCalibration();
  loadEcCalibration();

  xTaskCreatePinnedToCore(Task1code, "Task1", 12000, NULL, 1, &Task1, 0);
  delay(500);

  clockReady=validBucketClock(rtc.now());
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Debug.println(clockReady ? F("[CLOCK] Existing RTC valid; SNTP starting") :
    F("[CLOCK] Invalid RTC; dosing blocked until SNTP succeeds"));

  // first snapshot right away
  debugPrintSnapshot(rtc.now(), "BOOT-SNAPSHOT");
}

// ================= LOOP =================
void loop() {
  serviceClock();
  ArduinoOTA.handle();
  if (ecSampleValid && uint32_t(millis() - lastValidEcMs) > EC_STALE_MS) invalidateEC();

  DateTime now = rtc.now();

  // Telnet single-client
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      telnetClient = telnetServer.accept();
      telnetClient.println("Telnet connected to ESP32");
      telnetClient.print("FW="); telnetClient.println(FW_VERSION);
      telnetClient.println("Tip: Open /debug in browser for a status dump.");
      Debug.println(F("[TELNET] Client connected."));
    } else {
      WiFiClient reject = telnetServer.accept();
      reject.println("Only one Telnet client allowed");
      reject.stop();
    }
  }

  // periodic heartbeat snapshot (even when Serial1 is dead)
  if (millis() - lastHeartbeatMs >= HEARTBEAT_MS) {
    lastHeartbeatMs = millis();

    ph = getFilteredPH();
    updatePhCalLive();

    debugPrintSnapshot(now, "HEARTBEAT");
  }

  // Safe m for cal screens
  float m = 0;
  if (Vph7 > 0 && Vph4 > 0 && fabsf(Vph7 - Vph4) >= 0.05f) {
    m = (pH7 - pH4) / (Vph7 - Vph4);
  }

  updatePhCalLive();

  up   = digitalRead(up_lcd);
  down = digitalRead(down_lcd);
  lcd_up = digitalRead(lcdup);
  lcd    = digitalRead(lcddown);

  Hour   = now.hour();
  Minute = now.minute();

  currentDay = now.day();
  if (newDay != currentDay) {
    newDay        = currentDay;
    ph_dose_count = 0;
    cf_dose_count = 0;
    tft.setCursor(220, 160); tft.println("  ");
    tft.setCursor(220, 180); tft.println("  ");
    Debug.println(F("[DAY] New day -> dose counters reset."));
  }

  // ================= MAIN SCREEN =================
  if (lcd_menu == 0) {
    tft.setTextColor(GREEN, BLACK);
    tft.setTextSize(3);
    tft.setCursor(0, 0);
    tft.print("Ph EC Controller ");
    tft.print(FW_VERSION);

    tft.setTextSize(2);
    tft.setCursor(0, 60);
    tft.println("Time");
    tft.setCursor(78, 60);  tft.println(now.hour(), DEC);
    tft.setCursor(105, 60); tft.println(":");
    tft.setCursor(118, 60); tft.println(now.minute(), DEC);

    if (now.hour() < 10) {
      tft.setCursor(78, 60); tft.println("0");
      tft.setCursor(90, 60); tft.println(now.hour(), DEC);
    }
    if (now.minute() < 10) {
      tft.setCursor(115, 60); tft.println("0");
      tft.setCursor(130, 60); tft.println(now.minute(), DEC);
    }

    tft.setCursor(0, 100);
    tft.println("Water Temperature");
    tft.setCursor(220, 100);
    tft.println(Temperature);

    tft.setTextSize(4);
    tft.setCursor(0, 160);
    tft.println("PH");
    tft.setCursor(60, 160);
    tft.println(ph, 1);

    tft.setCursor(160, 160);
    tft.println("CF");
    tft.fillRect(220, 160, 100, 36, BLACK);
    tft.setCursor(220, 160);
    if (ecFresh()) tft.println(cf, 1);
    else tft.println("--");

    tft.setTextSize(2);
    tft.setCursor(0, 220);
    tft.println("PH Dose Count");
    tft.setCursor(220, 220);
    tft.println(ph_dose_count);

    tft.setCursor(0, 260);
    tft.println("CF Dose Count");
    tft.setCursor(220, 260);
    tft.println(cf_dose_count);
  }

  // ================= MENU 1 =================
  if (lcd_menu == 1) {
    main_meun = 0;

    tft.setTextSize(3);
    tft.setCursor(0, 20);  tft.println("Set Points:");
    tft.setCursor(0, 60);  tft.println("Calibration:");
    tft.setCursor(0, 100); tft.println("Dose Settings:");
    tft.setCursor(0, 140); tft.println("Force Dose:");
    tft.setCursor(0, 180); tft.println("Main Meun:");

    if ((lcd == LOW) && (lcd_menu == 1)) sel++;

    if (sel == 1) { tft.setCursor(250, 20);  tft.println("<"); tft.setCursor(250, 60);  tft.println(" "); tft.setCursor(250, 100); tft.println(" "); tft.setCursor(250, 140); tft.println(" "); tft.setCursor(250, 180); tft.println(" "); lcd_menu_sel = 2; }
    if (sel == 2) { tft.setCursor(250, 20);  tft.println(" "); tft.setCursor(250, 60);  tft.println("<"); tft.setCursor(250, 100); tft.println(" "); tft.setCursor(250, 140); tft.println(" "); tft.setCursor(250, 180); tft.println(" "); lcd_menu_sel = 3; }
    if (sel == 3) { tft.setCursor(250, 20);  tft.println(" "); tft.setCursor(250, 60);  tft.println(" "); tft.setCursor(250, 100); tft.println("<"); tft.setCursor(250, 140); tft.println(" "); tft.setCursor(250, 180); tft.println(" "); lcd_menu_sel = 4; }
    if (sel == 4) { tft.setCursor(250, 20);  tft.println(" "); tft.setCursor(250, 60);  tft.println(" "); tft.setCursor(250, 100); tft.println(" "); tft.setCursor(250, 140); tft.println("<"); tft.setCursor(250, 180); tft.println(" "); lcd_menu_sel = 5; }
    if (sel == 5) { tft.setCursor(250, 20);  tft.println(" "); tft.setCursor(250, 60);  tft.println(" "); tft.setCursor(250, 100); tft.println(" "); tft.setCursor(250, 140); tft.println(" "); tft.setCursor(250, 180); tft.println("<"); lcd_menu_sel = 6; }

    if (sel > 6) sel = 0;

    if ((lcd_up == LOW) && (sel == 1)) { lcd_menu = 2; tft.fillScreen(TFT_BLACK); }
    if ((lcd_up == LOW) && (sel == 2)) { lcd_menu = 3; tft.fillScreen(TFT_BLACK); }
    if ((lcd_up == LOW) && (sel == 3)) { lcd_menu = 4; tft.fillScreen(TFT_BLACK); }
    if ((lcd_up == LOW) && (sel == 4)) { lcd_menu = 5; tft.fillScreen(TFT_BLACK); }
    if ((lcd_up == LOW) && (sel == 5)) { tft.fillScreen(TFT_BLACK); lcd_menu = 0; }
  }

  // ================= MENU 2 (setpoints) =================
  if (lcd_menu == 2) {
    tft.setTextColor(GREEN, BLACK);
    tft.setTextSize(3);
    tft.setCursor(0, 20);  tft.println("PH Set Point:");
    tft.setCursor(0, 60);  tft.println(ph_setpoint, 1);
    tft.setCursor(0, 100); tft.println("CF Set Point:");
    tft.setCursor(0, 140); tft.println(cf_setpoint, 1);

    tft.setCursor(0, 180); tft.println("Meun:");

    if ((lcd == LOW) && (lcd_menu == 2)) set++;

    if (set == 1) {
      tft.setCursor(100, 60);  tft.println("<");
      tft.setCursor(100, 140); tft.println("     ");
      tft.setCursor(100, 180); tft.println("     ");
    }
    if ((up == LOW) && (set == 1))   ph_setpoint = ph_setpoint + 0.1;
    if ((down == LOW) && (set == 1)) ph_setpoint = ph_setpoint - 0.1;

    if (set == 2) {
      tft.setCursor(100, 60);  tft.println("     ");
      tft.setCursor(100, 140); tft.println("<");
      tft.setCursor(100, 180); tft.println("     ");
    }
    if ((up == LOW) && (set == 2))   cf_setpoint = cf_setpoint + 1;
    if ((down == LOW) && (set == 2)) cf_setpoint = cf_setpoint - 1;

    if (set == 3) {
      tft.setCursor(100, 60);  tft.println("     ");
      tft.setCursor(100, 140); tft.println("     ");
      tft.setCursor(100, 180); tft.println("<");
    }

    if (set == 4) {
      set = 0;
      tft.setCursor(100, 60);  tft.println("     ");
      tft.setCursor(100, 140); tft.println("     ");
      tft.setCursor(100, 180); tft.println("    ");
    }

    if ((lcd_up == LOW) && (set == 3)) {
      lcd_menu = 1;
      set      = 0;
      tft.fillScreen(TFT_BLACK);
    }
  }

  // ================= MENU 3 (Calibration menu) =================
  if (lcd_menu == 3) {
    tft.setTextSize(3);
    tft.setTextColor(GREEN, BLACK);
    tft.setCursor(0, 20);  tft.println("Calibration:");
    tft.setCursor(0, 60);  tft.println("PH Cal:");
    tft.setCursor(0, 100); tft.println("EC 2PT Cal:");
    tft.setCursor(0, 130); tft.println("Save: ");
    tft.setCursor(0, 160); tft.println("Menu:");

    if ((lcd == LOW) && (lcd_menu == 3)) sel_cal++;

    if (sel_cal == 1) { tft.setCursor(150, 60);  tft.println("<"); tft.setCursor(150, 100); tft.println(" "); tft.setCursor(150, 130); tft.println(" "); tft.setCursor(150, 160); tft.println(" "); }
    if (sel_cal == 2) { tft.setCursor(150, 60);  tft.println(" "); tft.setCursor(150, 100); tft.println("<"); tft.setCursor(150, 130); tft.println(" "); tft.setCursor(150, 160); tft.println(" "); }
    if (sel_cal == 3) { tft.setCursor(150, 60);  tft.println(" "); tft.setCursor(150, 100); tft.println(" "); tft.setCursor(150, 130); tft.println("<"); tft.setCursor(150, 160); tft.println(" "); }
    if (sel_cal == 4) { tft.setCursor(150, 60);  tft.println(" "); tft.setCursor(150, 100); tft.println(" "); tft.setCursor(150, 130); tft.println(" "); tft.setCursor(150, 160); tft.println("<"); }

    if (sel_cal == 5) sel_cal = 0;

    if ((lcd_up == LOW) && (sel_cal == 1)) { lcd_menu = 7; sel_cal = 1; tft.fillScreen(TFT_BLACK); }
    if ((lcd_up == LOW) && (sel_cal == 2)) { lcd_menu = 8; sel_cal = 2; tft.fillScreen(TFT_BLACK); }

    if ((up == LOW) && (sel_cal == 3)) {
      saveAllToEEPROM();
      updateEC2ptCalibration();
      tft.setCursor(0, 130);
      tft.println("Saved:");
      Debug.println(F("[LCD] Save pressed -> EEPROM saved."));
    }

    if ((lcd_up == LOW) && (sel_cal == 4)) {
      tft.fillScreen(TFT_BLACK);
      lcd_menu = 1;
      sel_cal  = 0;
    }
  }

  // ================= MENU 7 (pH cal menu) =================
  if (lcd_menu == 7) {
    tft.setCursor(0, 20);  tft.println("PH Calibration:");
    tft.setCursor(0, 60);  tft.println("PH 7:");
    tft.setCursor(0, 100); tft.println("PH 4:");
    tft.setCursor(0, 140); tft.println("Meun:");

    if (sel_ph_cal == 4) sel_ph_cal = 0;
    if ((lcd == LOW) && (lcd_menu == 7)) sel_ph_cal++;

    if (sel_ph_cal == 1) { tft.setCursor(200, 60);  tft.println("<"); tft.setCursor(200, 100); tft.println(" "); tft.setCursor(200, 140); tft.println(" "); }
    if (sel_ph_cal == 2) { tft.setCursor(200, 60);  tft.println(" "); tft.setCursor(200, 100); tft.println("<"); tft.setCursor(200, 140); tft.println(" "); }
    if (sel_ph_cal == 3) { tft.setCursor(200, 60);  tft.println(" "); tft.setCursor(200, 100); tft.println(" "); tft.setCursor(200, 140); tft.println("<"); }

    if ((lcd_up == LOW) && (sel_ph_cal == 1)) { tft.fillScreen(TFT_BLACK); lcd_menu = 15; sel_ph_cal = 0; }
    if ((lcd_up == LOW) && (sel_ph_cal == 2)) { tft.fillScreen(TFT_BLACK); lcd_menu = 16; sel_ph_cal = 0; }
    if ((lcd_up == LOW) && (sel_ph_cal == 3)) { tft.fillScreen(TFT_BLACK); lcd_menu = 3;  sel_ph_cal = 0; }
  }

  // ================= MENU 8 (EC CALIBRATION STATUS) =================
  if (lcd_menu == 8) {
    tft.setTextSize(2); tft.setTextColor(GREEN, BLACK);
    tft.setCursor(0, 20); tft.println("EC temperature cal");
    tft.setCursor(0, 60); tft.println("Use web Calibration");
    tft.setCursor(0, 100); tft.print("CF: "); tft.println(cf, 1);
    tft.setCursor(0, 140); tft.println("Menu button: back");
    if (lcd_up == LOW) {tft.fillScreen(TFT_BLACK); lcd_menu = 3; sel_cf_cal = 0;}
  }

  // ================= MENU 15 (pH7 cal) =================
  if (lcd_menu == 15) {
    tft.setCursor(0, 20);
    tft.println("PH 7 cal:");
    analogReadResolution(12);
    float Po_local = analogRead(adcPin) * 5.0f / 4095.0f;
    delay(10);
    float phValue  = (m != 0) ? (7.0f - (Vph7 - Po_local) * m) : ph;
    ph             = phValue;

    tft.setCursor(0, 60);  tft.println("PH ");
    tft.setCursor(80, 60); tft.println(ph, 1);
    tft.setCursor(0, 100); tft.println("ph7");
    tft.setCursor(80, 100); tft.println(Vph7, 2);
    tft.setCursor(0, 130); tft.println("Meun:");

    if ((lcd == LOW) && (lcd_menu == 15)) sel_ph7_cal++;
    if (sel_ph7_cal == 3) sel_ph7_cal = 0;

    if (sel_ph7_cal == 1) { tft.setCursor(220, 100); tft.println("<"); tft.setCursor(220, 130); tft.println(" "); }
    if (sel_ph7_cal == 2) { tft.setCursor(220, 100); tft.println(" "); tft.setCursor(220, 130); tft.println("<"); }

    if ((up == LOW) && (sel_ph7_cal == 1))   Vph7 = Vph7 + 0.01f;
    if ((down == LOW) && (sel_ph7_cal == 1)) Vph7 = Vph7 - 0.01f;

    if ((lcd_up == LOW) && (sel_ph7_cal == 2)) {
      tft.fillScreen(TFT_BLACK);
      lcd_menu   = 7;
      sel_ph7_cal = 0;
    }
  }

  // ================= MENU 16 (pH4 cal) =================
  if (lcd_menu == 16) {
    tft.setCursor(0, 20);
    tft.println("PH 4 cal:");
    analogReadResolution(12);
    float Po_local = analogRead(adcPin) * 5.0f / 4095.0f;
    delay(10);
    float phValue  = (m != 0) ? (7.0f - (Vph7 - Po_local) * m) : ph;
    ph             = phValue;

    tft.setCursor(0, 60);  tft.println("PH ");
    tft.setCursor(80, 60); tft.println(ph, 1);
    tft.setCursor(0, 100); tft.println("ph4");
    tft.setCursor(80, 100); tft.println(Vph4, 2);
    tft.setCursor(0, 140); tft.println("Meun:");

    if ((lcd == LOW) && (lcd_menu == 16)) sel_ph4_cal++;
    if (sel_ph4_cal == 3) sel_ph4_cal = 0;

    if (sel_ph4_cal == 1) { tft.setCursor(220, 100); tft.println("<"); tft.setCursor(220, 140); tft.println(" "); }
    if (sel_ph4_cal == 2) { tft.setCursor(220, 100); tft.println(" "); tft.setCursor(220, 140); tft.println("<"); }

    if ((up == LOW) && (sel_ph4_cal == 1))   Vph4 = Vph4 + 0.01f;
    if ((down == LOW) && (sel_ph4_cal == 1)) Vph4 = Vph4 - 0.01f;

    if ((lcd_up == LOW) && (sel_ph4_cal == 2)) {
      tft.fillScreen(TFT_BLACK);
      lcd_menu    = 7;
      sel_ph4_cal = 0;
    }
  }

  // ================= MENU 4 (Dose Settings) =================
  if (lcd_menu == 4) {
    tft.setTextSize(3);
    tft.setTextColor(GREEN, BLACK);
    tft.setCursor(0, 20);  tft.println("Dose Settings:");
    tft.setCursor(0, 60);  tft.println("PH Settings:");
    tft.setCursor(0, 100); tft.println("CF Settings:");
    tft.setCursor(0, 140); tft.println("Menu:");

    if ((lcd == LOW) && (lcd_menu == 4)) sel_dose++;

    if (sel_dose == 1) { tft.setCursor(260, 60);  tft.println("<"); tft.setCursor(260, 100); tft.println(" "); tft.setCursor(260, 140); tft.println(" "); }
    if (sel_dose == 2) { tft.setCursor(260, 60);  tft.println(" "); tft.setCursor(260, 100); tft.println("<"); tft.setCursor(260, 140); tft.println(" "); }
    if (sel_dose == 3) { tft.setCursor(260, 60);  tft.println(" "); tft.setCursor(260, 100); tft.println(" "); tft.setCursor(260, 140); tft.println("<"); }
    if (sel_dose == 4) sel_cal = 0;

    if ((lcd_up == LOW) && (sel_dose == 1)) { lcd_menu = 9;  sel_dose = 1; tft.fillScreen(TFT_BLACK); }
    if ((lcd_up == LOW) && (sel_dose == 2)) { lcd_menu = 10; sel_dose = 2; tft.fillScreen(TFT_BLACK); }
    if ((lcd_up == LOW) && (sel_dose == 3)) { tft.fillScreen(TFT_BLACK); lcd_menu = 1; sel_dose = 0; }
  }

  // ================= MENU 9 (pH dose settings) =================
  if (lcd_menu == 9) {
    tft.setTextSize(3);
    tft.setTextColor(GREEN, BLACK);
    tft.setCursor(0, 20);  tft.println("Ph Dose Time:");
    tft.setCursor(0, 60);  tft.println(ph_dose_time);
    tft.setCursor(60, 60); tft.println("Sec");
    tft.setCursor(0, 100); tft.println("PH Dose Limit:");
    tft.setCursor(0, 140); tft.println(ph_dose_max);
    tft.setCursor(0, 180); tft.println("Menu:");

    if ((lcd == LOW) && (lcd_menu == 9)) sel_ph++;

    if (sel_ph == 1) { tft.setCursor(260, 60); tft.println("<"); tft.setCursor(260, 140); tft.println(" "); tft.setCursor(260, 180); tft.println(" "); }
    if ((up == LOW) && (sel_ph == 1))   ph_dose_time = ph_dose_time + 1;
    if ((down == LOW) && (sel_ph == 1)) ph_dose_time = ph_dose_time - 1;

    if (sel_ph == 2) { tft.setCursor(260, 60); tft.println(" "); tft.setCursor(260, 140); tft.println("<"); tft.setCursor(260, 180); tft.println(" "); }
    if ((up == LOW) && (sel_ph == 2))   ph_dose_max = ph_dose_max + 1;
    if ((down == LOW) && (sel_ph == 2)) ph_dose_max = ph_dose_max - 1;

    if (sel_ph == 3) { tft.setCursor(260, 60); tft.println(" "); tft.setCursor(260, 140); tft.println(" "); tft.setCursor(260, 180); tft.println("<"); }
    if (sel_ph == 4) { sel_ph = 0; tft.setCursor(260, 60); tft.println(" "); tft.setCursor(260, 140); tft.println(" "); tft.setCursor(260, 180); tft.println(" "); }

    if ((lcd_up == LOW) && (sel_ph == 3)) { lcd_menu = 4; sel_ph = 0; tft.fillScreen(TFT_BLACK); }
  }

  // ================= MENU 10 (CF dose settings) =================
  if (lcd_menu == 10) {
    tft.setTextSize(3);
    tft.setTextColor(GREEN, BLACK);
    tft.setCursor(0, 20);  tft.println("CF Dose Time:");
    tft.setCursor(0, 60);  tft.println(cf_dose_time);
    tft.setCursor(60, 60); tft.println("Sec");
    tft.setCursor(0, 100); tft.println("CF Dose Limit:");
    tft.setCursor(0, 140); tft.println(cf_dose_max);
    tft.setCursor(0, 180); tft.println("Menu:");

    if ((lcd == LOW) && (lcd_menu == 10)) sel_cf++;

    if (sel_cf == 1) { tft.setCursor(260, 60); tft.println("<"); tft.setCursor(260, 140); tft.println(" "); tft.setCursor(260, 180); tft.println(" "); }
    if ((up == LOW) && (sel_cf == 1))   cf_dose_time = cf_dose_time + 1;
    if ((down == LOW) && (sel_cf == 1)) cf_dose_time = cf_dose_time - 1;

    if (sel_cf == 2) { tft.setCursor(260, 60); tft.println(" "); tft.setCursor(260, 140); tft.println("<"); tft.setCursor(260, 180); tft.println(" "); }
    if ((up == LOW) && (sel_cf == 2))   cf_dose_max = cf_dose_max + 1;
    if ((down == LOW) && (sel_cf == 2)) cf_dose_max = cf_dose_max - 1;

    if (sel_cf == 3) { tft.setCursor(260, 60); tft.println(" "); tft.setCursor(260, 140); tft.println(" "); tft.setCursor(260, 180); tft.println("<"); }
    if (sel_cf == 4) { sel_cf = 0; tft.setCursor(260, 60); tft.println(" "); tft.setCursor(260, 140); tft.println(" "); tft.setCursor(260, 180); tft.println(" "); }

    if ((lcd_up == LOW) && (sel_cf == 3)) { lcd_menu = 4; sel_cf = 0; tft.fillScreen(TFT_BLACK); }
  }

  // ================= MENU 5 (Force Dose) =================
  if (lcd_menu == 5) {
    tft.setTextSize(3);
    tft.setTextColor(GREEN, BLACK);
    tft.setCursor(0, 20); tft.println("Force Dose:");
    tft.setCursor(0, 60); tft.println("PH");

    if (ph_on == 1) { tft.setCursor(100, 60); tft.println("ON "); }
    if (ph_on == 0) { tft.setCursor(100, 60); tft.println("OFF"); }

    tft.setCursor(0, 100); tft.println("CF");
    if (cf_on == 1) { tft.setCursor(100, 100); tft.println("ON "); }
    if (cf_on == 0) { tft.setCursor(100, 100); tft.println("OFF"); }

    tft.setCursor(0, 180); tft.println("Menu:");

    if ((lcd == LOW) && (lcd_menu == 5)) sel_force++;
    if (sel_force == 4) sel_force = 0;

    if (sel_force == 1) { tft.setCursor(200, 60); tft.println("<"); tft.setCursor(200, 100); tft.println(" "); tft.setCursor(200, 180); tft.println(" "); }
    if (sel_force == 2) { tft.setCursor(200, 60); tft.println(" "); tft.setCursor(200, 100); tft.println("<"); tft.setCursor(200, 180); tft.println(" "); }
    if (sel_force == 3) { tft.setCursor(200, 60); tft.println(" "); tft.setCursor(200, 100); tft.println(" "); tft.setCursor(200, 180); tft.println("<"); }

    if ((up == LOW) && (sel_force == 1)) ph_on = (maintenanceMode || !clockReady) ? 0 : 1;
    if ((up == LOW) && (sel_force == 2)) cf_on = (maintenanceMode || !clockReady) ? 0 : 1;

    if ((lcd_up == LOW) && (sel_force == 3)) {
      lcd_menu  = 1;
      sel_force = 0;
      tft.fillScreen(TFT_BLACK);
    }
  }

  // ===== MENU CONTROL OPEN =====
  if ((lcd_up == LOW) && (lcd_menu == 0)) {
    lcd_menu = 1;
    tft.fillScreen(TFT_BLACK);
  }

  // ================= SEND STRUCT =================
  c = ph;
  d = cf;
  sendStruct.a = c;
  sendStruct.b = d;

  if (millis() > read_now + period) {
    read_now = millis();

    ph = getFilteredPH();
    Debug.print(F("[PERIOD] pH=")); Debug.print(ph, 2);
    Debug.print(F(" Temp=")); Debug.print(Temperature, 1);
    Debug.print(F("C ECnow=")); Debug.print(ecNow_mS, 3);
    Debug.print(F(" EC25=")); Debug.print(ec, 3);
    Debug.print(F(" CF=")); Debug.println(cf, 1);
  }

  // ================= MAINTENANCE MODE GATE =================
  if (maintenanceMode || otaInProgress || !clockReady) {
    ph_on       = 0;
    cf_on       = 0;
    ph_DoseWait = 0;
    cf_DoseWait = 0;
    ph_on_time  = 0;
    cf_on_timeA = 0;
    cf_on_timeB = 0;
    cf_on_timeC = 0;
    digitalWrite(Ph_pump,  LOW);
    digitalWrite(Cf_pumpA, LOW);
    digitalWrite(Cf_pumpB, LOW);
    phDoseActive = false;
    cfDoseActive = false;
  }

  // ===================================================
  // SIMPLE TIME-SLOT AUTO DOSING (PH + CF)
  //   - Check setpoint ONLY when minute % interval == 0
  //   - Slots like 00/10/20/30/40/50 when interval = 10
  //   - Force dose (ph_on / cf_on) still works the same
  // ===================================================

  // Common "minute of day" for both pH + CF
  uint32_t minuteOfDay = (uint32_t)now.hour() * 60u + (uint32_t)now.minute();

  // ================= pH DOSE CHECK (SIMPLE) =================
  if (ph > ph_setpoint) Dose_ON_ph = 1;
  else                  Dose_ON_ph = 0;

  int intervalPH = saneIntervalMin(phDoseIntervalMin);

  // Only arm pH auto dose on clean time slots (10/20/30/40/50/00)
  if (!maintenanceMode && clockReady &&
      autoDosePH &&
      Dose_ON_ph == 1 &&
      now.second() == 0 &&                         // exactly when the clock shows minute:00
      (now.minute() % intervalPH) == 0 &&          // 10,20,30,... based on interval
      minuteOfDay != lastPhSlotMinute &&           // one trigger per slot
      ph_DoseWait == 0 &&
      ph_dose_count < ph_dose_max) {

    lastPhSlotMinute = minuteOfDay;
    ph_DoseWait      = 1;
  }

  // pH dose run (AUTO or FORCE)
  if (!maintenanceMode && clockReady &&
      ( ((ph_DoseWait == 1 && ph > ph_setpoint && ph_dose_count <= ph_dose_max) || (ph_on == 1)) )) {

    ph_on_time++;

    if (ph_on_time > (ph_dose_time + 100)) {
      ph_on_time = ph_dose_time + 1;
    }

    if (ph_on_time <= ph_dose_time) {
      digitalWrite(Ph_pump, HIGH);
      phDoseActive = true;
    } else {
      digitalWrite(Ph_pump, LOW);
      phDoseActive = false;

      ph_DoseWait = 0;
      ph_dose_count++;
      ph_on_time = 0;
      ph_on      = 0;

      phPostCheckBase    = ph;
      phPostCheckDueMs   = millis() + POST_DOSE_CHECK_DELAY_MS;
      phPostCheckPending = true;

      Debug.print(F("[DOSE] pH dose DONE. count=")); Debug.println(ph_dose_count);
    }
  } else {
    digitalWrite(Ph_pump, LOW);
    phDoseActive = false;
    if (ph_DoseWait == 0) ph_on_time = 0;
  }

  // Invalid EC cannot arm or continue nutrient dosing, including forced dosing.
  if (!ecFresh() || otaInProgress) {
    cf_on = 0; cf_DoseWait = 0; cf_on_timeA = 0; cf_on_timeB = 0; cf_on_timeC = 0;
    cfDoseActive = false; digitalWrite(Cf_pumpA, LOW); digitalWrite(Cf_pumpB, LOW);
  }
  // ================= CF DOSE CHECK (SIMPLE) =================
  if (ecFresh() && cf < cf_setpoint && cf > 0) Dose_ON_cf = 1;
  else                            Dose_ON_cf = 0;

  int intervalCF = saneIntervalMin(cfDoseIntervalMin);

  // Only arm CF auto dose on clean time slots
  if (!maintenanceMode && clockReady &&
      autoDoseCF && ecFresh() && !otaInProgress &&
      Dose_ON_cf == 1 &&
      now.second() == 0 &&
      (now.minute() % intervalCF) == 0 &&
      minuteOfDay != lastCfSlotMinute &&
      cf_DoseWait == 0 &&
      cf_dose_count < cf_dose_max) {

    lastCfSlotMinute = minuteOfDay;
    cf_DoseWait      = 1;
  }

  // CF dose run (AUTO or FORCE)
  if (!maintenanceMode && clockReady && !otaInProgress && ecFresh() &&
      ( ((cf_DoseWait == 1 && cf < cf_setpoint && cf_dose_count <= cf_dose_max) || (cf_on == 1)) )) {

    cf_on_timeA++;

    if (cf_on_timeA > (cf_dose_time * 3 + 100)) {
      cf_on_timeA = cf_dose_time * 3 + 1;
    }

    if (cf_on_timeA <= cf_dose_time) {
      digitalWrite(Cf_pumpA, HIGH);
      digitalWrite(Cf_pumpB, LOW);
      cfDoseActive = true;
    } else {
      digitalWrite(Cf_pumpA, LOW);
      cf_on_timeC++;

      if (cf_on_timeC >= cf_dose_time) {
        cf_on_timeB++;

        if (cf_on_timeB <= cf_dose_time) {
          digitalWrite(Cf_pumpB, HIGH);
        } else {
          digitalWrite(Cf_pumpB, LOW);
          cfDoseActive = false;

          cf_DoseWait = 0;
          cf_on_timeA = 0;
          cf_on_timeB = 0;
          cf_on_timeC = 0;
          cf_dose_count++;
          cf_on = 0;

          cfPostCheckBase    = cf;
          cfPostCheckDueMs   = millis() + POST_DOSE_CHECK_DELAY_MS;
          cfPostCheckPending = true;

          Debug.print(F("[DOSE] CF dose DONE. count=")); Debug.println(cf_dose_count);
        }
      }
    }
  } else {
    digitalWrite(Cf_pumpA, LOW);
    digitalWrite(Cf_pumpB, LOW);
    cfDoseActive = false;
    if (cf_DoseWait == 0) {
      cf_on_timeA = 0;
      cf_on_timeB = 0;
      cf_on_timeC = 0;
    }
  }

  // ================= SERIAL READ (raw,temp) =================
  if (Serial1.available()) {
    String data = Serial1.readStringUntil('\n');
    data.trim();

    uint32_t raw = 0; float Temp = NAN;
    if (!BucketEC::parseSample(data.c_str(), raw, Temp)) {
      invalidateEcRaw();
      invalidateEC();
      Debug.println(F("[SERIAL1] Rejected invalid EC/temperature sample"));
      delay(50); return;
    }

    Temperature = Temp;
    raw_ec      = raw;
    raw1        = (float)raw;
    lastRawRxMs = millis();

    bool ok = getFilteredEC(raw);
    if (!ok) invalidateEC();

    Debug.print(F("[SERIAL1] Raw="));  Debug.print(raw);
    Debug.print(F(" Temp="));          Debug.print(Temp, 1); Debug.print(F("C"));
    if (ok) {
      Debug.print(F(" ECnow=")); Debug.print(ecNow_mS, 3); Debug.print(F("mS"));
      Debug.print(F(" EC25="));  Debug.print(ec, 3);       Debug.print(F("mS"));
      Debug.print(F(" CF="));    Debug.print(cf, 1);
      Debug.println(F(" (Hydro CF from EC25)"));
    } else {
      Debug.println(F(" -> EC unavailable: calibration required or reading rejected"));
    }

    // NOTE: removed debugPrintSnapshot(now, "RAW-RX") to avoid heavy output on every line
  }

  delay(1000);
}

void Task1code(void *pvParameters) {
  for (;;) {

    // ================== HTTP WEB SERVER ==================
    WiFiClient client = server.accept();
    if (client) {
      currentTime  = millis();
      previousTime = currentTime;

      Debug.println(F("New Client."));
      String currentLine = "";
      String header      = "";

      while (client.connected() && (millis() - previousTime <= (unsigned long)timeoutTime)) {
        if (client.available()) {
          char c = client.read();
          header += c;

          if (c == '\n') {
            if (currentLine.length() == 0) {

              // ------------------------------------------------
              // /debug endpoint (plain text)
              // ------------------------------------------------
              if (header.indexOf("GET /debug") >= 0) {
                ph = getFilteredPH();
                DateTime now = rtc.now();

                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: text/plain; charset=utf-8");
                client.println("Cache-Control: no-store");
                client.println("Connection: close");
                client.println();

                client.print("FW="); client.println(FW_VERSION);
                client.print("DateTime=");
                client.print(now.year()); client.print("-");
                if (now.month() < 10) client.print("0");
                client.print(now.month()); client.print("-");
                if (now.day() < 10) client.print("0");
                client.print(now.day()); client.print(" ");
                if (now.hour() < 10) client.print("0");
                client.print(now.hour()); client.print(":");
                if (now.minute() < 10) client.print("0");
                client.print(now.minute()); client.print(":");
                if (now.second() < 10) client.print("0");
                client.println(now.second());

                client.print("WiFi="); client.println(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
                client.print("RSSI="); client.println(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
                client.print("IP=");    client.println(WiFi.localIP());
                client.print("Heap=");  client.println(ESP.getFreeHeap());
                client.print("Uptime_s="); client.println((uint32_t)(millis() / 1000UL));
                client.print("ET-Core sender=");  client.println(coreSession ? "OK" : "DOWN");
                client.print("Telnet="); client.println((telnetClient && telnetClient.connected()) ? "OK" : "NONE");

                client.print("TempC=");      client.println(Temperature, 2);
                client.print("pH=");         client.println(ph, 2);
                client.print("pH_raw=");     client.println(ph_raw_live, 3);
                client.print("EC_now_mS=");  client.println(ecNow_mS, 3);
                client.print("EC25_mS=");    client.println(ec, 3);
                client.print("CF=");         client.println(cf, 1);
                client.print("EC_alpha=");   client.println(EC_ALPHA, 4);

                client.print("raw_ec=");     client.println((uint32_t)raw_ec);
                {
                  uint32_t ageS = (lastRawRxMs == 0) ? 0 : (uint32_t)((millis() - lastRawRxMs) / 1000UL);
                  client.print("raw_age_s=");  client.println(ageS);
                }

                client.print("raw14=");  client.println(ecRaw14);
                client.print("raw27=");  client.println(ecRaw27);
                client.print("EC_P=");   client.println(EC_P, 6);
                client.print("EC_K2=");  client.println(EC_K2, 2);
                client.print("ec2ok=");  client.println(ec2ptValid ? 1 : 0);
                client.print("maint=");  client.println(maintenanceMode ? 1 : 0);

                break;
              }

              // ------------------------------------------------
              // /data endpoint (JSON for web UI)
              // ------------------------------------------------
              if (header.indexOf("GET /data") >= 0) {
                ph = getFilteredPH();

                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: application/json");
                client.println("Cache-Control: no-store");
                client.println("Connection: close");
                client.println();

                client.print("{");
                client.print("\"fw\":\"");     client.print(FW_VERSION);                client.print("\",");
                client.print("\"ec_valid\":"); client.print(ecFresh() ? 1 : 0); client.print(",");
                BucketEC::Calibration reportModel;
                float reportCycles;
                portENTER_CRITICAL(&ecModelMux); reportModel = ecCalibration; reportCycles = ecLastCycles; portEXIT_CRITICAL(&ecModelMux);
                client.print("\"ec_cal_mode\":\""); client.print(BucketEC::modeName(reportModel)); client.print("\",");
                client.print("\"ec_gain\":"); jsonPrintNum(client, reportModel.gain, 6); client.print(",");
                client.print("\"ec_offset\":"); jsonPrintNum(client, reportModel.offset, 6); client.print(",");
                client.print("\"ec_cycles\":"); jsonPrintNum(client, reportCycles, 3); client.print(",");
                client.print("\"ec_cal_points\":"); client.print(reportModel.active ? 3 : 0); client.print(",");
                client.print("\"ec_pending_points\":"); client.print(reportModel.pendingMask); client.print(",");
                client.print("\"ec_calibrated\":"); client.print(BucketEC::calibrated(reportModel) ? 1 : 0); client.print(",");
                client.print("\"ec_raw_valid\":"); client.print(ecRawFresh() ? 1 : 0); client.print(",");
                BucketEC::RawWindow reportWindow; bool storageInvalid;
                portENTER_CRITICAL(&ecModelMux); reportWindow=ecRawWindow; storageInvalid=ecStorageInvalid; portEXIT_CRITICAL(&ecModelMux);
                BucketEC::Point captureSample;
                client.print("\"ec_capture_ready\":"); client.print(reportWindow.stablePoint(millis(),captureSample) ? 1 : 0); client.print(",");
                client.print("\"ec_storage_invalid\":"); client.print(storageInvalid ? 1 : 0); client.print(",");
                client.print("\"ec_cal_temp14\":"); jsonPrintNum(client, reportModel.active ? reportModel.point14.temperature : NAN, 2); client.print(",");
                client.print("\"ec_cal_temp27\":"); jsonPrintNum(client, reportModel.active ? reportModel.point27.temperature : NAN, 2); client.print(",");
                client.print("\"ph_active\":"); client.print(phDoseActive ? 1 : 0); client.print(",");
                client.print("\"cf_active\":"); client.print(cfDoseActive ? 1 : 0); client.print(",");
                client.print("\"ph_set\":"); jsonPrintNum(client, ph_setpoint, 2); client.print(",");
                client.print("\"cf_set\":"); client.print(cf_setpoint); client.print(",");
                client.print("\"ph_time\":"); client.print(ph_dose_time); client.print(",");
                client.print("\"cf_time\":"); client.print(cf_dose_time); client.print(",");
                client.print("\"ph_max\":"); client.print(ph_dose_max); client.print(",");
                client.print("\"cf_max\":"); client.print(cf_dose_max); client.print(",");
                client.print("\"sketch_md5\":\""); client.print(ESP.getSketchMD5()); client.print("\",");

                client.print("\"ph\":");       jsonPrintNum(client, ph, 2);             client.print(",");
                client.print("\"cf\":");       jsonPrintNum(client, ecFresh() ? cf : NAN, 1);             client.print(",");

                // Backwards-compat: "ec" is EC25
                client.print("\"ec\":");       jsonPrintNum(client, ecFresh() ? ec : NAN, 3);             client.print(",");
                client.print("\"ec_now\":");   jsonPrintNum(client, ecFresh() ? ecNow_mS : NAN, 3);       client.print(",");
                client.print("\"ec25\":");     jsonPrintNum(client, ecFresh() ? ec : NAN, 3);             client.print(",");

                client.print("\"temp\":");     jsonPrintNum(client, Temperature, 1);    client.print(",");

                client.print("\"vph4\":");     jsonPrintNum(client, Vph4, 3);           client.print(",");
                client.print("\"vph7\":");     jsonPrintNum(client, Vph7, 3);           client.print(",");

                client.print("\"ph_dose\":");  jsonPrintInt(client, ph_dose_count);     client.print(",");
                client.print("\"cf_dose\":");  jsonPrintInt(client, cf_dose_count);     client.print(",");

                client.print("\"raw_ec\":");   jsonPrintU32(client, (uint32_t)raw_ec);  client.print(",");
                client.print("\"ecraw14\":");  jsonPrintU32(client, (uint32_t)ecRaw14); client.print(",");
                client.print("\"ecraw27\":");  jsonPrintU32(client, (uint32_t)ecRaw27); client.print(",");

                client.print("\"ec_p\":");     jsonPrintNum(client, EC_P, 6);           client.print(",");
                client.print("\"ec_k2\":");    jsonPrintNum(client, EC_K2, 2);          client.print(",");
                client.print("\"ec2ok\":");    client.print(BucketEC::calibrated(reportModel) ? "1" : "0");    client.print(",");

                client.print("\"ec_alpha\":"); jsonPrintNum(client, EC_ALPHA, 4);       client.print(",");

                client.print("\"phauto\":");   client.print(autoDosePH ? "1" : "0");    client.print(",");
                client.print("\"cfauto\":");   client.print(autoDoseCF ? "1" : "0");    client.print(",");
                client.print("\"phint\":");    jsonPrintInt(client, phDoseIntervalMin); client.print(",");
                client.print("\"cfint\":");    jsonPrintInt(client, cfDoseIntervalMin); client.print(",");

                client.print("\"maint\":");    client.print(maintenanceMode ? "1" : "0");  client.print(",");

                client.print("\"rssi\":");     jsonPrintInt(client, (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0); client.print(",");
                client.print("\"heap\":");     jsonPrintU32(client, (uint32_t)ESP.getFreeHeap()); client.print(",");
                client.print("\"uptime_s\":"); jsonPrintU32(client, (uint32_t)(millis() / 1000UL)); client.print(",");

                {
                  uint32_t ageS = (lastRawRxMs == 0) ? 0 : (uint32_t)((millis() - lastRawRxMs) / 1000UL);
                  client.print("\"raw_age_s\":"); jsonPrintU32(client, ageS); client.print(",");
                }

                client.print("\"wifi_power_save\":"); client.print(WiFi.getSleep()!=WIFI_PS_NONE ? "1" : "0"); client.print(",");
                client.print("\"clock_valid\":"); client.print(clockReady ? "1" : "0"); client.print(",");
                client.print("\"etcore_sender_ready\":");   client.print(coreSession ? "1" : "0"); client.print(",");
                client.print("\"telnet\":"); client.print((telnetClient && telnetClient.connected()) ? "1" : "0"); client.print(",");

                client.print("\"ph_post_pending\":"); client.print(phPostCheckPending ? "1" : "0"); client.print(",");
                client.print("\"ph_post_base\":");    jsonPrintNum(client, phPostCheckBase, 2); client.print(",");

                {
                  long phDue = phPostCheckPending ? (long)((int32_t)(phPostCheckDueMs - millis()) / 1000L) : 0L;
                  if (phDue < 0) phDue = 0;
                  client.print("\"ph_post_due_s\":");   jsonPrintInt(client, (int)phDue); client.print(",");
                }

                client.print("\"cf_post_pending\":"); client.print(cfPostCheckPending ? "1" : "0"); client.print(",");
                client.print("\"cf_post_base\":");    jsonPrintNum(client, cfPostCheckBase, 2); client.print(",");

                {
                  long cfDue = cfPostCheckPending ? (long)((int32_t)(cfPostCheckDueMs - millis()) / 1000L) : 0L;
                  if (cfDue < 0) cfDue = 0;
                  client.print("\"cf_post_due_s\":");   jsonPrintInt(client, (int)cfDue);
                }

                client.print("}");
                break;
              }

              // ------------------------------------------------
              // force dose from web
              // ------------------------------------------------
              if (header.indexOf("GET /?force_ph=1") >= 0) { 
                if (!maintenanceMode && clockReady && !otaInProgress) ph_on = 1; 
              }
              if (header.indexOf("GET /?force_cf=1") >= 0) { 
                if (!maintenanceMode && clockReady && !otaInProgress && ecFresh()) cf_on = 1; 
              }

              // ------------------------------------------------
              // settings / calibration via query string
              // ------------------------------------------------
              if (header.indexOf("GET /?") >= 0) {
                handleRequest(header, client);
                break;
              }

              // ------------------------------------------------
              // MAIN PAGE (full HTML / CSS / JS)
              // ------------------------------------------------
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              // ======= HTML HEAD + STYLE =======
              client.println("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
              client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
              client.println("<title>Hydroponics EC/pH 2.0 Controller</title>");

              client.println("<style>");
              client.println(":root{--bg:#060a09;--panel:rgba(14,22,18,.92);--panel2:rgba(12,18,15,.94);--border:rgba(70,200,140,.18);--text:#d5ffea;--green:#46c88c;--white:#ffffff;--shadow:0 12px 34px rgba(0,0,0,.65);--radius:16px;}");
              client.println("*{box-sizing:border-box}");
              client.println("body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;color:var(--text);background:radial-gradient(1200px 650px at 15% 0%, rgba(70,200,140,.18), transparent 62%),radial-gradient(1200px 650px at 85% 0%, rgba(70,200,140,.14), transparent 62%),radial-gradient(1100px 700px at 50% 100%, rgba(70,200,140,.12), transparent 68%),linear-gradient(180deg, #0f1c17 0%, #08110e 55%, #060a09 100%);background-color:var(--bg);min-height:100vh;}");
              client.println("@keyframes fadeUp{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:translateY(0)}}.fade{animation:fadeUp .45s ease-out both;}");
              client.println(".wrap{max-width:1120px;margin:0 auto;padding:14px 10px 22px;}.top{display:flex;flex-wrap:wrap;gap:10px;align-items:flex-end;justify-content:space-between;margin-bottom:8px;}");
              client.println("h1{margin:0;font-size:20px;letter-spacing:.35px;color:var(--white);}h1 .g{color:var(--green);text-shadow:0 0 10px rgba(70,200,140,.18);}#clock{font-size:12px;color:rgba(213,255,234,.75);}");
              client.println(".stats{display:grid;grid-template-columns:repeat(12,1fr);gap:8px;margin-top:10px;}.stat{grid-column:span 6;background:linear-gradient(180deg,var(--panel) 0%, rgba(7,10,9,.98) 100%);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);padding:7px;position:relative;overflow:hidden;min-height:74px;transition:transform .15s ease, box-shadow .15s ease, border-color .15s ease;}.stat:before{content:'';position:absolute;inset:0;background:radial-gradient(650px 120px at 18% 0%, rgba(70,200,140,.10), transparent 60%);pointer-events:none;}.stat:hover{border-color:rgba(70,200,140,.32);box-shadow:0 14px 40px rgba(0,0,0,.72), 0 0 18px rgba(70,200,140,.07);} .stat .k{font-size:10px;letter-spacing:.45px;text-transform:uppercase;color:rgba(213,255,234,.65);margin-bottom:4px;} .stat .v{font-size:22px;font-weight:900;color:var(--text);text-shadow:0 0 14px rgba(70,200,140,.10);line-height:1.05;} .stat .u{margin-top:2px;font-size:10px;color:rgba(213,255,234,.55);} @media(min-width:780px){.stat{grid-column:span 3;}}");
              client.println(".split{display:grid;grid-template-columns:repeat(12,1fr);gap:10px;margin-top:10px;}.card{grid-column:span 12;background:linear-gradient(180deg,var(--panel2) 0%, rgba(7,10,9,.98) 100%);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);padding:12px;}@media(min-width:780px){.half{grid-column:span 6;}}.card h2{margin:0 0 10px 0;font-size:13px;letter-spacing:.35px;text-transform:uppercase;color:rgba(213,255,234,.82);}");
              client.println(".field{display:flex;gap:8px;align-items:center;flex-wrap:wrap;justify-content:center;}label{font-weight:800;font-size:12px;color:rgba(213,255,234,.78);}");
              client.println("input[type='number'],select{width:132px;padding:10px 10px;border-radius:14px;border:1px solid rgba(70,200,140,.22);background:rgba(0,0,0,.45);color:var(--text);font-size:16px;text-align:center;outline:none;}input[type='number']:focus,select:focus{border-color:rgba(70,200,140,.45);box-shadow:0 0 0 4px rgba(70,200,140,.10);}input[readonly]{opacity:.96}");
              client.println("button{min-height:38px;min-width:80px;padding:10px 12px;border-radius:14px;border:1px solid rgba(70,200,140,.20);background:linear-gradient(180deg, rgba(70,200,140,.18), rgba(70,200,140,.08));color:var(--text);font-weight:900;font-size:13px;letter-spacing:.2px;cursor:pointer;text-shadow:0 0 10px rgba(70,200,140,.14);transition:filter .15s ease, transform .05s ease, border-color .15s ease;}button:hover{border-color:rgba(70,200,140,.36);filter:brightness(1.06);}button:active{transform:translateY(1px);} .btnDanger{color:#ffd1d1;border-color:rgba(255,80,80,.26);background:linear-gradient(180deg, rgba(255,80,80,.18), rgba(255,80,80,.08));}.btnDanger:hover{border-color:rgba(255,80,80,.40);} .btnOff{opacity:.75;border-color:rgba(213,255,234,.18);background:linear-gradient(180deg, rgba(213,255,234,.10), rgba(213,255,234,.06));}");
              client.println(".muted{font-size:12px;color:rgba(213,255,234,.65);text-align:center;}hr.sep{border:0;height:1px;background:linear-gradient(90deg, transparent, rgba(70,200,140,.22), transparent);margin:12px 0;}");
              client.println("</style></head><body>");

              // ======= BODY / CARDS =======
              client.println("<div class='wrap fade'><div class='top'><div><h1>Hydroponics <span class='g'>EC</span> / <span class='g'>pH</span> 2.0</h1></div><div id='clock'></div></div>");

              client.println("<div class='stats'>");

              client.println("<div class='stat fade'><div class='k'>pH</div><div class='v' id='ph'>--</div><div class='u'>filtered</div></div>");
              client.println("<div class='stat fade'><div class='k'>CF</div><div class='v' id='cf'>--</div><div class='u'>Hydro CF (from EC25)</div></div>");
              client.println("<div class='stat fade'><div class='k'>EC25</div><div class='v' id='ec25'>--</div><div class='u'>mS/cm @25 C (used)</div></div>");
              client.println("<div class='stat fade'><div class='k'>EC Now</div><div class='v' id='ec_now'>--</div><div class='u'>mS/cm (temp)</div></div>");
              client.println("<div class='stat fade'><div class='k'>Temp</div><div class='v' id='temp'>--</div><div class='u'>C</div></div>");
              client.println("<div class='stat fade'><div class='k'>RAW EC</div><div class='v' id='raw_ec'>--</div><div class='u'>live</div></div>");
              client.println("<div class='stat fade'><div class='k'>pH Dose</div><div class='v' id='ph_dose'>--</div><div class='u'>today</div></div>");
              client.println("<div class='stat fade'><div class='k'>CF Dose</div><div class='v' id='cf_dose'>--</div><div class='u'>today</div></div>");

              client.println("</div>");

              client.println("<div class='split'>");

              client.println("<div class='card half fade'><h2>Live Sensors / Cal Info</h2>");
              client.println("<div class='field'><label>Live pH4 V</label><input id='vph4_live' type='number' readonly></div><br>");
              client.println("<div class='field'><label>Live pH7 V</label><input id='vph7_live' type='number' readonly></div>");
              client.println("<hr class='sep'>");
              client.println("<div class='muted'><b>EC temperature calibration</b><br><span id='ec2ok'>--</span><br>Legacy slope=<span id='ec_p'>--</span> &nbsp; b=<span id='ec_k2'>--</span><br>alpha=<span id='ec_alpha'>--</span></div>");
              client.println("</div>");

              client.println("<div class='card half fade'><h2>Setpoints / Dose Settings</h2>");
              client.println("<div class='field'><label>pH Set</label><input id='ph_set_input' type='number' step='0.1'><button onclick='setPh()'>Set</button></div>");
              client.print("<div class='muted'>Current: <span id='ph_set_display'>"); client.print(String(ph_setpoint, 2)); client.println("</span></div><br>");
              client.println("<div class='field'><label>CF Set</label><input id='cf_set_input' type='number' step='1'><button onclick='setCf()'>Set</button></div>");
              client.print("<div class='muted'>Current: <span id='cf_set_display'>"); client.print(String(cf_setpoint)); client.println("</span></div><br>");
              client.println("<div class='field'><label>pH Dose (s)</label><input id='ph_time_input' type='number' min='1' max='120'><button onclick='setPhTime()'>Set</button></div>");
              client.print("<div class='muted'>Current: <span id='ph_time_display'>"); client.print(String(ph_dose_time)); client.println(" sec</span></div><br>");
              client.println("<div class='field'><label>CF Dose (s)</label><input id='cf_time_input' type='number' min='1' max='120'><button onclick='setCfTime()'>Set</button></div>");
              client.print("<div class='muted'>Current: <span id='cf_time_display'>"); client.print(String(cf_dose_time)); client.println(" sec</span></div>");
              client.println("<hr class='sep'><div class='muted'><b>Auto Dosing</b> (Force Dose disabled during Maintenance)</div><br>");
              client.println("<div class='field'><label>Auto pH</label><button id='btn_phauto' onclick='togglePhAuto()'>--</button><label>Dose Interval</label>");
              client.println("<select id='ph_interval' onchange='setPhInterval()'><option value='10'>10</option><option value='20'>20</option><option value='30'>30</option><option value='40'>40</option><option value='50'>50</option><option value='60'>60</option></select><span class='muted'>min</span></div><br>");
              client.println("<div class='field'><label>Auto CF</label><button id='btn_cfauto' onclick='toggleCfAuto()'>--</button><label>Dose Interval</label>");
              client.println("<select id='cf_interval' onchange='setCfInterval()'><option value='10'>10</option><option value='20'>20</option><option value='30'>30</option><option value='40'>40</option><option value='50'>50</option><option value='60'>60</option></select><span class='muted'>min</span></div>");
              client.println("</div>");

              client.println("<div class='card fade'><h2>Calibration</h2>");
              client.print("<div class='field'><label>pH4 Volt</label><button onclick=\"adjustValue('vph4_input',-0.01,3)\">-</button><input id='vph4_input' type='number' step='0.01' value='"); jsonPrintNum(client, Vph4, 3); client.print("'>");
              client.print("<button onclick=\"adjustValue('vph4_input',0.01,3)\">+</button><button onclick='useLiveVph4()'>Use Live</button><button onclick='setVph4()'>Set</button></div><br>");
              client.print("<div class='field'><label>pH7 Volt</label><button onclick=\"adjustValue('vph7_input',-0.01,3)\">-</button><input id='vph7_input' type='number' step='0.01' value='"); jsonPrintNum(client, Vph7, 3); client.print("'>");
              client.print("<button onclick=\"adjustValue('vph7_input',0.01,3)\">+</button><button onclick='useLiveVph7()'>Use Live</button><button onclick='setVph7()'>Set</button></div>");
              client.println("<hr class='sep'><div class='muted'><b>EC temperature calibration</b><br>Enable Maintenance before calibrating. Keep both probes in the same mixed water.</div><br>");
              client.println("<div class='field'><button onclick='captureEc14()'>Capture 1.413 mS standard</button><button onclick='captureEc27()'>Capture 2.770 mS standard</button></div>");
              client.println("<div class='muted'>Use each labelled standard, with both probes together. Wait for stable readings before capture. Each point saves its own temperature. A new calibration needs both points.</div><div id='ec_capture_status' class='muted'>Waiting for sensor</div><br>");
              client.print("<div class='muted'>Saved original calibration: 1.413 mS raw "); client.print(ecRaw14); client.print("; 2.770 mS raw "); client.print(ecRaw27); client.println(". These values are preserved.</div>");
              client.println("<div class='muted'>To reuse these saved points, enter the actual water temperature during each original calibration. Leave blank if unknown; the solution label's 25 C is not the capture temperature.</div>");
              client.println("<div class='field'><label>Original 1.413 temperature C</label><input id='ec_legacy_temp14' type='number' step='0.1'><label>Original 2.770 temperature C</label><input id='ec_legacy_temp27' type='number' step='0.1'><button onclick='reuseEcLegacy()'>Use saved two-point calibration</button></div>");
              client.print("<input id='ec14raw_input' type='hidden' value='"); client.print(ecRaw14); client.println("'>");
              client.print("<input id='ec27raw_input' type='hidden' value='"); client.print(ecRaw27); client.println("'>");
              client.println("<hr class='sep'>");
              client.print("<div class='field'><label>pH Dose Max</label><input id='ph_dose_max_input' type='number' min='0' max='999' value='"); client.print(String(ph_dose_max)); client.print("'>");
              client.println("<button onclick='setPhMax()'>Set</button></div><br>");
              client.print("<div class='field'><label>CF Dose Max</label><input id='cf_dose_max_input' type='number' min='0' max='999' value='"); client.print(String(cf_dose_max)); client.print("'>");
              client.println("<button onclick='setCfMax()'>Set</button></div>");
              client.println("</div>");

              client.println("<div class='card fade'><h2>Actions</h2><div class='field'>");
              client.println("<button onclick='forcePhDose()'>Force Dose pH</button>");
              client.println("<button onclick='forceCfDose()'>Force Dose CF</button>");
              client.println("<button id='btn_maint' class='btnOff' onclick='toggleMaint()'>Maintenance: --</button>");
              client.println("<button class='btnDanger' onclick='saveCalibration()'>Save to EEPROM</button>");
              client.println("</div><div class='muted' style='margin-top:10px;'>When Maintenance is ON, all dosing is disabled (auto + force).</div>");
              client.println("<div class='muted' style='margin-top:12px;'>© electronicstech.co.nz &nbsp;&bull;&nbsp; Firmware 8.9</div></div>");
              client.println("</div></div>");

              // ======= JS =======
              client.println("<script>");
              client.println("function updateClock(){const now=new Date();document.getElementById('clock').textContent=now.toLocaleString();}setInterval(updateClock,1000);updateClock();");
              client.println("let lastRaw=0;let liveVph4=0;let liveVph7=0;let phAuto=1, cfAuto=1;let maint=0;");
              client.println("function paintAutoButtons(){const b1=document.getElementById('btn_phauto');const b2=document.getElementById('btn_cfauto');b1.textContent=phAuto? 'ON':'OFF';b2.textContent=cfAuto? 'ON':'OFF';b1.className = phAuto? '' : 'btnOff';b2.className = cfAuto? '' : 'btnOff';const bm=document.getElementById('btn_maint');if(bm){bm.textContent = maint ? 'Maintenance: ON' : 'Maintenance: OFF';bm.className = maint ? 'btnDanger' : 'btnOff';}}");
              client.println("function fetchData(){fetch('/data').then(r=>r.json()).then(d=>{");
              client.println("document.getElementById('ph').textContent=Number(d.ph||0).toFixed(2);document.getElementById('cf').textContent=d.ec_valid==1?Number(d.cf).toFixed(1):'Unavailable';");
              client.println("document.getElementById('ec25').textContent=(d.ec_valid==1?Number(d.ec25).toFixed(3):'Unavailable');document.getElementById('ec_now').textContent=(d.ec_valid==1?Number(d.ec_now).toFixed(3):'Unavailable');");
              client.println("document.getElementById('temp').textContent=Number(d.temp||0).toFixed(1);document.getElementById('ph_dose').textContent=d.ph_dose||0;document.getElementById('cf_dose').textContent=d.cf_dose||0;");
              client.println("lastRaw=d.raw_ec||0; document.getElementById('raw_ec').textContent=lastRaw;");
              client.println("liveVph4=Number(d.vph4||0);liveVph7=Number(d.vph7||0);document.getElementById('vph4_live').value=liveVph4.toFixed(3);document.getElementById('vph7_live').value=liveVph7.toFixed(3);");
              client.println("document.getElementById('ec_p').textContent=Number(d.ec_p||0).toFixed(6);document.getElementById('ec_k2').textContent=Number(d.ec_k2||0).toFixed(2);document.getElementById('ec2ok').textContent=d.ec_storage_invalid?'Stored EC calibration invalid':(d.ec_cal_mode||'calibration-required');document.getElementById('ec_capture_status').textContent=d.ec_capture_ready?'Stable: ready to capture':(d.ec_raw_valid?'Waiting for stable temperature and sensor readings':'Waiting for fresh sensor readings');");
              client.println("document.getElementById('ec_alpha').textContent=Number(d.ec_alpha||0.019).toFixed(4);");
              client.println("phAuto = (d.phauto==1);cfAuto = (d.cfauto==1);maint = (d.maint==1);paintAutoButtons();if(d.phint){document.getElementById('ph_interval').value=String(d.phint);}if(d.cfint){document.getElementById('cf_interval').value=String(d.cfint);}}).catch(e=>console.log('JSON error',e));}");
              client.println("setInterval(fetchData,2500); window.onload=fetchData;");
              client.println("function setPh(){const v=document.getElementById('ph_set_input').value;fetch('/?phwebset='+encodeURIComponent(v)).then(()=>{document.getElementById('ph_set_display').textContent=v;});}");
              client.println("function setCf(){const v=document.getElementById('cf_set_input').value;fetch('/?cfwebset='+encodeURIComponent(v)).then(()=>{document.getElementById('cf_set_display').textContent=v;});}");
              client.println("function setPhTime(){const v=document.getElementById('ph_time_input').value;fetch('/?phwebtime='+encodeURIComponent(v)).then(()=>{document.getElementById('ph_time_display').textContent=v+' sec';});}");
              client.println("function setCfTime(){const v=document.getElementById('cf_time_input').value;fetch('/?cfwebtime='+encodeURIComponent(v)).then(()=>{document.getElementById('cf_time_display').textContent=v+' sec';});}");
              client.println("function togglePhAuto(){phAuto = phAuto?0:1; fetch('/?phauto='+phAuto).then(fetchData);}function toggleCfAuto(){cfAuto = cfAuto?0:1; fetch('/?cfauto='+cfAuto).then(fetchData);}");
              client.println("function setPhInterval(){const v=document.getElementById('ph_interval').value; fetch('/?phinterval='+encodeURIComponent(v));}function setCfInterval(){const v=document.getElementById('cf_interval').value; fetch('/?cfinterval='+encodeURIComponent(v));}");
              client.println("function toggleMaint(){maint = maint?0:1; fetch('/?maint='+maint).then(fetchData);}");
              client.println("function setVph4(){const v=document.getElementById('vph4_input').value;fetch('/?vph4='+encodeURIComponent(v)).then(fetchData);}function setVph7(){const v=document.getElementById('vph7_input').value;fetch('/?vph7='+encodeURIComponent(v)).then(fetchData);}");
              client.println("function useLiveVph4(){document.getElementById('vph4_input').value=Number(liveVph4||0).toFixed(3);}function useLiveVph7(){document.getElementById('vph7_input').value=Number(liveVph7||0).toFixed(3);}");
              client.println("function setPhMax(){const v=document.getElementById('ph_dose_max_input').value;fetch('/?phmax='+encodeURIComponent(v));}function setCfMax(){const v=document.getElementById('cf_dose_max_input').value;fetch('/?cfmax='+encodeURIComponent(v));}");
              client.println("function setEc14Raw(){const v=document.getElementById('ec14raw_input').value;fetch('/?ec14raw='+encodeURIComponent(v));}function setEc27Raw(){const v=document.getElementById('ec27raw_input').value;fetch('/?ec27raw='+encodeURIComponent(v));}");
              client.println("function ecCommand(q){fetch('/?'+q).then(async r=>{const msg=await r.text();alert(msg);fetchData();});}function reuseEcLegacy(){const a=document.getElementById('ec_legacy_temp14').value,b=document.getElementById('ec_legacy_temp27').value;if(!a.trim()||!b.trim()){alert('Enter both original capture temperatures, or capture fresh standards.');return;}ecCommand('ec_legacy_temp14='+encodeURIComponent(a)+'&ec_legacy_temp27='+encodeURIComponent(b));}function captureEc14(){ecCommand('ec_capture=1');}function captureEc27(){ecCommand('ec_capture=2');}");
              client.println("function forcePhDose(){fetch('/?force_ph=1');}function forceCfDose(){fetch('/?force_cf=1');}");
              client.println("function saveCalibration(){const v4=document.getElementById('vph4_input').value;const v7=document.getElementById('vph7_input').value;const phm=document.getElementById('ph_dose_max_input').value;const cfm=document.getElementById('cf_dose_max_input').value;const r14=document.getElementById('ec14raw_input').value;const r27=document.getElementById('ec27raw_input').value;const pint=document.getElementById('ph_interval').value;const cint=document.getElementById('cf_interval').value;");
              client.println("fetch('/?vph4='+encodeURIComponent(v4)+'&vph7='+encodeURIComponent(v7)+'&phmax='+encodeURIComponent(phm)+'&cfmax='+encodeURIComponent(cfm)+'&ec14raw='+encodeURIComponent(r14)+'&ec27raw='+encodeURIComponent(r27)+'&phauto='+(phAuto?1:0)+'&cfauto='+(cfAuto?1:0)+'&phinterval='+encodeURIComponent(pint)+'&cfinterval='+encodeURIComponent(cint)+'&maint='+(maint?1:0)+'&save=1').then(()=>alert('Saved to EEPROM'));}");
              client.println("function adjustValue(id,step,dp){const input=document.getElementById(id);let v=parseFloat(input.value)||0;v+=step;input.value=v.toFixed(dp||2);}");
              client.println("</script></body></html>");
              // ======= END MAIN PAGE =======

              break;

            } else {
              currentLine = "";
            }
          } else if (c != '\r') {
            currentLine += c;
          }

          previousTime = millis();
        }
      }

      // Done with this client
      client.stop();
    }

    // ================== ET-CORE + BACKGROUND TASKS ==================
    serviceCore();

    // pH post-dose verification window
    if (phPostCheckPending && (int32_t)(millis() - phPostCheckDueMs) >= 0) {
      phPostCheckPending = false;
    }

    // CF post-dose verification window
    if (cfPostCheckPending && (int32_t)(millis() - cfPostCheckDueMs) >= 0) {
      cfPostCheckPending = false;
    }

    // Small delay to avoid hammering CPU
    delay(50);
  }
}


bool ecFresh() {
  portENTER_CRITICAL(&ecModelMux);
  const bool ready = ecPublishedGeneration == ecModelGeneration &&
    BucketEC::fresh(ecSampleValid, ecGoodSamples, millis(), lastValidEcMs, EC_STALE_MS);
  portEXIT_CRITICAL(&ecModelMux);
  return ready;
}
void invalidateEC() {
  // Stop nutrient outputs immediately, including a rejection after the dose loop.
  cf_on = 0; cf_DoseWait = 0; cf_on_timeA = 0; cf_on_timeB = 0; cf_on_timeC = 0;
  cfDoseActive = false;
  digitalWrite(Cf_pumpA, LOW); digitalWrite(Cf_pumpB, LOW);
  ecSampleValid = false; ecGoodSamples = 0;
  ecFilterInitialized = false; cfMqttInitialized = false;
}
bool ecRawFresh() {
  portENTER_CRITICAL(&ecModelMux);
  const bool ready=ecRawWindow.freshRaw(millis());
  portEXIT_CRITICAL(&ecModelMux);return ready;
}
void invalidateEcRaw() {
  portENTER_CRITICAL(&ecModelMux);ecRawWindow.reset();portEXIT_CRITICAL(&ecModelMux);
}
void loadEcCalibration() {
  // Deliberately no migration of the ecfix4/ecfix5 tank-derived calibration.
  // Legacy EEPROM points remain intact and can be reused with known capture T.
  Preferences prefs;
  if(!prefs.begin("bucket_ec6",true))return;
  BucketEC::Calibration saved={};const bool present=prefs.isKey("model");
  const bool found=prefs.getBytesLength("model")==sizeof(saved)&&prefs.getBytes("model",&saved,sizeof(saved))==sizeof(saved);
  prefs.end();
  if(found&&BucketEC::valid(saved))ecCalibration=saved;
  else if(present){ecStorageInvalid=true;Debug.println(F("[EC] Invalid stored calibration; fresh two-point calibration required"));}
}
bool handleEcCalibration(const String &header,WiFiClient &client) {
  const String capture=getParam(header,"ec_capture");
  const String t14=getParam(header,"ec_legacy_temp14"),t27=getParam(header,"ec_legacy_temp27");
  const String oldReference=getParam(header,"ec_reference_cf");
  const String restoreCf=getParam(header,"restore_cf_count"),restorePh=getParam(header,"restore_ph_count");
  const bool legacyRequest=header.indexOf("ec_legacy_temp14=")>=0||header.indexOf("ec_legacy_temp27=")>=0;
  if(!capture.length()&&!legacyRequest&&!oldReference.length()&&!restoreCf.length()&&!restorePh.length())return false;
  const char *message="Enable Maintenance before changing EC calibration or restoring counters.";
  bool ok=false;
  if(maintenanceMode&&!otaInProgress) {
    if(restoreCf.length()||restorePh.length()) {
      // Counter restoration does not require an already calibrated EC sensor.
      // It only restores bookkeeping while outputs are disabled.
      const int nextCf=restoreCf.length()?restoreCf.toInt():cf_dose_count;
      const int nextPh=restorePh.length()?restorePh.toInt():ph_dose_count;
      if(nextCf>=cf_dose_count&&nextCf<=cf_dose_max&&nextPh>=ph_dose_count&&nextPh<=ph_dose_max){
        cf_dose_count=nextCf;ph_dose_count=nextPh;ok=true;message="Dose counters restored.";
      }
    } else if(oldReference.length()) {
      message="A single tank reading cannot replace the two-point calibration. Use both standards or the saved original points with their capture temperatures.";
    } else {
      BucketEC::Calibration next;BucketEC::RawWindow window;
      portENTER_CRITICAL(&ecModelMux);next=ecCalibration;window=ecRawWindow;portEXIT_CRITICAL(&ecModelMux);
      if(legacyRequest) {
        float a,b;
        ok=BucketEC::parseTemperature(t14.c_str(),a)&&BucketEC::parseTemperature(t27.c_str(),b)&&
           BucketEC::reuseLegacy(next,float(ecRaw14),a,float(ecRaw27),b);
        message="Original points rejected: provide both actual capture temperatures and a consistent two-point pair.";
      } else {
        BucketEC::Point sample;
        ok=(capture=="1"||capture=="2")&&window.stablePoint(millis(),sample)&&
           BucketEC::capturePoint(next,unsigned(capture.toInt()),sample.senderRaw,sample.temperature);
        message="Capture rejected: allow at least 8 fresh samples with stable temperature and sensor readings; check the selected standard.";
      }
      if(ok) {
        Preferences prefs;ok=prefs.begin("bucket_ec6",false);
        if(ok){ok=prefs.putBytes("model",&next,sizeof(next))==sizeof(next);prefs.end();}
        if(ok) {
          portENTER_CRITICAL(&ecModelMux);
          ecCalibration=next;ecModelGeneration++;ecSampleValid=false;ecStorageInvalid=false;
          ecRawWindow.reset(); // Require a complete new stability window for the next point.
          portEXIT_CRITICAL(&ecModelMux);
          message=next.pendingMask?"First standard saved with temperature. Place both probes in the other standard and wait for stable readings before capturing it.":"Two-point calibration saved with both capture temperatures. Return both probes to the tank before leaving Maintenance.";
        } else message="Calibration was not saved: storage error. Existing calibration remains active.";
      }
    }
  }
  client.println(ok?"HTTP/1.1 200 OK":"HTTP/1.1 409 Conflict");
  client.println("Content-Type: text/plain");client.println("Connection: close");client.println();client.println(message);
  return true;
}
