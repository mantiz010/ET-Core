#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
enum NodeType {
    NODE1,
    NODE2,
    NODE3,
    NODE4,
    NODE5,
    NODE6,
    NODE7,
    NODE8,
    NODE9,
    NODE10,
    NODE11,
    LORA1,
    LORA2
};

// Structure to hold node data
typedef struct {
    char nodeName[10];
    float temp;
    float humd;
    float battery;
} struct_message_node1;

typedef struct {
    char nodeName[10];
    float temp;
    float humd;
    float battery;
} struct_message_node2;

typedef struct {
    char nodeName[10];
    float temp;
    float humd;
    float battery;
} struct_message_node3;

typedef struct {
    char nodeName[10];
    float pV;
    float pI;
    float bV;
    float bI;
    float lV;
    float lI;
} struct_message_node4;

typedef struct {
    char nodeName[10];
    float pV;
    float pI;
    float bV;
    float bI;
    float lV;
    float lI;
} struct_message_node5;

typedef struct {
    char nodeName[10];
    float dustDensity;
    float voltage;
} struct_message_node6;

typedef struct {
    char nodeName[10];
    float temperature;
    float humidity;
    float pressure;
    float gas;
    float voltage;
} struct_message_node7;

typedef struct {
    char nodeName[10];
    float loadvoltageB;
    float loadvoltage;
    float current_mA;
    float lux;
    float moisture_per_1;
    float moisture_per_2;
    float moisture_per_3;
    float moisture_per_4;
    float water_total_count;
} struct_message_node8;

typedef struct {
    char nodeName[10];
    float ph;
} struct_message_node9;

typedef struct {
    char nodeName[10];
    float cf;
} struct_message_node10;

typedef struct {
    char nodeName[10];
    int pir;
    float battery;
} struct_message_node11;

typedef struct {
    char nodeName[10];
    float battery;
    int water;
} struct_message_lora1;

typedef struct {
    char nodeName[10];
    float temperature;
    float humidity;
    float battery;
} struct_message_lora2;

// Union to hold any node data
typedef union {
    struct_message_node1 node1Data;
    struct_message_node2 node2Data;
    struct_message_node3 node3Data;
    struct_message_node4 node4Data;
    struct_message_node5 node5Data;
    struct_message_node6 node6Data;
    struct_message_node7 node7Data;
    struct_message_node8 node8Data;
    struct_message_node9 node9Data;
    struct_message_node10 node10Data;
    struct_message_node11 node11Data;
    struct_message_lora1 lora1Data;
    struct_message_lora2 lora2Data;
} NodeDataUnion;

// Structure to hold node type and data
typedef struct {
    NodeType type;
    NodeDataUnion data;
} NodeMessage;


static_assert(sizeof(struct_message_lora1) == 20, "Legacy LoRa1 wire size changed");
static_assert(sizeof(struct_message_lora2) == 24, "Legacy LoRa2 wire size changed");
static_assert(offsetof(struct_message_lora2, temperature) == 12, "Legacy LoRa alignment changed");

inline bool parseNumber(const char* text, float& value) {
    if (!text || !*text) return false;
    errno = 0;
    char* end = nullptr;
    value = strtof(text, &end);
    while (*end == ' ' || *end == '\t') ++end;
    return end != text && !*end && errno != ERANGE && isfinite(value);
}
inline bool integerValue(float value) {
    return isfinite(value) && double(value) >= INT_MIN && double(value) <= INT_MAX && truncf(value) == value;
}
inline bool parseDurationMs(const char* text, uint32_t& result) {
    if (!text || !*text) return false;
    uint32_t seconds = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const uint32_t digit = uint32_t(*p - '0');
        if (seconds > (UINT32_MAX / 1000U - digit) / 10U) return false;
        seconds = seconds * 10U + digit;
    }
    result = seconds * 1000U;
    return true;
}
// Destructive bounded CSV split: empty fields are retained and then rejected.
inline size_t splitFields(char* line, char** fields, size_t capacity) {
    size_t count = 0;
    if (!line || !capacity) return 0;
    fields[count++] = line;
    for (char* p = line; *p; ++p) {
        if (*p != ',') continue;
        if (count == capacity) return 0;
        *p = '\0';
        fields[count++] = p + 1;
    }
    return count;
}
inline bool legacyName(const char* field, const char* expected) {
    const void* nul = memchr(field, 0, 10);
    return nul && strcmp(field, expected) == 0;
}
inline bool parseLoRa(const uint8_t* bytes, size_t size, NodeMessage& out) {
    out = {};
    if (size == sizeof(struct_message_lora1)) {
        auto& v = out.data.lora1Data;
        memcpy(&v, bytes, size);
        if (!legacyName(v.nodeName, "lora1") || !isfinite(v.battery)) return false;
        out.type = LORA1;
        return true;
    }
    if (size == sizeof(struct_message_lora2)) {
        auto& v = out.data.lora2Data;
        memcpy(&v, bytes, size);
        if (!legacyName(v.nodeName, "lora2") || !isfinite(v.temperature) || !isfinite(v.humidity) || !isfinite(v.battery)) return false;
        out.type = LORA2;
        return true;
    }
    return false;
}

