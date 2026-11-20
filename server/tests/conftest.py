import sys
import threading
import time
from pathlib import Path

import httpx
import numpy as np
import pytest
import uvicorn

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from config import Settings  # noqa: E402
from domain import labels  # noqa: E402
from fake_devices import FakeDevices, free_port  # noqa: E402
from main import create_app  # noqa: E402
from services.container import Container  # noqa: E402
from services.history_repo import HistoryRepo  # noqa: E402
from services.inference import Classifier  # noqa: E402


def colour_classifier(images_rgb: list[np.ndarray]) -> np.ndarray:
    """Fake model: dominant colour of non-grey pixels picks the label."""
    rows = []
    for img in images_rgb:
        mask = (np.abs(img.astype(int) - 114) > 30).any(axis=2)
        p = np.full(4, 0.05)
        if mask.sum() < 50:
            p[:] = 0.25
        else:
            r, g, b = img[mask].mean(axis=0)
            target = "plastic" if r > 150 and g < 100 else "glass" if g > 120 and r < 100 else \
                "paper" if b > 150 else "other" if r > 150 and g > 150 else None
            if target is None:
                p[:] = 0.25
            else:
                p[labels.LABEL_IDS.index(target)] = 0.85
        rows.append(p / p.sum())
    return np.stack(rows)


class _Server(uvicorn.Server):
    def install_signal_handlers(self) -> None:
        pass


@pytest.fixture()
def env(tmp_path):
    devices = FakeDevices()
    cfg = Settings(tmp_path / "settings.json")
    cfg.update({"cooldown_sec": 0.05, "vote_frames": 3})
    container = Container(cfg, Classifier(predict_fn=colour_classifier), HistoryRepo(tmp_path / "h.db", tmp_path / "t"))
    app = create_app(container, network=False)
    port = free_port()
    server = _Server(uvicorn.Config(app, host="127.0.0.1", port=port, log_level="error"))
    threading.Thread(target=server.run, daemon=True).start()
    for _ in range(100):
        if server.started:
            break
        time.sleep(0.05)
    client = httpx.Client(base_url=f"http://127.0.0.1:{port}/api/v1", timeout=15)
    client.post("/devices/heartbeat", json={"role": "cam", "ip": f"127.0.0.1:{devices.cam_port}",
                                            "stream_port": devices.stream_port})
    client.post("/devices/heartbeat", json={"role": "actuator", "ip": f"127.0.0.1:{devices.act_port}"})
    yield type("Env", (), {"client": client, "devices": devices, "container": container, "port": port})
    client.close()
    server.should_exit = True
    devices.stop()
