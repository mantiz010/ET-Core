"""Package a credential-free, self-contained C6 Thread preview sketch."""
from pathlib import Path
import hashlib,json,zipfile
ROOT=Path(__file__).resolve().parents[1]
lib=ROOT/'arduino/libraries';example=lib/'ETCore/examples/C6Thread'
out=ROOT/'downloads/C6.zip'
files={}
for name in ['C6Thread.ino','build_opt.h']:
 data=(example/name).read_text()
 if name.endswith('.ino'):
  data=data.replace('#include <ETCore.h>','#include "src/etcore/ETCore.h"').replace('#include <ETCoreOpenThread.h>','#include "src/etcore/ETCoreOpenThread.h"')
 files['C6Thread/'+name]=data.encode()
for library,dest in [('ETCore','etcore'),('ETChaCha20Poly1305','crypto')]:
 for p in (lib/library/'src').rglob('*'):
  if not p.is_file():continue
  data=p.read_bytes()
  if library=='ETCore' and p.name=='ETCore.h':data=data.replace(b'#include <ETChaCha20Poly1305.h>',b'#include "../crypto/ETChaCha20Poly1305.h"')
  files['C6Thread/src/'+dest+'/'+p.relative_to(lib/library/'src').as_posix()]=data
files['READ-FIRST.txt']=b'''C6 THREAD PREVIEW - SELF-CONTAINED SKETCH\nExtract into a new folder. Open C6Thread/C6Thread.ino. Keep the src folder and build_opt.h beside it. Matching ET-Core and crypto source are bundled; do not replace your installed ETCore library. ArduinoJson 7 and ESP32 board package 3.3.11 are required.\nBoard: ESP32C6 Dev Module. USB CDC enabled. Flash 4MB. Default partition. Zigbee disabled. Upload speed 115200. Serial 115200.\nPublic copy has placeholders. Operational dataset, isolated test key and reachable IPv6 hub must be configured privately before the radio test. This is not Thread OTA or a hardware-verified release.\n'''
files['SHA256.json']=json.dumps({n:hashlib.sha256(b).hexdigest() for n,b in files.items()},indent=2).encode()
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
 for name,data in files.items():z.writestr(name,data)
print('Created portable public C6.zip with bundled SDK and crypto source')
