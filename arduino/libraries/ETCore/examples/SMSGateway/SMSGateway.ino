/* ESP32 Dev Module + existing SIM7600SA-H wiring.
 * Encrypted ET-Core transport with durable request reservations/event outbox.
 * Existing modem setup/recovery is preserved. OTA waits for an active SMS
 * submission to finish. Private settings are in gateway_secrets.h.
 */
#define TINY_GSM_MODEM_SIM7600

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <atomic>
#include "gateway_secrets.h"
#include "SmsTransport.h"
#include <TinyGsmClient.h>
#include <HardwareSerial.h>

//-------------------- Wi-Fi Settings ---------------------



//-------------------- SIM7600 Setup ----------------------
// Modem pin definitions
#define MODEM_TX      16  
#define MODEM_RX      17  
#define MODEM_PWRKEY   4  
#define MODEM_RST      5  

HardwareSerial SerialAT(1);
TinyGsm        modem(SerialAT);
TinyGsmClient  gsmClient(modem);

static constexpr char FIRMWARE_VERSION[] = "sim7600-etcore-2026.11.08.1";
static constexpr char OTA_HOSTNAME[] = "sim7600-sms-gateway";
static SemaphoreHandle_t updateGate = nullptr;
static std::atomic<bool> otaActive{false};
static TaskHandle_t otaTaskHandle = nullptr;

// ET-Core and OTA share a dedicated task; the existing modem code stays on loop().
static void otaService(void*) {
  SmsTransport::run(updateGate, otaActive, OTA_HOSTNAME, ota_password, FIRMWARE_VERSION);
}

static void safeRestart() {
  if (xSemaphoreTake(updateGate, pdMS_TO_TICKS(100)) == pdTRUE) {
    ESP.restart();
    xSemaphoreGive(updateGate);
  } else {
    Serial.println("[OTA] Reboot deferred while updating or sending SMS");
  }
}

struct UpdateGuard {
  bool held;
  UpdateGuard() : held(xSemaphoreTake(updateGate, 0) == pdTRUE) {}
  ~UpdateGuard() { if (held) xSemaphoreGive(updateGate); }
};

// Forward declarations
bool reconnectSMS(); 
bool gprsReconnect();
void connectWiFi();

void processSmsRequest(const SmsTransport::Request& request);
void readModemUnsolicited();
String extractJsonValue(String json, String key);

