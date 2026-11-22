#pragma once
#include <Arduino.h>
#include <IPAddress.h>
// Application-owned adapter; its lifetime must exceed the ETCore instance.
// bootstrapAddress is a configured unicast peer until authenticated discovery
// learns a hub. This interface does not confer trust on that address.
class ETCoreTransport {
public:
 virtual ~ETCoreTransport() = default;
 virtual bool begin(uint16_t port) = 0;
 virtual int receive(char* data, size_t max, IPAddress& from) = 0;
 virtual bool send(IPAddress to, uint16_t port, const char* data, size_t size) = 0;
 virtual IPAddress bootstrapAddress() const = 0;
 virtual const char* name() const = 0;
};
