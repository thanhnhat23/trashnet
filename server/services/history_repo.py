"""SQLite history with thumbnail files."""
import sqlite3
import threading
import time
import uuid
from pathlib import Path
from typing import Optional

import cv2
import numpy as np

from config import VAR_DIR
from domain import labels


class HistoryRepo:
    def __init__(self, db_path: Path | None = None, thumbs_dir: Path | None = None):
        self.db_path = Path(db_path) if db_path else VAR_DIR / "history.db"
        self.thumbs = Path(thumbs_dir) if thumbs_dir else VAR_DIR / "thumbs"
        self.thumbs.mkdir(parents=True, exist_ok=True)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._db = sqlite3.connect(self.db_path, check_same_thread=False)
        self._db.row_factory = sqlite3.Row
        self._db.execute(
            "CREATE TABLE IF NOT EXISTS history (id TEXT PRIMARY KEY, ts REAL, label TEXT, bin INTEGER,"
            " confidence REAL, uncertain INTEGER, source TEXT)")
        self._db.commit()

    def add(self, label: str, confidence: float, uncertain: bool, source: str,
            image: Optional[np.ndarray] = None) -> dict:
        rec_id = uuid.uuid4().hex[:12]
        rec = {"id": rec_id, "ts": time.time(), "label": label, "bin": labels.bin_of(label),
               "confidence": round(confidence, 4), "uncertain": bool(uncertain), "source": source}
        if image is not None:
            h, w = image.shape[:2]
            scale = 160 / max(h, w)
            thumb = cv2.resize(image, (max(1, int(w * scale)), max(1, int(h * scale))))
            cv2.imwrite(str(self.thumbs / f"{rec_id}.jpg"), thumb, [cv2.IMWRITE_JPEG_QUALITY, 70])
        with self._lock:
            self._db.execute("INSERT INTO history VALUES (?,?,?,?,?,?,?)",
                             (rec["id"], rec["ts"], rec["label"], rec["bin"], rec["confidence"],
                              int(rec["uncertain"]), rec["source"]))
            self._db.commit()
        return rec

    def _row(self, row: sqlite3.Row) -> dict:
        d = dict(row)
        d["uncertain"] = bool(d["uncertain"])
        d["thumb"] = f"/api/v1/history/{d['id']}/thumb" if (self.thumbs / f"{d['id']}.jpg").exists() else ""
        return d

    def list(self, limit: int = 100, offset: int = 0, label: str = "") -> list[dict]:
        sql, args = "SELECT * FROM history", []
        if label:
            sql += " WHERE label=?"
            args.append(label)
        sql += " ORDER BY ts DESC LIMIT ? OFFSET ?"
        args += [limit, offset]
        with self._lock:
            return [self._row(r) for r in self._db.execute(sql, args)]

    def delete(self, rec_id: str) -> None:
        with self._lock:
            self._db.execute("DELETE FROM history WHERE id=?", (rec_id,))
            self._db.commit()
        (self.thumbs / f"{rec_id}.jpg").unlink(missing_ok=True)

    def clear(self) -> None:
        with self._lock:
            self._db.execute("DELETE FROM history")
            self._db.commit()
        for f in self.thumbs.glob("*.jpg"):
            f.unlink(missing_ok=True)

    def stats(self, days: int = 14) -> dict:
        with self._lock:
            by_label = {r["label"]: r["n"] for r in self._db.execute(
                "SELECT label, COUNT(*) n FROM history GROUP BY label")}
            by_day = [dict(r) for r in self._db.execute(
                "SELECT date(ts,'unixepoch','localtime') day, COUNT(*) n FROM history "
                "WHERE ts > ? GROUP BY day ORDER BY day", (time.time() - days * 86400,))]
            avg = self._db.execute("SELECT AVG(confidence) FROM history").fetchone()[0]
            unc = self._db.execute("SELECT COUNT(*) FROM history WHERE uncertain=1").fetchone()[0]
        total = sum(by_label.values())
        return {"total": total, "by_label": {k: by_label.get(k, 0) for k in labels.LABEL_IDS},
                "by_day": by_day, "avg_confidence": round(avg or 0, 4), "uncertain": unc}

    def thumb_path(self, rec_id: str) -> Path:
        return self.thumbs / f"{Path(rec_id).name}.jpg"
