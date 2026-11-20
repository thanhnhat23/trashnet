"""Wires services together; one instance per app."""
import asyncio
import logging
from typing import Optional

import cv2
import httpx

from config import Settings
from services import discovery, vision
from services.device_clients import ActuatorClient, CameraClient, StreamHub
from services.event_bus import EventBus
from services.history_repo import HistoryRepo
from services.detector import Detector
from services.inference import Classifier
from services.orchestrator import Orchestrator

log = logging.getLogger("app")


class Container:
    def __init__(self, cfg: Optional[Settings] = None, classifier: Optional[Classifier] = None,
                 history: Optional[HistoryRepo] = None, http_port: int = 5001,
                 detector: Optional[Detector] = None):
        self.http_port = http_port
        self.cfg = cfg or Settings()
        self.bus = EventBus()
        self.registry = discovery.Registry(self.cfg, self.bus)
        self.http = httpx.AsyncClient()
        self.cam = CameraClient(self.cfg, self.registry, self.http)
        self.act = ActuatorClient(self.cfg, self.registry, self.http)
        self.hub = StreamHub(self.registry, self.cfg, self.http)
        self.classifier = classifier or Classifier()
        self.detector = detector or Detector()
        self.history = history or HistoryRepo()
        self.ref = vision.TrayReference()
        self.orch = Orchestrator(self)
        self.tasks: list[asyncio.Task] = []
        self._extras: list = []

    @property
    def ref_path(self):
        """Empty-tray reference lives next to the settings file."""
        return self.cfg.path.parent / "reference.jpg"

    def save_reference(self, frame) -> None:
        self.ref_path.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(self.ref_path), frame, [cv2.IMWRITE_JPEG_QUALITY, 92])

    async def start(self, network: bool = True) -> None:
        if self.detector.load():
            log.info("detector ready")
        if self.classifier.load():
            log.info("classifier ready")
        else:
            log.warning("classifier unavailable: %s", self.classifier.error)
        if self.ref_path.exists():
            img = cv2.imread(str(self.ref_path))
            if img is not None:
                self.ref.set(img)
        self.orch.auto = bool(self.cfg.get("auto_enabled"))
        if network:
            self._extras.append(await discovery.start_udp(self.registry))
            self._extras.append(await discovery.start_mdns(self.registry, self.http_port))
        self.tasks = [asyncio.create_task(self.orch.auto_loop()), asyncio.create_task(self.orch.status_loop())]

    async def stop(self) -> None:
        for t in self.tasks:
            t.cancel()
        await self.hub.stop()
        for extra in self._extras:
            if extra is None:
                continue
            if hasattr(extra, "async_close"):
                await extra.async_close()
            elif hasattr(extra, "close"):
                extra.close()
        await self.http.aclose()
