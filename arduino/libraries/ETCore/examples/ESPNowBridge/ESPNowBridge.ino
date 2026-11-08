// ESP32 Dev Module Wi-Fi / encrypted ET-Core / serial / E220 bridge, migration 2026-11-08.
#include <Arduino.h>
#include <WiFi.h>
#include <ETCore.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <atomic>
#include "LoRa_E220.h"
#include "BridgeProtocol.h"
#include "secrets.h"
#include "BridgeETCore.h"

// ET-Core command and state channel identifiers
#define COMMAND_TOPIC "relay_set"
#define STATE_TOPIC "garden_relaystate"
#define PAYLOAD_ON "1"
#define PAYLOAD_OFF "0"

// Add RGB control topics for Nodes 12, 13, and 14
const char* light_set_topic12 = "rgb12_set";
const char* light_state_topic12 = "rgb2";
const char* light_set_topic13 = "rgb13_set";
const char* light_state_topic13 = "rgb3";
const char* light_set_topic14 = "rgb14_set";
const char* light_state_topic14 = "rgb1";

// ET-Core command and state channel identifiers for pump control
const char* pump_on_time_topic = "pump_on_time";
const char* pump_off_time_topic = "pump_off_time";
const char* pump_control_topic = "pump_control";
const char* pump_save_settings_topic = "pump_save_settings";

// ET-Core topic for pump state
const char* pump_state_topic = "pump_state";


// Additional variables for RGB values for each node
int red12 = 255, green12 = 255, blue12 = 255, brightness12 = 255;
int red13 = 255, green13 = 255, blue13 = 255, brightness13 = 255;
int red14 = 255, green14 = 255, blue14 = 255, brightness14 = 255;
bool stateOn12 = false, stateOn13 = false, stateOn14 = false;

// Float variables for each node
float temp1, humd1, battery1;
float temp2, humd2, battery2;
float temp3, humd3, battery3;
float pV4, pI4, bV4, bI4, lV4, lI4;
float pV5, pI5, bV5, bI5, lV5, lI5;
float dustDensity6, voltage6;
float temp7, humd7, pressure7, gas7, voltage7;
float loadvoltageB8, loadvoltage8, current_mA8, lux8, moisture_per_18, moisture_per_28, moisture_per_38, moisture_per_48, water_total_count8;
float ph9;
float cf10;
float batteryLora1;
int waterLora1;
float battery11;
int pir11;
float tempLora2, humdLora2, batteryLora2;

// Hardware wiring preserved from the working bridge.
#define RXD2 25
#define TXD2 26
LoRa_E220 e220ttl(&Serial2);

void processReceivedData(const NodeMessage& message);
void publishRelayState(const char* state);
bool processRGBJson(String message, int nodeId);
void sendRGBState(int nodeId, bool stateOn, int red, int green, int blue, int brightness);
void handleCoreCommand(const char* deviceClass, JsonObject payload);