//--------------------------------------------
// Setup
//--------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("\n=== ESP32 + SIM7600SA-H + Encrypted ET-Core ===\n");

  Serial.printf("[BUILD] %s\n", FIRMWARE_VERSION);
  updateGate = xSemaphoreCreateMutex();
  if (!updateGate) {
    Serial.println("[FATAL] Could not allocate gateway buffers");
    return;
  }
  if (!SmsTransport::init()) { Serial.println("[FATAL] Queue allocation failed"); return; }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setAutoReconnect(true);
  if (xTaskCreatePinnedToCore(otaService, "gatewayOTA", 8192, nullptr, 1,
                            &otaTaskHandle, 0) != pdPASS) {
    Serial.println("[FATAL] OTA task creation failed");
    otaTaskHandle = nullptr;
    return;
  }

  // 1) Connect to Wi-Fi
  connectWiFi();

  // Network task starts encrypted ET-Core when Wi-Fi becomes available.

  // 3) Initialize SIM7600
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);

  // Power on the modem
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);

  Serial.println("Initializing SIM7600SA-H with TinyGsm...");
  delay(3000);

  // Restart
  Serial.println("Restarting modem...");
  modem.restart();
  Serial.println("Modem restarted!");

  // Network mode configuration
  Serial.println("Setting legacy network mode => AT+CNMP=38");
  modem.sendAT("+CNMP=38");
  delay(1000);

  // Preserve the final network-mode selection from the working sketch.
  Serial.println("Selecting final network mode => AT+CNMP=2");
  modem.sendAT("+CNMP=2");
  delay(1000);

  // Enable Cat-M1
  Serial.println("Enabling Cat-M1 => AT+CMNB=1");
  modem.sendAT("+CMNB=1");
  delay(1000);

  // Print modem info
  String info = modem.getModemInfo();
  Serial.print("Modem Info: ");
  Serial.println(info);

  // Attempt Network Registration
  Serial.println("Attempting automatic network registration...");
  if (!modem.waitForNetwork(60000L)) {
    Serial.println("Auto network registration failed. Attempting manual operator selection...");
    modem.sendAT("+COPS=1,2,\"53024\",7"); // Adjust if needed
    delay(5000);
    if (!modem.waitForNetwork(60000L)) {
      Serial.println("Manual operator selection failed. Check SIM/antenna...");
      // Attempt reconnect at boot
      Serial.println("Attempting SMS reconnect at boot...");
      if (!reconnectSMS()) {
        Serial.println("SMS reconnect failed. Rebooting...");
        safeRestart();
      }
    }
  }
  Serial.println("Network registered!");

  // Connect APN
  Serial.println("Connecting to GPRS (APN: internet)...");
  if (!modem.gprsConnect("internet", "", "")) {
    Serial.println("Failed to connect to GPRS. Attempt reconnect...");
    if (!reconnectSMS()) {
      Serial.println("Reconnect also failed => Rebooting.");
      safeRestart();
    }
  }
  Serial.println("GPRS connected!");
  Serial.print("Assigned IP: ");
  Serial.println(modem.localIP());

  // Print signal quality
  {
    int csq = modem.getSignalQuality(); 
    Serial.print("Signal Quality (CSQ): ");
    Serial.println(csq);
  }

  // Enable text mode + direct push of incoming SMS
  Serial.println("Setting SMS text mode => AT+CMGF=1");
  modem.sendAT("+CMGF=1");
  delay(300);

  Serial.println("Enabling direct SMS URC => AT+CNMI=1,2,0,0,0");
  modem.sendAT("+CNMI=1,2,0,0,0");
  delay(300);
  while (SerialAT.available()) {
    String tmp = SerialAT.readStringUntil('\n');
    tmp.trim();
    Serial.println("[CNMI RESPONSE]: " + tmp);
  }

  Serial.println("[SIM7600] Ready with direct +CMT: incoming SMS enabled.");
  SmsTransport::modemReady = true;
}

//--------------------------------------------
// Additional periodic check
static unsigned long lastNetworkCheck = 0;
void loop() {
  if (!updateGate || !otaTaskHandle || otaActive.load()) { delay(10); return; }
  connectWiFi();
  SmsTransport::Request request;
  if (xQueueReceive(SmsTransport::requests, &request, 0) == pdTRUE) processSmsRequest(request);

  // Continuously read URCs (including +CMT: and +CIPEVENT)
  readModemUnsolicited();

  // Every 30s, check if we're still registered & GPRS is connected
  if (millis() - lastNetworkCheck > 30000UL) {
    lastNetworkCheck = millis();

    bool registered = modem.isNetworkConnected();
    bool gprs       = modem.isGprsConnected();

    if (!registered || !gprs) {
      Serial.println("[LOOP CHECK] Modem lost registration or GPRS => Attempt reconnectSMS()");
      if (!reconnectSMS()) {
        Serial.println("[LOOP CHECK] reconnectSMS() also failed => Rebooting...");
        safeRestart();
      } else {
        Serial.println("[LOOP CHECK] Reconnect successful => Modem back online.");
      }
    }
  }
}

//------------------ connectWiFi --------------------------
void connectWiFi() {
  static uint32_t lastAttempt = 0;
  static bool attempted = false;
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t now = millis();
  if (attempted && uint32_t(now - lastAttempt) < 15000) return;
  attempted = true; lastAttempt = now;
  WiFi.begin(ssid, password);
}

// Only the modem task calls TinyGSM. Never retry a submission automatically:
// a timeout can mean that the carrier accepted the SMS but its response was lost.
void processSmsRequest(const SmsTransport::Request& request) {
  UpdateGuard guard;
  if (!guard.held || otaActive.load()) {
    SmsTransport::result(request.id, "not_submitted"); return;
  }
  const char* destination = request.number[0] ? request.number :
                            (request.recipient == 2 ? secondaryPhone : primaryPhone);
  bool result = modem.sendSMS(destination, request.message);
  SmsTransport::result(request.id, result ? "modem_accepted" : "submission_unknown");
  Serial.printf("[SMS] %s; handset delivery is unconfirmed\n", result ? "modem accepted" : "submission result unknown");
}

