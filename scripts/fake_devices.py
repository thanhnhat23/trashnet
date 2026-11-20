"""Run simulated CAM + actuator on the LAN for UI/dev work without hardware.

    python scripts/fake_devices.py        # prints the URLs to paste into Devices > manual
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "server" / "tests"))
from fake_devices import FakeDevices  # noqa: E402

if __name__ == "__main__":
    fd = FakeDevices()
    print(f"cam http://127.0.0.1:{fd.cam_port}\nactuator http://127.0.0.1:{fd.act_port}\nCtrl+C to stop")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        fd.stop()