void processReceivedData(const NodeMessage& message) {
    (void)message;
    // Print received data
    String tempTopic, humdTopic, batteryTopic, pVTopic, pITopic, bVTopic, bITopic, lVTopic, lITopic;
    String dustDensityTopic, voltageTopic, pressureTopic, gasTopic;
    String loadvoltageBTopic, loadvoltageTopic, current_mATopic, luxTopic, moisture_per_1Topic, moisture_per_2Topic, moisture_per_3Topic, moisture_per_4Topic, water_total_countTopic;
    String phTopic, cfTopic, batteryLora1Topic, waterLora1Topic, pirTopic, battery11Topic, tempLora2Topic, humdLora2Topic, batteryLora2Topic;

    switch (message.type) {
        case NODE1:
            tempTopic = "bedroom_temperature";
            humdTopic = "bedroom_humidity";
            batteryTopic = "bedroom_battery";
            Serial.print("Received Serial data Node1, Temp: ");
            Serial.print(temp1);
            Serial.print(", Humidity: ");
            Serial.print(humd1);
            Serial.print(", Battery: ");
            Serial.println(battery1);

            // Publish to ET-Core topics with retain flag
            publishState(tempTopic.c_str(), String(temp1).c_str(), true);
            publishState(humdTopic.c_str(), String(humd1).c_str(), true);
            publishState(batteryTopic.c_str(), String(battery1).c_str(), true);
            break;
        case NODE2:
            tempTopic = "lounge_temperature";
            humdTopic = "lounge_humidity";
            batteryTopic = "lounge_battery";
            Serial.print("Received Serial data Node2, Temp: ");
            Serial.print(temp2);
            Serial.print(", Humidity: ");
            Serial.print(humd2);
            Serial.print(", Battery: ");
            Serial.println(battery2);

            // Publish to ET-Core topics with retain flag
            publishState(tempTopic.c_str(), String(temp2).c_str(), true);
            publishState(humdTopic.c_str(), String(humd2).c_str(), true);
            publishState(batteryTopic.c_str(), String(battery2).c_str(), true);
            break;
        case NODE3:
            tempTopic = "bedroom1_temperature";
            humdTopic = "bedroom1_humidity";
            batteryTopic = "bedroom1_battery";
            Serial.print("Received Serial data Node3, Temp: ");
            Serial.print(temp3);
            Serial.print(", Humidity: ");
            Serial.print(humd3);
            Serial.print(", Battery: ");
            Serial.println(battery3);

            // Publish to ET-Core topics with retain flag
            publishState(tempTopic.c_str(), String(temp3).c_str(), true);
            publishState(humdTopic.c_str(), String(humd3).c_str(), true);
            publishState(batteryTopic.c_str(), String(battery3).c_str(), true);
            break;
        case NODE4:
            pVTopic = "solar10a_panelv";
            pITopic = "solar10a_paneli";
            bVTopic = "solar10a_batteryv";
            bITopic = "solar10a_batteryi";
            lVTopic = "solar10a_loadv";
            lITopic = "solar10a_loadi";
            Serial.print("Received Serial data Node4, pV: ");
            Serial.print(pV4);
            Serial.print(", pI: ");
            Serial.print(pI4);
            Serial.print(", bV: ");
            Serial.print(bV4);
            Serial.print(", bI: ");
            Serial.print(bI4);
            Serial.print(", lV: ");
            Serial.print(lV4);
            Serial.print(", lI: ");
            Serial.println(lI4);

            // Publish to ET-Core topics with retain flag
            publishState(pVTopic.c_str(), String(pV4).c_str(), true);
            publishState(pITopic.c_str(), String(pI4).c_str(), true);
            publishState(bVTopic.c_str(), String(bV4).c_str(), true);
            publishState(bITopic.c_str(), String(bI4).c_str(), true);
            publishState(lVTopic.c_str(), String(lV4).c_str(), true);
            publishState(lITopic.c_str(), String(lI4).c_str(), true);
            break;
        case NODE5:
            pVTopic = "solar20a_panelv";
            pITopic = "solar20a_paneli";
            bVTopic = "solar20a_batteryv";
            bITopic = "solar20a_batteryi";
            lVTopic = "solar20a_loadv";
            lITopic = "solar20a_loadi";
            Serial.print("Received Serial data Node5, pV: ");
            Serial.print(pV5);
            Serial.print(", pI: ");
            Serial.print(pI5);
            Serial.print(", bV: ");
            Serial.print(bV5);
            Serial.print(", bI: ");
            Serial.print(bI5);
            Serial.print(", lV: ");
            Serial.print(lV5);
            Serial.print(", lI: ");
            Serial.println(lI5);

            // Publish to ET-Core topics with retain flag
            publishState(pVTopic.c_str(), String(pV5).c_str(), true);
            publishState(pITopic.c_str(), String(pI5).c_str(), true);
            publishState(bVTopic.c_str(), String(bV5).c_str(), true);
            publishState(bITopic.c_str(), String(bI5).c_str(), true);
            publishState(lVTopic.c_str(), String(lV5).c_str(), true);
            publishState(lITopic.c_str(), String(lI5).c_str(), true);
            break;
        case NODE6:
            dustDensityTopic = "espnow_node6_dustdensity";
            voltageTopic = "espnow_node6_voltage";
            Serial.print("Received Serial data Node6, DustDensity: ");
            Serial.print(dustDensity6);
            Serial.print(", Voltage: ");
            Serial.println(voltage6);

            // Publish to ET-Core topics with retain flag
            publishState(dustDensityTopic.c_str(), String(dustDensity6).c_str(), true);
            publishState(voltageTopic.c_str(), String(voltage6).c_str(), true);
            break;
        case NODE7:
            tempTopic = "temperature_degreecelsius";
            humdTopic = "humidity_percentrelative";
            pressureTopic = "barometer_hectopascal";
            gasTopic = "gas_gas";
            voltageTopic = "voltage_volts";
            Serial.print("Received Serial data Node7, Temp: ");
            Serial.print(temp7);
            Serial.print(", Humidity: ");
            Serial.print(humd7);
            Serial.print(", Pressure: ");
            Serial.print(pressure7);
            Serial.print(", Gas: ");
            Serial.print(gas7);
            Serial.print(", Voltage: ");
            Serial.println(voltage7);

            // Publish to ET-Core topics with retain flag
            publishState(tempTopic.c_str(), String(temp7).c_str(), true);
            publishState(humdTopic.c_str(), String(humd7).c_str(), true);
            publishState(pressureTopic.c_str(), String(pressure7).c_str(), true);
            publishState(gasTopic.c_str(), String(gas7).c_str(), true);
            publishState(voltageTopic.c_str(), String(voltage7).c_str(), true);
            break;
        case NODE8:
            loadvoltageBTopic = "garden_battery";
            loadvoltageTopic = "garden_solar";
            current_mATopic = "garden_amps";
            luxTopic = "garden_lux";
            moisture_per_1Topic = "garden_moisture1";
            moisture_per_2Topic = "garden_moisture2";
            moisture_per_3Topic = "garden_moisture3";
            moisture_per_4Topic = "garden_moisture4";
            water_total_countTopic = "garden_watercheck";
            Serial.print("Received Serial data Node8, LoadVoltageB: ");
            Serial.print(loadvoltageB8);
            Serial.print(", LoadVoltage: ");
            Serial.print(loadvoltage8);
            Serial.print(", Current_mA: ");
            Serial.print(current_mA8);
            Serial.print(", Lux: ");
            Serial.print(lux8);
            Serial.print(", Moisture_Per_1: ");
            Serial.print(moisture_per_18);
            Serial.print(", Moisture_Per_2: ");
            Serial.print(moisture_per_28);
            Serial.print(", Moisture_Per_3: ");
            Serial.print(moisture_per_38);
            Serial.print(", Moisture_Per_4: ");
            Serial.print(moisture_per_48);
            Serial.print(", Water_Total_Count: ");
            Serial.println(water_total_count8);

            // Publish to ET-Core topics with retain flag
            publishState(loadvoltageBTopic.c_str(), String(loadvoltageB8).c_str(), true);
            publishState(loadvoltageTopic.c_str(), String(loadvoltage8).c_str(), true);
            publishState(current_mATopic.c_str(), String(current_mA8).c_str(), true);
            publishState(luxTopic.c_str(), String(lux8).c_str(), true);
            publishState(moisture_per_1Topic.c_str(), String(moisture_per_18).c_str(), true);
            publishState(moisture_per_2Topic.c_str(), String(moisture_per_28).c_str(), true);
            publishState(moisture_per_3Topic.c_str(), String(moisture_per_38).c_str(), true);
            publishState(moisture_per_4Topic.c_str(), String(moisture_per_48).c_str(), true);
            publishState(water_total_countTopic.c_str(), String(water_total_count8).c_str(), true);
            break;
        case NODE9:
            phTopic = "espph_ph";
            Serial.print("Received Serial data Node9, pH: ");
            Serial.println(ph9);

            // Publish to ET-Core topics with retain flag
            publishState(phTopic.c_str(), String(ph9).c_str(), true);
            break;
        case NODE10:
            cfTopic = "espcf_cf";
            Serial.print("Received Serial data Node10, CF: ");
            Serial.println(cf10);

            // Publish to ET-Core topics with retain flag
            publishState(cfTopic.c_str(), String(cf10).c_str(), true);
            break;
        case NODE11:
            pirTopic = "pir_pirbatt_state1";
            battery11Topic = "pirbatt_voltage";
            Serial.print("Received Serial data Node11, PIR: ");
            Serial.print(pir11);
            Serial.print(", Battery: ");
            Serial.println(battery11);

            // Publish to ET-Core topics with retain flag
            publishState(pirTopic.c_str(), String(pir11).c_str(), true);
            publishState(battery11Topic.c_str(), String(battery11).c_str(), true);
            break;
        case LORA1:
            batteryLora1Topic = "espnow_lora1_battery";
            waterLora1Topic = "espnow_lora1_water";
            Serial.print("Received Serial data LoRa1, Battery: ");
            Serial.print(batteryLora1);
            Serial.print(", Water: ");
            Serial.println(waterLora1);

            // Publish to ET-Core topics with retain flag
            publishState(batteryLora1Topic.c_str(), String(batteryLora1).c_str(), true);
            publishState(waterLora1Topic.c_str(), String(waterLora1).c_str(), true);
            break;
        case LORA2:
            tempLora2Topic = "loranode2_temp";
            humdLora2Topic = "loranode2_humd";
            batteryLora2Topic = "loranode2_battery";

            Serial.print("Received LoRa data Node2, Temperature: ");
            Serial.print(tempLora2);
            Serial.print(", Humidity: ");
            Serial.print(humdLora2);
            Serial.print(", Battery: ");
            Serial.println(batteryLora2);

            // Publish to ET-Core topics with retain flag
            publishState(tempLora2Topic.c_str(), String(tempLora2).c_str(), true);
            publishState(humdLora2Topic.c_str(), String(humdLora2).c_str(), true);
            publishState(batteryLora2Topic.c_str(), String(batteryLora2).c_str(), true);
            break;
        default:
            Serial.println("Unknown node type received.");
            break;
    }
}


