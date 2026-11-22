import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).with_name("generate_key.py")


class KeyGeneratorTests(unittest.TestCase):
    def test_format_and_independent_keys(self):
        keys = [subprocess.check_output([sys.executable, str(TOOL)], text=True).strip() for _ in range(4)]
        for key in keys:
            self.assertRegex(key, r"^[0-9a-f]{64}$")
        self.assertEqual(len(set(keys)), len(keys))

    def test_private_file_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "network.key"
            result = subprocess.run([sys.executable, str(TOOL), "--output", str(target)], capture_output=True)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")
            original = target.read_bytes()
            self.assertRegex(original.decode().strip(), r"^[0-9a-f]{64}$")
            if os.name == "posix":
                self.assertEqual(target.stat().st_mode & 0o777, 0o600)
            result = subprocess.run([sys.executable, str(TOOL), "--output", str(target)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
