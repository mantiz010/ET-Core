"""Produce the credential-free portable Thread OTA receiver example."""
from pathlib import Path
import hashlib,json,zipfile
R=Path(__file__).resolve().parents[1];lib=R/'arduino/libraries';example=lib/'ETCore/examples/C6ThreadOTA'
files={}
for name in ['C6ThreadOTA.ino','build_opt.h']:
 data=(example/name).read_text()
 if name.endswith('.ino'):
  for header in ['ETCore.h','ETCoreOpenThread.h','ETCoreOTAESP32.h']:
   data=data.replace('#include <'+header+'>','#include "src/etcore/'+header+'"')
 files['C6ThreadOTA/'+name]=data.encode()
for library,dest in [('ETCore','etcore'),('ETChaCha20Poly1305','crypto')]:
 for p in (lib/library/'src').rglob('*'):
  if not p.is_file():continue
  b=p.read_bytes()
  if p.name=='ETCore.h':b=b.replace(b'#include <ETChaCha20Poly1305.h>',b'#include "../crypto/ETChaCha20Poly1305.h"')
  files['C6ThreadOTA/src/'+dest+'/'+str(p.relative_to(lib/library/'src'))]=b
files['READ-FIRST.txt']=b'''C6 THREAD OTA RECEIVER PREVIEW - PUBLIC UNCONFIGURED COPY\nExtract first. Open C6ThreadOTA/C6ThreadOTA.ino. Keep src and build_opt.h. Configure your private existing Thread dataset, direct HA IPv6 endpoint and ET-Core key. Never publish the configured copy.\nArduino-ESP32 3.3.11, ArduinoJson7. ESP32C6 Dev Module; USB CDC enabled;4MB;Default partition;Zigbee disabled;115200. No WiFi or external pins.\nFirst install requires USB. Requires matching experimental HA Thread OTA server. Physical transfer and rollback are not yet verified. See THREAD-OTA-PREVIEW.md.\n'''
files['SHA256.json']=json.dumps({n:hashlib.sha256(b).hexdigest() for n,b in files.items()},indent=2).encode()
with zipfile.ZipFile(R/'downloads/C6OTA.zip','w',zipfile.ZIP_DEFLATED) as z:
 for n,b in files.items():z.writestr(n,b)
print('Public portable C6OTA.zip generated without private configuration')