void acceptMessage(const NodeMessage& message) {
    switch (message.type) {
    case NODE1:
        temp1 = message.data.node1Data.temp;
        humd1 = message.data.node1Data.humd;
        battery1 = message.data.node1Data.battery;
        break;
    case NODE2:
        temp2 = message.data.node2Data.temp;
        humd2 = message.data.node2Data.humd;
        battery2 = message.data.node2Data.battery;
        break;
    case NODE3:
        temp3 = message.data.node3Data.temp;
        humd3 = message.data.node3Data.humd;
        battery3 = message.data.node3Data.battery;
        break;
    case NODE4:
        pV4 = message.data.node4Data.pV;
        pI4 = message.data.node4Data.pI;
        bV4 = message.data.node4Data.bV;
        bI4 = message.data.node4Data.bI;
        lV4 = message.data.node4Data.lV;
        lI4 = message.data.node4Data.lI;
        break;
    case NODE5:
        pV5 = message.data.node5Data.pV;
        pI5 = message.data.node5Data.pI;
        bV5 = message.data.node5Data.bV;
        bI5 = message.data.node5Data.bI;
        lV5 = message.data.node5Data.lV;
        lI5 = message.data.node5Data.lI;
        break;
    case NODE6:
        dustDensity6 = message.data.node6Data.dustDensity;
        voltage6 = message.data.node6Data.voltage;
        break;
    case NODE7:
        temp7 = message.data.node7Data.temperature;
        humd7 = message.data.node7Data.humidity;
        pressure7 = message.data.node7Data.pressure;
        gas7 = message.data.node7Data.gas;
        voltage7 = message.data.node7Data.voltage;
        break;
    case NODE8:
        loadvoltageB8 = message.data.node8Data.loadvoltageB;
        loadvoltage8 = message.data.node8Data.loadvoltage;
        current_mA8 = message.data.node8Data.current_mA;
        lux8 = message.data.node8Data.lux;
        moisture_per_18 = message.data.node8Data.moisture_per_1;
        moisture_per_28 = message.data.node8Data.moisture_per_2;
        moisture_per_38 = message.data.node8Data.moisture_per_3;
        moisture_per_48 = message.data.node8Data.moisture_per_4;
        water_total_count8 = message.data.node8Data.water_total_count;
        break;
    case NODE9:
        ph9 = message.data.node9Data.ph;
        break;
    case NODE10:
        cf10 = message.data.node10Data.cf;
        break;
    case NODE11:
        pir11 = message.data.node11Data.pir;
        battery11 = message.data.node11Data.battery;
        break;
    case LORA1:
        batteryLora1 = message.data.lora1Data.battery;
        waterLora1 = message.data.lora1Data.water;
        break;
    case LORA2:
        tempLora2 = message.data.lora2Data.temperature;
        humdLora2 = message.data.lora2Data.humidity;
        batteryLora2 = message.data.lora2Data.battery;
        break;
    }
    processReceivedData(message);
}

