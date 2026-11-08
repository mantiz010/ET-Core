# Hydro bucket regression tests

Run from any directory on Linux with Python 3, g++, Node.js and AddressSanitizer/UBSan available:

```sh
python3 tests/hydro_bucket/test_ec_model.py
python3 tests/hydro_bucket/test_calibration_controls.py
python3 tests/hydro_bucket/test_clock.py
python3 tests/hydro_bucket/test_bucket_adapter.py
```

The C++ checks extract production functions from the example and compile them with host fakes; the adapter uses a stubbed HA framework. These prove arithmetic, validation, storage/error handling and entity lifecycle behaviour, not physical probe accuracy or dosing hardware. Output files are created in temporary directories. No test contacts Home Assistant or a device.
