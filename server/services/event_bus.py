"""In-process pub/sub used by the SSE endpoint."""
import asyncio
import json


class EventBus:
    def __init__(self) -> None:
        self._subs: set[asyncio.Queue] = set()

    @property
    def count(self) -> int:
        return len(self._subs)

    def open(self) -> asyncio.Queue:
        q: asyncio.Queue = asyncio.Queue(maxsize=64)
        self._subs.add(q)
        return q

    def close(self, q: asyncio.Queue) -> None:
        self._subs.discard(q)

    def publish(self, kind: str, data: dict) -> None:
        msg = {"type": kind, "data": data}
        for q in list(self._subs):
            if q.full():
                try:
                    q.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            q.put_nowait(msg)


def sse_format(msg: dict) -> str:
    return f"event: {msg['type']}\ndata: {json.dumps(msg['data'])}\n\n"