void publishRelayState(const char* state) {
    // Legacy optimistic command echo, not physical relay acknowledgement.
    publishState(STATE_TOPIC, strcmp(state, "ON") == 0 ? "1" : "0", true);
}

bool processRGBJson(String message, int nodeId) {
    JsonDocument doc;
    if (deserializeJson(doc, message)) { Serial.println("Invalid RGB JSON"); return false; }
    bool* stateOn = nodeId == 12 ? &stateOn12 : nodeId == 13 ? &stateOn13 : &stateOn14;
    int* red = nodeId == 12 ? &red12 : nodeId == 13 ? &red13 : &red14;
    int* green = nodeId == 12 ? &green12 : nodeId == 13 ? &green13 : &green14;
    int* blue = nodeId == 12 ? &blue12 : nodeId == 13 ? &blue13 : &blue14;
    int* brightness = nodeId == 12 ? &brightness12 : nodeId == 13 ? &brightness13 : &brightness14;
    // Merge partial commands, so brightness alone cannot silently turn a light off.
    bool nextState = *stateOn;
    int r = *red, g = *green, b = *blue, level = *brightness, transition = 0;
    if (!doc["state"].isNull()) {
        if (!doc["state"].is<const char*>()) return false;
        const char* state = doc["state"];
        if (strcmp(state, "ON") != 0 && strcmp(state, "OFF") != 0) return false;
        nextState = strcmp(state, "ON") == 0;
    }
    auto readByte = [](JsonVariantConst value, int& result) -> bool {
        if (value.isNull()) return true;
        if (!value.is<int>() || value.as<int>() < 0 || value.as<int>() > 255) return false;
        result = value.as<int>(); return true;
    };
    if (!doc["color"].isNull() && !doc["color"].is<JsonObject>()) return false;
    if (!readByte(doc["color"]["r"], r) || !readByte(doc["color"]["g"], g)
        || !readByte(doc["color"]["b"], b) || !readByte(doc["brightness"], level)) return false;
    if (!doc["transition"].isNull()) {
        if (!doc["transition"].is<int>() || doc["transition"].as<int>() < 0) return false;
        transition = doc["transition"].as<int>();
    }
    Serial1.printf("RGB%d,%d,%d,%d,%d,%d,%d\n", nodeId, nextState ? 1 : 0, r, g, b, level, transition);
    *stateOn = nextState; *red = r; *green = g; *blue = b; *brightness = level;
    sendRGBState(nodeId, nextState, r, g, b, level);
    return true;
}