//------------------ readModemUnsolicited -----------------
void readModemUnsolicited() {
  while (SerialAT.available()) {
    String line = SerialAT.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;

    Serial.println("[MODEM URC] " + line);

    // +CMT => new SMS
    if (line.startsWith("+CMT:")) {
      // Next line => the SMS text
      String smsText = SerialAT.readStringUntil('\n');
      smsText.trim();
      Serial.println("[MODEM URC] " + smsText);

      int firstQuote  = line.indexOf('"');
      int secondQuote = (firstQuote != -1) ? line.indexOf('"', firstQuote + 1) : -1;
      String sender   = (firstQuote != -1 && secondQuote != -1)
                        ? line.substring(firstQuote + 1, secondQuote)
                        : "Unknown";

      // Debug

      SmsTransport::incoming(sender.c_str(), smsText.c_str());


    }
    // +CIPEVENT => network closed
    else if (line.indexOf("NETWORK CLOSED UNEXPECTEDLY") != -1) {
      Serial.println("[CIPEVENT] GPRS session dropped. Trying reconnect...");
      if (!gprsReconnect()) {
        Serial.println("[CIPEVENT] GPRS reconnect failed => calling reconnectSMS()");
        if (!reconnectSMS()) {
          Serial.println("[CIPEVENT] Full reconnect also failed => rebooting...");
          safeRestart();
        }
      }
    }
  }
}

//------------------ gprsReconnect ------------------------
bool gprsReconnect() {
  Serial.println("[GPRS] Attempting GPRS reconnect...");
  if (!modem.gprsConnect("internet", "", "")) {
    Serial.println("[GPRS] Reconnect failed.");
    return false;
  }
  Serial.println("[GPRS] Reconnected => IP: " + modem.localIP());
  return true;
}

//------------------ extractJsonValue ---------------------
String extractJsonValue(String json, String key) {
  JsonDocument document;
  if (deserializeJson(document, json) || !document[key].is<const char*>()) return "";
  return String(document[key].as<const char*>());
}

//------------------ reconnectSMS -------------------------
bool reconnectSMS() {
  Serial.println("[SMS] Reinitializing SIM7600 module...");

  // power cycle
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(3000);

  Serial.println("[SMS] Restarting modem...");
  modem.restart();
  Serial.println("[SMS] Modem restarted!");

  // re-set modes
  Serial.println("[SMS] AT+CNMP=38");
  modem.sendAT("+CNMP=38"); 
  delay(500);
  Serial.println("[SMS] Selecting final network mode (AT+CNMP=2)...");
  modem.sendAT("+CNMP=2");
  delay(500);
  Serial.println("[SMS] Enabling Cat-M1 (AT+CMNB=1)...");
  modem.sendAT("+CMNB=1");
  delay(500);

  // Wait for network
  if (!modem.waitForNetwork(60000L)) {
    Serial.println("[SMS] WaitForNetwork fail => Attempt manual operator selection...");
    modem.sendAT("+COPS=1,2,\"53024\",7");
    delay(5000);
    if (!modem.waitForNetwork(60000L)) {
      Serial.println("[SMS] Manual operator selection also failed => returning false");
      return false;
    }
  }
  Serial.println("[SMS] Network re-registered after reconnect.");

  // GPRS
  if (!modem.gprsConnect("internet", "", "")) {
    Serial.println("[SMS] GPRS reconnect fail => returning false");
    return false;
  }
  Serial.println("[SMS] GPRS reconnected => IP: " + modem.localIP());

  // Re-enable text mode + direct push
  Serial.println("[SMS] Re-enabling +CMT: => AT+CMGF=1, +CNMI=1,2,0,0,0");
  modem.sendAT("+CMGF=1");
  delay(200);
  modem.sendAT("+CNMI=1,2,0,0,0");
  delay(200);

  while (SerialAT.available()) {
    String tmp = SerialAT.readStringUntil('\n');
    tmp.trim();
    if (!tmp.isEmpty()) {
      Serial.println("[reconnectSMS][CNMI RESPONSE]: " + tmp);
    }
  }


  return true;
}
