from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'arduino/libraries/ETCore/examples/HydroBucket/HydroBucket.ino').read_text()
a=source.index('bool validBucketClock(');b=source.index('// ================= ET-CORE',a)
fixture=Path(__file__).with_suffix('.cpp').read_text().replace('// INSERT_PRODUCTION_CLOCK',source[a:b])
with tempfile.TemporaryDirectory(prefix='hydro-clock-test-') as directory:
 p=Path(directory);(p/'test.cpp').write_text(fixture)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Production clock validation and nonblocking sync checks passed')