void sendRGBState(int nodeId, bool stateOn, int red, int green, int blue, int brightness) {
    JsonDocument doc;
    doc["state"] = stateOn ? "ON" : "OFF";
    doc["color"]["r"] = red;
    doc["color"]["g"] = green;
    doc["color"]["b"] = blue;
    doc["brightness"] = brightness;
    char buffer[160];
    if (measureJson(doc) >= sizeof(buffer)) return;
    serializeJson(doc, buffer, sizeof(buffer));
    publishState(nodeId == 12 ? light_state_topic12 : nodeId == 13 ? light_state_topic13 : light_state_topic14, buffer, true);
}


void handleCoreCommand(const char* deviceClass, JsonObject payload) {
    const char* id = payload["cmd_id"] | "";
    auto reject = [&]() { BridgeETCore::core.sendAck(id, false); };
    if (strcmp(deviceClass, "bridge.espnow") != 0 || BridgeETCore::otaBusy
        || WiFi.status() != WL_CONNECTED || !BridgeETCore::started) { reject(); return; }
    const char* command = payload["command"] | "";
    if (!strcmp(command,"relay_set")) {
        if (!payload["on"].is<bool>()) { reject(); return; }
        const bool on = payload["on"].as<bool>();
        Serial1.println(on ? "RELAY_ON" : "RELAY_OFF");
        publishRelayState(on ? "ON" : "OFF");
    } else if (!strcmp(command,"pump_on_time") || !strcmp(command,"pump_off_time")) {
        if (!payload["seconds"].is<uint32_t>()) { reject(); return; }
        const uint32_t seconds = payload["seconds"].as<uint32_t>();
        if (seconds > UINT32_MAX / 1000U) { reject(); return; }
        Serial1.printf("%s,%lu\n", !strcmp(command,"pump_on_time") ? "PUMP_ON_TIME" : "PUMP_OFF_TIME", (unsigned long)(seconds*1000U));
    } else if (!strcmp(command,"pump_control")) {
        const char* action = payload["action"] | "";
        if (strcmp(action,"START") && strcmp(action,"STOP")) { reject(); return; }
        Serial1.println(!strcmp(action,"START") ? "PUMP_START" : "PUMP_STOP");
    } else if (!strcmp(command,"pump_save_settings")) {
        Serial1.println("PUMP_SAVE_SETTINGS");
    } else if (!strcmp(command,"rgb12_set") || !strcmp(command,"rgb13_set") || !strcmp(command,"rgb14_set")) {
        String text; serializeJson(payload,text);
        const int node = !strcmp(command,"rgb12_set") ? 12 : !strcmp(command,"rgb13_set") ? 13 : 14;
        if (!processRGBJson(text,node)) { reject(); return; }
    } else { reject(); return; }
    // ACK confirms authenticated gateway acceptance/UART dispatch, not remote actuation.
    BridgeETCore::core.sendAck(id,true);
}

