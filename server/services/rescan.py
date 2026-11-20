"""Find devices again: probe known URLs, resolve mDNS names, then sweep the local /24 network."""
import asyncio
import logging
import socket
from typing import Optional

import httpx

from services.discovery import ROLES

log = logging.getLogger("rescan")
NAMES = {"cam": "trashnet-cam.local", "actuator": "trashnet-actuator.local"}


def local_prefixes() -> list[str]:
    """/24 prefixes of this PC's LAN addresses (e.g. '192.168.1')."""
    out: list[str] = []
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("10.255.255.255", 1))
        out.append(s.getsockname()[0].rsplit(".", 1)[0])
        s.close()
    except OSError:
        pass
    try:
        for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
            p = ip.rsplit(".", 1)[0]
            if not ip.startswith("127.") and p not in out:
                out.append(p)
    except OSError:
        pass
    return out


async def probe(http: httpx.AsyncClient, base: str, timeout: float = 1.5) -> Optional[dict]:
    """GET <base>/status; returns the JSON when it looks like a TrashNet device."""
    try:
        r = await http.get(base.rstrip("/") + "/status", timeout=timeout)
        data = r.json()
        return data if isinstance(data, dict) and data.get("role") in ROLES else None
    except (httpx.HTTPError, ValueError):
        return None


async def _resolve(name: str) -> str:
    try:
        infos = await asyncio.get_running_loop().getaddrinfo(name, 80, family=socket.AF_INET)
        return infos[0][4][0] if infos else ""
    except OSError:
        return ""


async def sweep(http: httpx.AsyncClient, prefix: str, want: set[str]) -> dict[str, str]:
    found: dict[str, str] = {}
    sem = asyncio.Semaphore(64)

    async def one(i: int) -> None:
        if not (want - found.keys()):
            return
        base = f"http://{prefix}.{i}"
        async with sem:
            data = await probe(http, base, 0.7)
        if data and data["role"] in want:
            found[data["role"]] = base

    await asyncio.gather(*(one(i) for i in range(1, 255)))
    return found


async def rescan(c, deep: bool = True) -> list[dict]:
    """Re-check every device; deep=True also sweeps the LAN for devices that are missing."""
    reg, http = c.registry, c.http
    missing: set[str] = set()
    for role in ROLES:
        url = reg.url(role)
        data = await probe(http, url) if url else None
        if data and data.get("role") == role:
            reg.update(role, url, data, "probe")
            continue
        # name-based lookup (mDNS / router DNS)
        ip = await _resolve(NAMES[role])
        data = await probe(http, f"http://{ip}") if ip else None
        if data and data.get("role") == role:
            reg.update(role, f"http://{ip}", data, "mdns")
            continue
        reg.mark_down(role)
        missing.add(role)
    if missing and deep:
        for prefix in local_prefixes():
            found = await sweep(http, prefix, missing)
            for role, base in found.items():
                data = await probe(http, base) or {}
                reg.update(role, base, data, "scan")
                missing.discard(role)
            if not missing:
                break
    for role in ROLES:
        c.bus.publish("device", reg.describe(role))
    return reg.snapshot()


async def rescan_loop(c, every: float = 20.0) -> None:
    """Background: when a device is offline, retry quietly (light probe; full sweep every 6th round)."""
    n = 0
    while True:
        await asyncio.sleep(every)
        try:
            if any(not d["online"] for d in c.registry.snapshot()):
                n += 1
                await rescan(c, deep=(n % 6 == 0))
            else:
                n = 0
        except Exception:  # noqa: BLE001
            log.exception("rescan loop")
