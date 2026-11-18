"""Strict, bounded application-image checks for reviewed ArduinoOTA packages.

Not a bootloader flasher, signature verifier or board/pin detector. The package
manifest must come from a reviewed exact-target build, not an arbitrary download.
Layout reference: esptool 5.3.1 ESP32FirmwareImage and ESP-IDF esp_app_desc_t.
"""
import hashlib
import re
import struct
from dataclasses import dataclass

MAX_IMAGE = 8 * 1024 * 1024
CHIPS = {0:'esp32', 2:'esp32s2', 5:'esp32c3', 9:'esp32s3', 12:'esp32c2',
         13:'esp32c6', 16:'esp32h2', 18:'esp32p4', 23:'esp32c5'}
SHA256 = re.compile(r'[0-9a-f]{64}\Z')


def digest(value):
    if not isinstance(value, str) or not SHA256.fullmatch(value):
        raise ValueError('Expected SHA256 digest')
    return value


@dataclass(frozen=True)
class AppImage:
    sha256: str
    size: int
    chip_id: int
    flash_bytes: int
    min_revision: int
    max_revision: int
    secure_version: int
    app_version: str
    project: str


def inspect_image(data: bytes) -> AppImage:
    if not isinstance(data, bytes) or not 288 <= len(data) <= MAX_IMAGE:
        raise ValueError('Application image size is invalid')
    if data[0] != 0xE9 or not 1 <= data[1] <= 16:
        raise ValueError('Not an ESP application image')
    chip = struct.unpack_from('<H', data, 12)[0]
    if chip not in CHIPS:
        raise ValueError('Unsupported image chip')
    flash_code = data[3] >> 4
    if flash_code > 7 or data[23] != 1:
        raise ValueError('Flash geometry or appended SHA256 is missing')
    pos, checksum, first = 24, 0xEF, None
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError('Truncated segment header')
        _, size = struct.unpack_from('<II', data, pos)
        pos += 8
        if size > MAX_IMAGE or pos + size > len(data):
            raise ValueError('Truncated image segment')
        segment = data[pos:pos+size]
        if first is None:
            first = segment
        for value in segment:
            checksum ^= value
        pos += size
    checksum_offset = pos + (15 - pos % 16)
    if checksum_offset + 33 != len(data):
        raise ValueError('Only a single application image is accepted; no merged/padded/signed images')
    if any(data[pos:checksum_offset]) or data[checksum_offset] != checksum:
        raise ValueError('Image checksum or alignment is invalid')
    if hashlib.sha256(data[:checksum_offset+1]).digest() != data[checksum_offset+1:]:
        raise ValueError('Image SHA256 verification failed')
    if len(first) < 256 or struct.unpack_from('<I', first)[0] != 0xABCD5432:
        raise ValueError('Application descriptor missing; bootloader images are not allowed')
    def ctext(start, length):
        raw = first[start:start+length].split(b'\0', 1)[0]
        try:
            value = raw.decode('utf-8')
        except UnicodeError as exc:
            raise ValueError('Invalid application descriptor text') from exc
        if not value or any(ord(c) < 32 for c in value):
            raise ValueError('Missing application descriptor text')
        return value
    min_rev = struct.unpack_from('<H', data, 15)[0] or data[14] * 100
    max_rev = struct.unpack_from('<H', data, 17)[0]
    return AppImage(hashlib.sha256(data).hexdigest(), len(data), chip,
                    (1 << flash_code) * 1024 * 1024, min_rev, max_rev,
                    struct.unpack_from('<I', first, 4)[0], ctext(16,32), ctext(48,32))
