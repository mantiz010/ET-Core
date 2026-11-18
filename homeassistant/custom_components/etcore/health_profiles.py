"""Device-aware diagnostic profiles. Never change entity availability or lease TTL."""
from __future__ import annotations
import math

PROFILES = {
    "auto": "Automatic",
    "wired_service": "Wired service",
    "wired_device": "Wired device",
    "wifi_device": "Wi-Fi device",
    "thread_device": "Thread device",
    "sleeping_sensor": "Sleep-capable sensor",
    "unknown": "Unknown connection",
}


def valid_signal(value: object) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and -127 <= value < 0 and math.isfinite(value))


def describe_device(device: dict, override: str = "auto") -> dict:
    hello = device.get("hello") if isinstance(device.get("hello"), dict) else {}
    transport = hello.get("transport")
    transport = transport if transport in ("wifi", "ethernet", "thread") else "unknown"
    interval = device.get("sleep_interval")
    sleeps = (isinstance(interval, (int, float)) and not isinstance(interval, bool)
              and 0 < interval <= 0xFFFFFFFF and math.isfinite(interval))
    signal = valid_signal(device.get("rssi"))
    selected = override if isinstance(override, str) and override in PROFILES else "auto"
    if selected != "auto":
        kind, source = selected, "configured"
    elif sleeps:
        kind, source = "sleeping_sensor", "sleep_schedule"
    elif hello.get("device_kind") == "service" or str(device.get("class", "")).startswith("service."):
        kind, source = ("wired_service" if transport == "ethernet" else "service"), "device_metadata"
    elif transport == "thread":
        kind, source = "thread_device", "device_metadata"
    elif transport in ("wifi", "ethernet"):
        kind, source = ("wifi_device" if transport == "wifi" else "wired_device"), "device_metadata"
    elif signal:
        kind, source = "wifi_device", "reported_signal"
    else:
        kind, source = "unknown", "unknown"
    if kind == "unknown":
        transport = "unknown"
    if kind in ("wired_service", "wired_device"):
        transport = "ethernet"
    elif kind == "wifi_device":
        transport = "wifi"
    elif kind == "thread_device":
        transport = "thread"
    elif kind == "sleeping_sensor" and signal and transport == "unknown":
        transport = "wifi"
    signal_applicable = transport == "wifi" and kind not in ("wired_service", "wired_device")
    if kind in ("wired_service", "service"):
        check = "Check service restarts, deployments, host health and missed heartbeats. An unavailable service does not prove the host is offline."
        unavailable = "Service heartbeat missing"
    elif kind == "sleeping_sensor":
        check = "Check the expected wake time and last report first. Sleep is normal; investigate missed check-ins after the device's lease expires."
        unavailable = "Sensor check-in overdue" if sleeps else "Sensor heartbeat missing"
    elif kind == "wired_device":
        check = "Check device restarts, Ethernet link and missed heartbeats. No Wi-Fi signal diagnosis applies."
        unavailable = "Device heartbeat missing"
    elif kind == "thread_device":
        check = "Check Thread attachment, the parent and border router, IPv6 reachability and missed heartbeats. Wi-Fi RSSI does not describe this Thread link."
        unavailable = "Thread heartbeat missing"
    elif kind == "wifi_device":
        check = "Check restarts and missed heartbeats, then Wi-Fi association and the reported signal. Missing heartbeats alone do not prove a Wi-Fi or power fault."
        unavailable = "Device heartbeat missing"
    else:
        check = "Confirm the device type, then check restarts and missed heartbeats. Connection and power faults are not established."
        unavailable = "Heartbeat missing"
    return {
        "kind": kind, "label": PROFILES.get(kind, "Software service"),
        "source": source, "configured": selected, "transport": transport,
        "signal_applicable": signal_applicable,
        "signal_valid": signal_applicable and signal,
        "sleep_schedule_known": sleeps, "check": check,
        "unavailable_label": unavailable,
    }