static char uartLine[384];
static size_t uartUsed = 0;
static bool uartOverflow = false;
static uint32_t uartLastByte = 0;

void handleLine(char* line) {
    if (strncmp(line, "PUMP_STATE,", 11) == 0) {
        String value(line + 11);
        value.trim();
        if (value.length() && value.length() <= 63) publishState(pump_state_topic, value.c_str(), true);
        return;
    }
    NodeMessage message = {};
    if (parseTelemetry(line, message)) acceptMessage(message);
    else Serial.println("[UART1] Invalid/incomplete packet rejected");
}

void serviceReceiver() {
    if (uartUsed && uint32_t(millis() - uartLastByte) > 2000U) {
        // Keep discarding until the next newline, rather than trusting a tail.
        uartOverflow = true;
        uartUsed = 0;
    }
    unsigned budget = 512;
    while (budget-- && Serial1.available()) {
        const int byteValue = Serial1.read();
        if (byteValue < 0) break;
        const char c = char(byteValue);
        uartLastByte = millis();
        if (c == '\n') {
            if (!uartOverflow && uartUsed) {
                uartLine[uartUsed] = 0;
                handleLine(uartLine);
            }
            uartUsed = 0; uartOverflow = false;
        } else if (c != '\r') {
            if (c == '\0' || uartUsed >= sizeof(uartLine) - 1) uartOverflow = true;
            if (!uartOverflow) uartLine[uartUsed++] = c;
        }
    }
}

void serviceLoRa() {
    // The original receiveMessage() uses String/readString() on binary data and
    // clears the UART afterwards. Read fixed legacy structs without those losses.
    static uint8_t packet[24];
    static size_t used = 0, expected = 0;
    static bool invalidBurst = false;
    static uint32_t lastByte = 0;
    if ((used || invalidBurst) && uint32_t(millis() - lastByte) >= 100U) {
        if (used) Serial.println("[LoRa] Truncated packet discarded");
        used = expected = 0; invalidBurst = false;
    }
    unsigned budget = 256;
    while (budget-- && Serial2.available()) {
        int value = Serial2.read();
        if (value < 0) break;
        lastByte = millis();
        if (invalidBurst) continue;
        packet[used++] = uint8_t(value);
        if (used == 10) {
            if (legacyName(reinterpret_cast<const char*>(packet), "lora1")) expected = 20;
            else if (legacyName(reinterpret_cast<const char*>(packet), "lora2")) expected = 24;
            else { used = 0; invalidBurst = true; Serial.println("[LoRa] Unknown packet header"); continue; }
        }
        if (expected && used == expected) {
            NodeMessage message = {};
            if (parseLoRa(packet, used, message)) acceptMessage(message);
            else Serial.println("[LoRa] Invalid packet rejected");
            used = expected = 0;
        }
    }
}

