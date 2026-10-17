DOMAIN = "etcore"
CONF_PSK_HEX = "psk_hex"
CONF_PORT = "port"

MULTICAST_GROUP = "239.10.0.2"
PORT = 5560

LEASE_SWEEP_INTERVAL = 1
DEFAULT_LEASE_TTL = 6
ANNOUNCE_INTERVAL = 30  # seconds; one multicast for the whole site, never answered

# Effects a light.rgb offers when its hello lists none: the same 35 as ET-Bus, so
# ET-Bus light sketches (e.g. RGB1) port without changes.
DEFAULT_RGB_EFFECTS = [
    "solid", "white", "warm_white", "cool_white", "night_light", "rainbow",
    "rainbow_cycle", "color_wipe", "color_chase", "color_bounce", "cylon", "scanner",
    "theater_chase", "twinkle", "sparkle", "confetti", "juggle", "pulse",
    "heartbeat", "strobe", "running_lights", "comet", "meteor", "chase",
    "fire", "aurora", "plasma", "pride", "lava", "ocean",
    "christmas", "halloween", "police", "ambulance", "party",
]