// ESP,<mac>,NodeN,... or LORA,loraN,... . No partial packet is accepted.
inline bool parseTelemetry(char* line, NodeMessage& out) {
    char* f[14];
    size_t n = splitFields(line, f, 14);
    if (!n) return false;
    bool esp = strcmp(f[0], "ESP") == 0;
    bool lora = strcmp(f[0], "LORA") == 0;
    const size_t first = esp ? 3 : 2;
    if ((!esp && !lora) || n <= first) return false;
    if (esp && (!*f[1] || strlen(f[1]) > 17)) return false;
    const char* name = f[first - 1];
    float values[9] = {};
    const size_t count = n - first;
    if (count > 9) return false;
    for (size_t i = 0; i < count; ++i) if (!parseNumber(f[first+i], values[i])) return false;
    out = {};
    if (esp && strcmp(name, "Node1") == 0) {
        if (count != 3) return false;
        out.type = NODE1;
        strcpy(out.data.node1Data.nodeName, "Node1");
        out.data.node1Data.temp = values[0];
        out.data.node1Data.humd = values[1];
        out.data.node1Data.battery = values[2];
        return true;
    }
    if (esp && strcmp(name, "Node2") == 0) {
        if (count != 3) return false;
        out.type = NODE2;
        strcpy(out.data.node2Data.nodeName, "Node2");
        out.data.node2Data.temp = values[0];
        out.data.node2Data.humd = values[1];
        out.data.node2Data.battery = values[2];
        return true;
    }
    if (esp && strcmp(name, "Node3") == 0) {
        if (count != 3) return false;
        out.type = NODE3;
        strcpy(out.data.node3Data.nodeName, "Node3");
        out.data.node3Data.temp = values[0];
        out.data.node3Data.humd = values[1];
        out.data.node3Data.battery = values[2];
        return true;
    }
    if (esp && strcmp(name, "Node4") == 0) {
        if (count != 6) return false;
        out.type = NODE4;
        strcpy(out.data.node4Data.nodeName, "Node4");
        out.data.node4Data.pV = values[0];
        out.data.node4Data.pI = values[1];
        out.data.node4Data.bV = values[2];
        out.data.node4Data.bI = values[3];
        out.data.node4Data.lV = values[4];
        out.data.node4Data.lI = values[5];
        return true;
    }
    if (esp && strcmp(name, "Node5") == 0) {
        if (count != 6) return false;
        out.type = NODE5;
        strcpy(out.data.node5Data.nodeName, "Node5");
        out.data.node5Data.pV = values[0];
        out.data.node5Data.pI = values[1];
        out.data.node5Data.bV = values[2];
        out.data.node5Data.bI = values[3];
        out.data.node5Data.lV = values[4];
        out.data.node5Data.lI = values[5];
        return true;
    }
    if (esp && strcmp(name, "Node6") == 0) {
        if (count != 2) return false;
        out.type = NODE6;
        strcpy(out.data.node6Data.nodeName, "Node6");
        out.data.node6Data.dustDensity = values[0];
        out.data.node6Data.voltage = values[1];
        return true;
    }
    if (esp && strcmp(name, "Node7") == 0) {
        if (count != 5) return false;
        out.type = NODE7;
        strcpy(out.data.node7Data.nodeName, "Node7");
        out.data.node7Data.temperature = values[0];
        out.data.node7Data.humidity = values[1];
        out.data.node7Data.pressure = values[2];
        out.data.node7Data.gas = values[3];
        out.data.node7Data.voltage = values[4];
        return true;
    }
    if (esp && strcmp(name, "Node8") == 0) {
        if (count != 9) return false;
        out.type = NODE8;
        strcpy(out.data.node8Data.nodeName, "Node8");
        out.data.node8Data.loadvoltageB = values[0];
        out.data.node8Data.loadvoltage = values[1];
        out.data.node8Data.current_mA = values[2];
        out.data.node8Data.lux = values[3];
        out.data.node8Data.moisture_per_1 = values[4];
        out.data.node8Data.moisture_per_2 = values[5];
        out.data.node8Data.moisture_per_3 = values[6];
        out.data.node8Data.moisture_per_4 = values[7];
        out.data.node8Data.water_total_count = values[8];
        return true;
    }
    if (esp && strcmp(name, "Node9") == 0) {
        if (count != 1) return false;
        out.type = NODE9;
        strcpy(out.data.node9Data.nodeName, "Node9");
        out.data.node9Data.ph = values[0];
        return true;
    }
    if (esp && strcmp(name, "Node10") == 0) {
        if (count != 1) return false;
        out.type = NODE10;
        strcpy(out.data.node10Data.nodeName, "Node10");
        out.data.node10Data.cf = values[0];
        return true;
    }
    if (esp && strcmp(name, "Node11") == 0) {
        if (count != 2) return false;
        if (!integerValue(values[0])) return false;
        out.type = NODE11;
        strcpy(out.data.node11Data.nodeName, "Node11");
        out.data.node11Data.pir = int(values[0]);
        out.data.node11Data.battery = values[1];
        return true;
    }
    if (lora && strcmp(name, "lora1") == 0) {
        if (count != 2) return false;
        if (!integerValue(values[1])) return false;
        out.type = LORA1;
        strcpy(out.data.lora1Data.nodeName, "lora1");
        out.data.lora1Data.battery = values[0];
        out.data.lora1Data.water = int(values[1]);
        return true;
    }
    if (lora && strcmp(name, "lora2") == 0) {
        if (count != 3) return false;
        out.type = LORA2;
        strcpy(out.data.lora2Data.nodeName, "lora2");
        out.data.lora2Data.temperature = values[0];
        out.data.lora2Data.humidity = values[1];
        out.data.lora2Data.battery = values[2];
        return true;
    }
    return false;
}
