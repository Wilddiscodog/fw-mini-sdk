#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Hub Browser — pull AR9331 luci-app-datahub records and play audio.

Stdlib only: tkinter, urllib, sqlite3, winsound (Windows).
"""

from __future__ import annotations

import io
import json
import os
import sqlite3
import struct
import sys
import tarfile
import threading
import time
import traceback
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Optional

try:
    import tkinter as tk
    from tkinter import ttk, messagebox, filedialog
except ImportError as e:
    raise SystemExit("需要带 tkinter 的 Python") from e

try:
    import winsound
except ImportError:
    winsound = None  # type: ignore


def _app_dir() -> Path:
    """PyInstaller onefile: 缓存放在 exe 旁, 勿写入 _MEIPASS 临时目录。"""
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


APP_DIR = _app_dir()
CACHE_DIR = APP_DIR / "cache"
FILES_DIR = CACHE_DIR / "files"
META_DB = CACHE_DIR / "meta.db"

DEFAULT_HOST = "192.168.5.11"
DEFAULT_PORT = 80
DEFAULT_KEY = "495f66430e261791"


# --------------- HTTP / Hub ---------------

class HubClient:
    def __init__(self, host: str, port: int = 80, key: str = "", timeout: float = 30.0):
        self.host = host.strip()
        self.port = int(port)
        self.key = key.strip()
        self.timeout = timeout

    def base(self) -> str:
        return f"http://{self.host}:{self.port}/cgi-bin/hub"

    def url(self, **params: Any) -> str:
        q = {k: v for k, v in params.items() if v is not None and v != ""}
        return self.base() + "?" + urllib.parse.urlencode(q)

    def get_bytes(self, **params: Any) -> tuple[bytes, str]:
        req = urllib.request.Request(self.url(**params), method="GET")
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            ctype = resp.headers.get("Content-Type", "")
            return resp.read(), ctype

    def get_json(self, **params: Any) -> dict:
        raw, _ = self.get_bytes(**params)
        return json.loads(raw.decode("utf-8", errors="replace"))

    def stats(self) -> dict:
        return self.get_json(cmd="stats")

    def list_rows(
        self,
        since: int = 0,
        limit: int = 200,
        ch: str = "",
        unread: bool = False,
        dev: str = "",
    ) -> list[dict]:
        params: dict[str, Any] = {
            "cmd": "list",
            "key": self.key,
            "since": since,
            "limit": min(max(limit, 1), 200),
        }
        if ch:
            params["ch"] = ch
        if unread:
            params["unread"] = 1
            if dev:
                params["dev"] = dev
        data = self.get_json(**params)
        if not data.get("ok"):
            raise RuntimeError(data.get("err", "list failed"))
        return data.get("rows") or []

    def get_file(self, rec_id: int, raw: bool = False) -> tuple[bytes, str]:
        params: dict[str, Any] = {"cmd": "get", "key": self.key, "id": rec_id}
        if raw:
            params["raw"] = 1
        return self.get_bytes(**params)

    def export_tgz(self, days: int = 90) -> bytes:
        days = min(max(int(days), 1), 90)
        raw, _ = self.get_bytes(cmd="export", days=days)
        return raw


# --------------- Local cache / meta ---------------

SCHEMA = """
CREATE TABLE IF NOT EXISTS records (
    id INTEGER PRIMARY KEY,
    devid TEXT,
    channel TEXT,
    ts INTEGER,
    dtype TEXT,
    val REAL,
    txt TEXT,
    data TEXT,
    fmt TEXT,
    dur_ms INTEGER,
    bytes INTEGER,
    rate INTEGER,
    local_path TEXT,
    synced_at INTEGER
);
CREATE TABLE IF NOT EXISTS meta (
    k TEXT PRIMARY KEY,
    v TEXT
);
"""


def ensure_dirs() -> None:
    FILES_DIR.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(META_DB) as db:
        db.executescript(SCHEMA)


def upsert_rows(rows: list[dict]) -> int:
    if not rows:
        return 0
    now = int(time.time())
    n = 0
    with sqlite3.connect(META_DB) as db:
        for r in rows:
            rid = r.get("id")
            if rid is None:
                continue
            rate = int(r.get("rate") or 0)
            db.execute(
                """
                INSERT INTO records(id,devid,channel,ts,dtype,val,txt,data,fmt,dur_ms,bytes,rate,synced_at)
                VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)
                ON CONFLICT(id) DO UPDATE SET
                  devid=excluded.devid, channel=excluded.channel, ts=excluded.ts,
                  dtype=excluded.dtype, val=excluded.val, txt=excluded.txt,
                  data=excluded.data, fmt=excluded.fmt, dur_ms=excluded.dur_ms,
                  bytes=excluded.bytes,
                  rate=CASE WHEN excluded.rate>0 THEN excluded.rate ELSE records.rate END,
                  synced_at=excluded.synced_at
                """,
                (
                    rid,
                    r.get("devid") or "",
                    r.get("channel") or "",
                    int(r.get("ts") or 0),
                    r.get("dtype") or "",
                    r.get("val"),
                    r.get("txt") or "",
                    r.get("data") or "",
                    r.get("fmt") or "",
                    int(r.get("dur_ms") or 0),
                    int(r.get("bytes") or 0),
                    rate,
                    now,
                ),
            )
            n += 1
        db.commit()
    return n


def set_local_path(rec_id: int, path: str) -> None:
    with sqlite3.connect(META_DB) as db:
        db.execute("UPDATE records SET local_path=? WHERE id=?", (path, rec_id))
        db.commit()


def load_local_rows(
    ch: str = "",
    dtype: str = "",
    q: str = "",
) -> list[dict]:
    sql = "SELECT id,devid,channel,ts,dtype,val,txt,fmt,dur_ms,bytes,rate,local_path FROM records WHERE 1=1"
    args: list[Any] = []
    if ch:
        sql += " AND channel=?"
        args.append(ch)
    if dtype:
        sql += " AND dtype=?"
        args.append(dtype)
    if q:
        sql += " AND (devid LIKE ? OR txt LIKE ? OR fmt LIKE ? OR CAST(id AS TEXT) LIKE ?)"
        like = f"%{q}%"
        args.extend([like, like, like, like])
    sql += " ORDER BY id DESC"
    with sqlite3.connect(META_DB) as db:
        db.row_factory = sqlite3.Row
        rows = db.execute(sql, args).fetchall()
    return [dict(r) for r in rows]


def import_sqlite_backup(bak_path: Path) -> int:
    """Import records(+rate if present) from hub export db backup."""
    rows: list[dict] = []
    with sqlite3.connect(bak_path) as src:
        src.row_factory = sqlite3.Row
        cols = {r[1] for r in src.execute("PRAGMA table_info(records)")}
        want = ["id", "devid", "channel", "ts", "dtype", "val", "txt", "data", "fmt", "dur_ms", "bytes"]
        have = [c for c in want if c in cols]
        extra_rate = "rate" in cols
        sel = ",".join(have + (["rate"] if extra_rate else []))
        for r in src.execute(f"SELECT {sel} FROM records"):
            d = {k: r[k] for k in have}
            d["rate"] = int(r["rate"]) if extra_rate and r["rate"] is not None else 0
            rows.append(d)
    return upsert_rows(rows)


# --------------- Audio helpers ---------------
# Windows winsound 只能播 PCM WAV; HUB 对 alaw/ulaw 包的是 tag=6/7, 必须先解码.

def _alaw_to_linear(a_val: int) -> int:
    """与手表 walkie_hub.c alaw_to_linear 一致 (ITU G.711 A-law)."""
    a_val ^= 0x55
    t = (a_val & 0x0F) << 4
    seg = (a_val & 0x70) >> 4
    if seg == 0:
        t += 8
    elif seg == 1:
        t += 0x108
    else:
        t += 0x108
        t <<= seg - 1
    return t if (a_val & 0x80) else -t


def _ulaw_to_linear(u_val: int) -> int:
    u_val = ~u_val & 0xFF
    t = ((u_val & 0x0F) << 3) + 0x84
    t <<= (u_val & 0x70) >> 4
    t -= 0x84
    return t if (u_val & 0x80) else -t


def _pcm16_wav_bytes(pcm: bytes, rate: int) -> bytes:
    rate = rate if rate and rate > 0 else 16000
    data_len = len(pcm)
    hdr = b"RIFF" + struct.pack("<I", 36 + data_len) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    hdr += b"data" + struct.pack("<I", data_len)
    return hdr + pcm


def _parse_wav(data: bytes) -> tuple[int, int, int, bytes] | None:
    """Return (tag, channels, rate, payload) or None if not WAV."""
    if len(data) < 44 or data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    # Walk chunks; fmt may not be immediately followed by data
    pos = 12
    tag = ch = rate = 0
    payload = b""
    while pos + 8 <= len(data):
        cid = data[pos : pos + 4]
        sz = struct.unpack_from("<I", data, pos + 4)[0]
        body = pos + 8
        if cid == b"fmt " and sz >= 16:
            tag, ch, rate = struct.unpack_from("<HHI", data, body)
        elif cid == b"data":
            payload = data[body : body + sz]
            break
        pos = body + sz + (sz & 1)
    if not payload or rate <= 0:
        return None
    return tag, ch, rate, payload


def to_playable_wav(
    data: bytes,
    fmt_hint: str = "",
    rate_hint: int = 0,
) -> bytes:
    """Convert hub payload to PCM16 mono WAV for winsound / 通用播放器."""
    fmt_hint = (fmt_hint or "").lower().strip()
    parsed = _parse_wav(data)
    if parsed:
        tag, _ch, rate, payload = parsed
        rate = rate or rate_hint or 16000
        if tag == 1:  # PCM
            return data if _ch == 1 else _pcm16_wav_bytes(payload, rate)
        if tag == 6 or fmt_hint == "alaw":
            pcm = b"".join(struct.pack("<h", _alaw_to_linear(x)) for x in payload)
            return _pcm16_wav_bytes(pcm, rate)
        if tag == 7 or fmt_hint == "ulaw":
            pcm = b"".join(struct.pack("<h", _ulaw_to_linear(x)) for x in payload)
            return _pcm16_wav_bytes(pcm, rate)
        # unknown compressed wav tag — return as-is
        return data

    # raw body
    rate = rate_hint or 16000
    if fmt_hint == "alaw":
        pcm = b"".join(struct.pack("<h", _alaw_to_linear(x)) for x in data)
        return _pcm16_wav_bytes(pcm, rate)
    if fmt_hint == "ulaw":
        pcm = b"".join(struct.pack("<h", _ulaw_to_linear(x)) for x in data)
        return _pcm16_wav_bytes(pcm, rate)
    if fmt_hint == "pcm":
        return _pcm16_wav_bytes(data, rate)
    return data


# 与 hub-scan-music EXT 对齐 + 对讲编码
AUDIO_EXTS = {
    "mp3", "wav", "flac", "m4a", "aac", "ogg", "opus", "amr", "ape", "wma",
    "alaw", "ulaw", "pcm",
}


def sniff_audio_fmt(data: bytes) -> str:
    """根据文件头推断扩展名; 无法识别返回空。"""
    if len(data) < 12:
        return ""
    if data.startswith(b"MAC "):
        return "ape"
    if data.startswith(b"fLaC"):
        return "flac"
    if data.startswith(b"OggS"):
        return "ogg"
    if data.startswith(b"#!AMR"):
        return "amr"
    if data[0:3] == b"ID3" or data[0:2] in (b"\xff\xfb", b"\xff\xf3", b"\xff\xf2"):
        return "mp3"
    if data[0:4] == b"RIFF" and data[8:12] == b"WAVE":
        return "wav"
    if data[4:8] == b"ftyp":
        # m4a / mp4 / aac in mp4
        brand = data[8:12]
        if brand in (b"M4A ", b"M4B ", b"mp42", b"isom", b"mp41"):
            return "m4a"
        return "m4a"
    if data.startswith(b"\x30\x26\xb2\x75\x8e\x66\xcf\x11"):
        return "wma"
    return ""


def resolve_fmt(fmt_hint: str, data: bytes = b"") -> str:
    fmt = (fmt_hint or "").lower().strip().lstrip(".")
    if fmt in ("", "bin", "file", "octet-stream"):
        fmt = sniff_audio_fmt(data) or fmt
    if fmt in AUDIO_EXTS:
        return fmt
    sniffed = sniff_audio_fmt(data)
    return sniffed or (fmt if fmt else "bin")


def safe_title(name: str, max_len: int = 60) -> str:
    bad = '<>:"/\\|?*\0'
    out = "".join("_" if c in bad else c for c in (name or "").strip())
    out = out.rstrip(" .")
    if not out:
        return ""
    return out[:max_len]


def media_filename(rec_id: int, fmt: str, title: str = "") -> str:
    """id-歌名.ext ; 对讲等无标题则 id.ext"""
    fmt = (fmt or "bin").lower()
    if fmt in ("alaw", "ulaw", "pcm"):
        fmt = "wav"
    stem = safe_title(title)
    if stem:
        return f"{rec_id}-{stem}.{fmt}"
    return f"{rec_id}.{fmt}"


def save_playable_file(
    dest: Path,
    data: bytes,
    fmt_hint: str = "",
    rate_hint: int = 0,
    title: str = "",
    rec_id: int = 0,
) -> Path:
    """按真实格式写文件; alaw/ulaw/pcm → PCM16 .wav; 音乐保留 ape/flac/mp3…"""
    fmt = resolve_fmt(fmt_hint, data)
    rid = rec_id or _id_from_name(dest)
    if fmt in ("alaw", "ulaw", "pcm") or (
        len(data) >= 44 and data[0:4] == b"RIFF" and data[8:12] == b"WAVE"
        and (_parse_wav(data) or (0, 0, 0, b""))[0] in (1, 6, 7)
    ):
        # 对讲 WAV: 统一落 PCM; 若已是 PCM tag=1 也走 to_playable 保持一致
        parsed = _parse_wav(data)
        if parsed and parsed[0] == 1 and fmt in ("", "wav", "pcm"):
            out = data
        else:
            out = to_playable_wav(data, fmt_hint=fmt or "alaw", rate_hint=rate_hint)
        dest = FILES_DIR / media_filename(rid, "wav", title)
        dest.write_bytes(out)
        return dest

    dest = FILES_DIR / media_filename(rid, fmt, title)
    dest.write_bytes(data)
    return dest


def _id_from_name(path: Path) -> int:
    stem = path.stem.split("-", 1)[0]
    try:
        return int(stem)
    except ValueError:
        return 0


def play_file(path: Path) -> None:
    path = Path(path)
    if not path.is_file():
        raise FileNotFoundError(str(path))
    if winsound and path.suffix.lower() == ".wav":
        raw = path.read_bytes()
        parsed = _parse_wav(raw)
        # 旧缓存可能是 A-law/μ-law WAV, winsound 播不了 → 就地转 PCM16
        if parsed and parsed[0] in (6, 7):
            path.write_bytes(to_playable_wav(raw))
        winsound.PlaySound(str(path), winsound.SND_FILENAME | winsound.SND_ASYNC)
        return
    os.startfile(str(path))  # type: ignore[attr-defined]


def stop_playback() -> None:
    if winsound:
        winsound.PlaySound(None, winsound.SND_PURGE)


def fmt_ts(ts: int) -> str:
    if not ts:
        return "-"
    try:
        return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")
    except (OSError, OverflowError, ValueError):
        return str(ts)


def fmt_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


# --------------- GUI ---------------

class HubBrowserApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Hub Browser — datahub 拉取 / 播放")
        self.geometry("1100x640")
        self.minsize(900, 480)

        ensure_dirs()
        self._busy = False
        self._rows: list[dict] = []

        self.host_var = tk.StringVar(value=DEFAULT_HOST)
        self.port_var = tk.StringVar(value=str(DEFAULT_PORT))
        self.key_var = tk.StringVar(value=DEFAULT_KEY)
        self.ch_var = tk.StringVar(value="")
        self.q_var = tk.StringVar(value="")
        self.days_var = tk.StringVar(value="90")
        self.status_var = tk.StringVar(value="就绪")

        self._build()
        self.after(100, self.refresh_local)

    def client(self) -> HubClient:
        return HubClient(self.host_var.get(), int(self.port_var.get() or 80), self.key_var.get())

    def _build(self) -> None:
        top = ttk.Frame(self, padding=8)
        top.pack(fill=tk.X)

        ttk.Label(top, text="HUB IP").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(top, textvariable=self.host_var, width=16).grid(row=0, column=1, padx=4)
        ttk.Label(top, text="端口").grid(row=0, column=2)
        ttk.Entry(top, textvariable=self.port_var, width=6).grid(row=0, column=3, padx=4)
        ttk.Label(top, text="Key").grid(row=0, column=4)
        ttk.Entry(top, textvariable=self.key_var, width=22).grid(row=0, column=5, padx=4)

        btns = ttk.Frame(top)
        btns.grid(row=0, column=6, padx=8)
        ttk.Button(btns, text="读 stats", command=self.on_stats).pack(side=tk.LEFT, padx=2)
        ttk.Button(btns, text="刷新列表(≤200)", command=self.on_list).pack(side=tk.LEFT, padx=2)
        ttk.Button(btns, text="全量同步", command=self.on_full_sync).pack(side=tk.LEFT, padx=2)

        filt = ttk.Frame(self, padding=(8, 0))
        filt.pack(fill=tk.X)
        ttk.Label(filt, text="通道").pack(side=tk.LEFT)
        ch = ttk.Combobox(
            filt,
            textvariable=self.ch_var,
            values=["", "audio", "music"],
            width=10,
            state="normal",
        )
        ch.pack(side=tk.LEFT, padx=4)
        ttk.Label(filt, text="搜索").pack(side=tk.LEFT, padx=(12, 0))
        ttk.Entry(filt, textvariable=self.q_var, width=24).pack(side=tk.LEFT, padx=4)
        ttk.Button(filt, text="本地过滤", command=self.refresh_local).pack(side=tk.LEFT, padx=2)
        ttk.Label(filt, text="export天数").pack(side=tk.LEFT, padx=(12, 0))
        ttk.Entry(filt, textvariable=self.days_var, width=4).pack(side=tk.LEFT, padx=4)

        mid = ttk.Frame(self, padding=8)
        mid.pack(fill=tk.BOTH, expand=True)

        cols = ("id", "ts", "devid", "channel", "fmt", "bytes", "dur", "txt", "file")
        self.tree = ttk.Treeview(mid, columns=cols, show="headings", selectmode="browse")
        headings = {
            "id": ("ID", 60),
            "ts": ("时间", 150),
            "devid": ("设备", 110),
            "channel": ("通道", 70),
            "fmt": ("格式", 60),
            "bytes": ("大小", 80),
            "dur": ("时长", 60),
            "txt": ("txt/收件人", 140),
            "file": ("本地", 60),
        }
        for c, (title, w) in headings.items():
            self.tree.heading(c, text=title)
            self.tree.column(c, width=w, anchor=tk.W if c in ("devid", "txt") else tk.CENTER)

        vsb = ttk.Scrollbar(mid, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        vsb.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.bind("<Double-1>", lambda e: self.on_play())

        bottom = ttk.Frame(self, padding=8)
        bottom.pack(fill=tk.X)
        ttk.Button(bottom, text="播放", command=self.on_play).pack(side=tk.LEFT, padx=2)
        ttk.Button(bottom, text="停止", command=stop_playback).pack(side=tk.LEFT, padx=2)
        ttk.Button(bottom, text="下载选中", command=self.on_download_one).pack(side=tk.LEFT, padx=2)
        ttk.Button(bottom, text="打开缓存目录", command=self.on_open_cache).pack(side=tk.LEFT, padx=2)
        ttk.Button(bottom, text="导出选中…", command=self.on_export_one).pack(side=tk.LEFT, padx=2)

        ttk.Label(bottom, textvariable=self.status_var).pack(side=tk.RIGHT)

        self.prog = ttk.Progressbar(self, mode="determinate")
        self.prog.pack(fill=tk.X, padx=8, pady=(0, 8))

    # ----- threading helpers -----

    def set_status(self, msg: str) -> None:
        self.status_var.set(msg)

    def run_bg(self, work: Callable[[], None], done: Optional[Callable[[], None]] = None) -> None:
        if self._busy:
            messagebox.showinfo("忙", "请等待当前任务结束")
            return

        def wrapper() -> None:
            self._busy = True
            try:
                work()
            except Exception as e:
                err = f"{e}\n{traceback.format_exc()}"
                self.after(0, lambda: messagebox.showerror("错误", err))
            finally:
                self._busy = False
                if done:
                    self.after(0, done)

        threading.Thread(target=wrapper, daemon=True).start()

    # ----- actions -----

    def on_stats(self) -> None:
        def work() -> None:
            self.after(0, lambda: self.set_status("读取 stats…"))
            data = self.client().stats()
            key = data.get("key") or ""
            if key:
                self.after(0, lambda: self.key_var.set(key))
            msg = (
                f"records={data.get('records')} today={data.get('today')} "
                f"db={fmt_size(int(data.get('db_bytes') or 0))} "
                f"key={'已填' if key else '无'}"
            )
            recent = data.get("recent") or []
            if recent:
                upsert_rows(recent)
            self.after(0, lambda: self.set_status(msg))

        self.run_bg(work, self.refresh_local)

    def on_list(self) -> None:
        def work() -> None:
            self.after(0, lambda: self.set_status("list 拉取…"))
            rows = self.client().list_rows(since=0, limit=200, ch=self.ch_var.get().strip())
            n = upsert_rows(rows)
            self.after(0, lambda: self.set_status(f"list 写入 {n} 条（单次最多 200）"))

        self.run_bg(work, self.refresh_local)

    def on_full_sync(self) -> None:
        days = int(self.days_var.get() or 90)

        def work() -> None:
            cli = self.client()
            self.after(0, lambda: self.set_status(f"export({days}天)…"))
            self.after(0, lambda: self.prog.configure(value=0, maximum=100))

            tgz = cli.export_tgz(days=days)
            tgz_path = CACHE_DIR / "export.tar.gz"
            tgz_path.write_bytes(tgz)

            extract_dir = CACHE_DIR / "export_unpack"
            if extract_dir.exists():
                for p in extract_dir.rglob("*"):
                    if p.is_file():
                        try:
                            p.unlink()
                        except OSError:
                            pass
            extract_dir.mkdir(parents=True, exist_ok=True)

            with tarfile.open(fileobj=io.BytesIO(tgz), mode="r:gz") as tar:
                tar.extractall(extract_dir)

            bak = None
            for cand in extract_dir.rglob("datahub.db.bak"):
                bak = cand
                break
            if not bak:
                raise RuntimeError("export 中未找到 datahub.db.bak")

            nmeta = import_sqlite_backup(bak)
            self.after(0, lambda: self.set_status(f"已导入元数据 {nmeta} 条，下载附件…"))

            # Copy any files already in the tarball into cache by scanning records
            with sqlite3.connect(META_DB) as db:
                rows = db.execute(
                    "SELECT id,fmt,bytes,rate,dtype,txt FROM records WHERE dtype='file' ORDER BY id"
                ).fetchall()

            total = len(rows) or 1
            self.after(0, lambda: self.prog.configure(maximum=total, value=0))

            got = 0
            for i, (rid, fmt, nbytes, rate, dtype, txt) in enumerate(rows):
                dest = self._find_cached(rid) or self._local_target(
                    rid, fmt or "bin", title=txt or ""
                )
                if dest.is_file() and dest.stat().st_size > 0:
                    try:
                        raw = dest.read_bytes()
                        # 纠正扩展名 / A-law→PCM
                        fixed = save_playable_file(
                            dest,
                            raw,
                            fmt_hint=fmt or "",
                            rate_hint=int(rate or 0),
                            title=txt or "",
                            rec_id=rid,
                        )
                        if fixed != dest and dest.suffix.lower() == ".bin":
                            try:
                                dest.unlink()
                            except OSError:
                                pass
                        dest = fixed
                    except OSError:
                        pass
                    set_local_path(rid, str(dest))
                    got += 1
                else:
                    try:
                        raw, _ctype = cli.get_file(rid, raw=False)
                        dest = save_playable_file(
                            dest,
                            raw,
                            fmt_hint=fmt or "",
                            rate_hint=int(rate or 0),
                            title=txt or "",
                            rec_id=rid,
                        )
                        set_local_path(rid, str(dest))
                        got += 1
                    except Exception as e:
                        print(f"get id={rid} fail: {e}")
                if i % 2 == 0:
                    self.after(0, lambda v=i + 1: self.prog.configure(value=v))
                    self.after(
                        0,
                        lambda i=i, t=total, g=got: self.set_status(
                            f"下载附件 {i + 1}/{t}（成功 {g}）"
                        ),
                    )

            self.after(0, lambda: self.prog.configure(value=total))
            self.after(
                0,
                lambda: self.set_status(f"全量同步完成：元数据 {nmeta}，附件成功 {got}/{len(rows)}"),
            )

        self.run_bg(work, self.refresh_local)

    def _local_target(self, rid: int, fmt: str, title: str = "") -> Path:
        fmt = resolve_fmt(fmt or "")
        return FILES_DIR / media_filename(rid, fmt, title)

    def _find_cached(self, rid: int) -> Optional[Path]:
        """按 id 前缀查找已下载文件 (兼容旧 .bin)."""
        hits = sorted(FILES_DIR.glob(f"{rid}.*")) + sorted(FILES_DIR.glob(f"{rid}-*"))
        for p in hits:
            if p.is_file() and p.stat().st_size > 0:
                return p
        return None

    def refresh_local(self) -> None:
        rows = load_local_rows(ch=self.ch_var.get().strip(), q=self.q_var.get().strip())
        self._rows = rows
        self.tree.delete(*self.tree.get_children())
        for r in rows:
            dur = r.get("dur_ms") or 0
            dur_s = f"{dur / 1000:.1f}s" if dur else "-"
            has = "有" if r.get("local_path") and Path(r["local_path"]).is_file() else "-"
            self.tree.insert(
                "",
                tk.END,
                iid=str(r["id"]),
                values=(
                    r["id"],
                    fmt_ts(int(r.get("ts") or 0)),
                    r.get("devid") or "",
                    r.get("channel") or "",
                    r.get("fmt") or "",
                    fmt_size(int(r.get("bytes") or 0)),
                    dur_s,
                    r.get("txt") or "",
                    has,
                ),
            )
        self.set_status(f"本地 {len(rows)} 条")

    def selected_id(self) -> Optional[int]:
        sel = self.tree.selection()
        if not sel:
            return None
        return int(sel[0])

    def ensure_local_file(self, rid: int) -> Path:
        with sqlite3.connect(META_DB) as db:
            row = db.execute(
                "SELECT fmt,rate,local_path,dtype,txt FROM records WHERE id=?", (rid,)
            ).fetchone()
        if not row:
            raise RuntimeError(f"无记录 id={rid}")
        fmt, rate, local_path, dtype, title = row
        title = title or ""
        if dtype and dtype != "file":
            raise RuntimeError("该条不是文件附件")
        path = None
        if local_path and Path(local_path).is_file():
            path = Path(local_path)
        else:
            path = self._find_cached(rid)
        if path and path.is_file():
            raw = path.read_bytes()
            fixed = save_playable_file(
                path,
                raw,
                fmt_hint=fmt or "",
                rate_hint=int(rate or 0),
                title=title,
                rec_id=rid,
            )
            if fixed != path and path.suffix.lower() == ".bin":
                try:
                    path.unlink()
                except OSError:
                    pass
            set_local_path(rid, str(fixed))
            return fixed

        dest = self._local_target(rid, fmt or "bin", title=title)
        raw, _ctype = self.client().get_file(rid, raw=False)
        dest = save_playable_file(
            dest,
            raw,
            fmt_hint=fmt or "",
            rate_hint=int(rate or 0),
            title=title,
            rec_id=rid,
        )
        set_local_path(rid, str(dest))
        return dest

    def on_play(self) -> None:
        rid = self.selected_id()
        if rid is None:
            messagebox.showinfo("提示", "请先选中一条记录")
            return

        def work() -> None:
            self.after(0, lambda: self.set_status(f"准备播放 id={rid}…"))
            path = self.ensure_local_file(rid)
            self.after(0, lambda: play_file(path))
            self.after(0, lambda: self.set_status(f"播放 {path.name}"))

        self.run_bg(work, self.refresh_local)

    def on_download_one(self) -> None:
        rid = self.selected_id()
        if rid is None:
            messagebox.showinfo("提示", "请先选中一条记录")
            return

        def work() -> None:
            path = self.ensure_local_file(rid)
            self.after(0, lambda: self.set_status(f"已保存 {path}"))

        self.run_bg(work, self.refresh_local)

    def on_export_one(self) -> None:
        rid = self.selected_id()
        if rid is None:
            messagebox.showinfo("提示", "请先选中一条记录")
            return
        try:
            path = self.ensure_local_file(rid)
        except Exception as e:
            messagebox.showerror("错误", str(e))
            return
        dest = filedialog.asksaveasfilename(
            title="导出文件",
            initialfile=path.name,
            defaultextension=path.suffix or ".bin",
        )
        if dest:
            Path(dest).write_bytes(path.read_bytes())
            self.set_status(f"已导出 {dest}")

    def on_open_cache(self) -> None:
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        os.startfile(str(CACHE_DIR))  # type: ignore[attr-defined]


def main() -> None:
    ensure_dirs()
    app = HubBrowserApp()
    app.mainloop()


if __name__ == "__main__":
    main()
