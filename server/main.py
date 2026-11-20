"""TrashNet server entry point: `python main.py` or `uvicorn main:app`."""
import argparse
import logging
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from api import actuator, camera, devices, history, sort, system
from services.container import Container

class NoCacheStatic(StaticFiles):
    """The UI is tiny and changes often: never let the browser keep an old copy of the JS/CSS."""

    async def get_response(self, path, scope):
        resp = await super().get_response(path, scope)
        resp.headers["Cache-Control"] = "no-cache, must-revalidate"
        return resp


WEB_DIR = Path(__file__).resolve().parent / "web"


def create_app(container: Optional[Container] = None, network: bool = True) -> FastAPI:
    c = container or Container()

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        await c.start(network=network)
        yield
        await c.stop()

    app = FastAPI(title="TrashNet", version="1.0", lifespan=lifespan)
    app.state.tn = c
    for module in (sort, devices, actuator, camera, history, system):
        app.include_router(module.router, prefix="/api/v1")
    if WEB_DIR.exists():
        app.mount("/", NoCacheStatic(directory=WEB_DIR, html=True), name="web")
    return app


if __name__ == "__main__":
    import uvicorn

    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5001)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    uvicorn.run(create_app(Container(http_port=args.port)), host=args.host, port=args.port)
