"""Compile and exercise the decoder extracted from the shareable Arduino example."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BresserDecoder(unittest.TestCase):
    @unittest.skipUnless(shutil.which('g++'), 'g++ required for production decoder tests')
    def test_production_decoder(self):
        source = (ROOT / 'arduino/libraries/ETCore/examples/Bresser6In1/Bresser6In1.ino').read_text()
        decoder = source[source.index('uint16_t lfsrDigest16'):source.index('void IRAM_ATTR onPacket')]
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / 'production-decoder.inc').write_text(decoder)
            shutil.copyfile(ROOT / 'tools/fixtures/bresser_decoder.cpp', folder / 'test.cpp')
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-fsanitize=undefined',str(folder/'test.cpp'),'-o',str(folder/'test')], check=True, capture_output=True, timeout=30)
            result = subprocess.run([str(folder/'test')], check=True, capture_output=True, text=True, timeout=10)
            self.assertIn('PASS: production decoder', result.stdout)


if __name__ == '__main__': unittest.main()
