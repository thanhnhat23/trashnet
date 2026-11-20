"""Device registry fed by mDNS, UDP broadcast, heartbeats and manual URLs."""
import asyncio
import logging
import socket
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

from config import Settings
from services.event_bus import EventBus

log = logging.getLogger("disc")
UDP_PORT = 5005
OFFLINE_AFTER = 45.0
ROLES = ("cam", "actuator")


@dataclass
class Device:
    role: str
    url: str
    id: str = ""
    source: str = ""
    last_seen: float = field(default_factory=time.time)
    info: dict = field(default_factory=dict)


def normalize_role(raw: str) -> str:
    raw = raw.strip().lower()
    if "act" in raw:
        return "actuator"
    if "cam" in raw:
        return "cam"
    return ""


def normalize_url(raw: str) -> str:
    raw = raw.strip().rstrip("/")
    if not raw:
        return ""
    return raw if raw.startswith(("http://", "https://")) else f"http://{raw}"


class Registry:
    def __init__(self, cfg: Settings, bus: EventBus):
        self.cfg = cfg
        self.bus = bus
        self.devices: dict[str, Device] = {}

    def update(self, role: str, url: str, info: Optional[dict] = None, source: str = "") -> None:
        role = normalize_role(role)
        url = normalize_url(url)
        if not role or not url:
            return
        old = self.devices.get(role)
        dev = Device(role, url, (info or {}).get("id", ""), source, time.time(), dict(info or {}))
        self.devices[role] = dev
        if old is None or old.url != url or time.time() - old.last_seen > OFFLINE_AFTER:
            log.info("device %s -> %s (%s)", role, url, source)
        self.bus.publish("device", self.describe(role))

    def url(self, role: str) -> str:
        """Manual URL from settings wins over discovered URL."""
        manual = normalize_url(self.cfg.device_url(role))
        if manual:
            return manual
        dev = self.devices.get(role)
        return dev.url if dev else ""

    def stream_url(self) -> str:
        dev = self.devices.get("cam")
        base = self.url("cam")
        if not base:
            return ""
        if dev and dev.info.get("stream_url"):
            return dev.info["stream_url"]
        port = (dev.info.get("stream_port") if dev else None) or 81
        host = base.split("://", 1)[1].split("/")[0].split(":")[0]
        return f"http://{host}:{port}/stream"

    def online(self, role: str) -> bool:
        dev = self.devices.get(role)
        if self.cfg.device_url(role):
            return True if dev is None else time.time() - dev.last_seen < OFFLINE_AFTER or dev.source == "manual"
        return bool(dev) and time.time() - dev.last_seen < OFFLINE_AFTER

    def describe(self, role: str) -> dict:
        dev = self.devices.get(role)
        return {
            "role": role,
            "url": self.url(role),
            "online": self.online(role),
            "source": dev.source if dev else ("manual" if self.cfg.device_url(role) else ""),
            "id": dev.id if dev else "",
            "info": dev.info if dev else {},
            "age": round(time.time() - dev.last_seen, 1) if dev else None,
        }

    def snapshot(self) -> list[dict]:
        return [self.describe(r) for r in ROLES]

    def sweep(self) -> None:
        for role in ROLES:
            dev = self.devices.get(role)
            if dev and dev.source != "manual" and time.time() - dev.last_seen > OFFLINE_AFTER:
                self.bus.publish("device", self.describe(role))


class _UdpProtocol(asyncio.DatagramProtocol):
    def __init__(self, on_msg: Callable[[str, str], None]):
        self.on_msg = on_msg

    def datagram_received(self, data: bytes, addr) -> None:
        parts = data.decode("utf-8", "ignore").strip().split(":", 2)
        if len(parts) == 3 and parts[0] == "TRASHNET":
            self.on_msg(parts[1], parts[2])


async def start_udp(registry: Registry):
    """Listen for TRASHNET:<ROLE>:<url> broadcasts and answer discovery requests."""
    loop = asyncio.get_running_loop()
    try:
        transport, _ = await loop.create_datagram_endpoint(
            lambda: _UdpProtocol(lambda role, url: registry.update(role, url, source="udp")),
            local_addr=("0.0.0.0", UDP_PORT), reuse_port=hasattr(socket, "SO_REUSEPORT") or None)
    except OSError as exc:
        log.warning("udp listener disabled: %s", exc)
        return None
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        probe.sendto(b"TRASHNET:DISCOVER", ("255.255.255.255", UDP_PORT))
        probe.close()
    except OSError:
        pass
    return transport


async def start_mdns(registry: Registry, http_port: int):
    """Browse _trashnet._tcp and advertise _trashnet-server._tcp (optional dependency)."""
    try:
        from zeroconf import ServiceStateChange
        from zeroconf.asyncio import AsyncServiceBrowser, AsyncServiceInfo, AsyncZeroconf
    except ImportError:
        log.info("zeroconf not installed; mDNS disabled")
        return None
    try:
        azc = AsyncZeroconf()
    except Exception as exc:  # noqa: BLE001
        log.warning("mdns disabled: %s", exc)
        return None

    async def resolve(name: str) -> None:
        info = AsyncServiceInfo("_trashnet._tcp.local.", name)
        if not await info.async_request(azc.zeroconf, 2000):
            return
        props = {k.decode(): (v or b"").decode() for k, v in info.properties.items()}
        addrs = info.parsed_addresses()
        if addrs and props.get("role"):
            registry.update(props["role"], f"http://{addrs[0]}:{info.port}", props, "mdns")

    def on_change(zeroconf, service_type, name, state_change):
        if state_change in (ServiceStateChange.Added, ServiceStateChange.Updated):
            asyncio.ensure_future(resolve(name))

    AsyncServiceBrowser(azc.zeroconf, "_trashnet._tcp.local.", handlers=[on_change])
    try:
        ip = _local_ip()
        svc = AsyncServiceInfo("_trashnet-server._tcp.local.", "TrashNet Server._trashnet-server._tcp.local.",
                               addresses=[socket.inet_aton(ip)], port=http_port,
                               properties={"api": "1"}, server="trashnet-server.local.")
        await azc.async_register_service(svc)
    except Exception as exc:  # noqa: BLE001
        log.warning("mdns advertise failed: %s", exc)
    return azc


def _local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()