static uint32_t lastWiFiAttempt = 0;
static unsigned wifiAttempts = 0;
static bool wifiStartPending = false;
static uint32_t wifiStoppedAt = 0;
static bool hadWiFi = false, otaStarted = false;
static std::atomic<unsigned> lastDisconnectReason{0};

void serviceWiFi() {
    bool online = WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) != 0;
    if (online) {
        if (!hadWiFi) {
            Serial.printf("[WiFi] Connected IP %s RSSI %d channel %d\n",
                WiFi.localIP().toString().c_str(), int(WiFi.RSSI()), int(WiFi.channel()));
            BridgeETCore::startSession();
            ArduinoOTA.begin();
            otaStarted = true; hadWiFi = true; wifiAttempts = 0;
            Serial.println("[OTA] espnow-wifi-master.local ready");
        }
        ArduinoOTA.handle();
        return;
    }
    if (hadWiFi) {
        Serial.printf("[WiFi] Link lost; status %d reason %u\n", int(WiFi.status()), lastDisconnectReason.load());
        hadWiFi = false;
        if (otaStarted) { ArduinoOTA.end(); otaStarted = false; }
        lastWiFiAttempt = millis() - 30000U;
    }
    // Stop and start on separate loop passes. The core's asynchronous retry
    // must not race a new esp_wifi_set_config() while still authenticating.
    if (wifiStartPending) {
        if (uint32_t(millis() - wifiStoppedAt) < 250U) return;
        wifiStartPending = false;
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(false);
        WiFi.setSleep(false);
        WiFi.begin(ssid, password);
        lastWiFiAttempt = millis();
        return;
    }
    if (uint32_t(millis() - lastWiFiAttempt) < 30000U) return;
    ++wifiAttempts;
    Serial.printf("[WiFi] Attempt %u; previous status %d reason %u\n",
        wifiAttempts, int(WiFi.status()), lastDisconnectReason.load());
    WiFi.mode(WIFI_OFF);
    wifiStoppedAt = millis();
    wifiStartPending = true;
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nESP-NOW Wi-Fi Master + encrypted ET-Core 2026-11-08.1");
    Serial1.setRxBufferSize(4096);
    Serial1.begin(115200, SERIAL_8N1, 16, 17);
    Serial2.setRxBufferSize(1024);
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    if (!e220ttl.begin()) Serial.println("[LoRa] Module initialization failed");
    // No ET-Core command is issued at boot. The original UART protocol is retained.
    ArduinoOTA.setHostname("espnow-wifi-master");
    ArduinoOTA.setPassword(ota_password);
    ArduinoOTA.setTimeout(10000);
    ArduinoOTA.onStart([]() {
        BridgeETCore::otaBusy = true;
        if (BridgeETCore::started) BridgeETCore::core.sendBye();
        Serial.println("[OTA] Upload started");
    });
    ArduinoOTA.onEnd([]() { Serial.println("[OTA] Complete; rebooting"); });
    ArduinoOTA.onError([](ota_error_t error) {
        BridgeETCore::otaBusy = false;
        if (WiFi.status() == WL_CONNECTED) BridgeETCore::startSession();
        Serial.printf("[OTA] Error %u\n", unsigned(error));
    });
    WiFi.persistent(false);
    WiFi.setHostname("espnow-wifi-master");
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        lastDisconnectReason.store(info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    lastWiFiAttempt = millis() - 30000U;
    BridgeETCore::configure(handleCoreCommand);
}

void loop() {
    serviceWiFi();
    serviceReceiver();
    serviceLoRa();
    BridgeETCore::service();
    delay(1);  // yield to Wi-Fi/RTOS; no connection wait in this task
}
