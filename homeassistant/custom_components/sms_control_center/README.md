# SMS Control Center

Existing sensor/button IDs and `sms_control_center.send_sms` stay the same.
Configure the integration's transport option to select encrypted ET-Core or the
legacy MQTT backend. ET-Core uses the `sim7600-sms-gateway` / `sms_gateway_v1`
profile, persistent incoming-event deduplication and explicit modem submission
results. See the [gateway setup and reliability contract](../../../arduino/libraries/ETCore/examples/SMSGateway/README.md).

No real phone numbers, network passwords or encryption keys are included.

New installations can choose ET-Core in the initial form without configuring a broker. Existing imported MQTT entries retain their original backend until explicitly switched in Configure options.
