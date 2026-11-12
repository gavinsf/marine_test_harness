"""Runs the real nmea_sim binary and records what it puts on the wire and in its log."""
import json
import os
import socket
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
START = 1767225600          # 2026-01-01 00:00:00 UTC: fixed so runs are reproducible


def find_simulator() -> Path:
    """$NMEA_SIM, else build/nmea_sim (configuring and building it if needed)."""
    env = os.environ.get("NMEA_SIM")
    if env:
        path = Path(env)
        if not path.is_file():
            raise FileNotFoundError(f"NMEA_SIM={env} does not exist")
        return path
    build = REPO / "build"
    # Always configure first: it is cheap, and an old build/ may predate the nmea_sim target.
    for cmd in (["cmake", "-S", str(REPO), "-B", str(build), "-Wno-dev"],
                ["cmake", "--build", str(build), "--target", "nmea_sim", "-j"]):
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(f"{' '.join(cmd)} failed:\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
    return build / "nmea_sim"


def latin1(s: str) -> bytes:
    """Log text is JSON with every non-ASCII byte escaped as \\u00XX, i.e. latin-1."""
    return s.encode("latin-1")


class Collector:
    """A UDP socket on a free loopback port, drained by a thread so the kernel never drops datagrams."""

    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.2)
        self.port = self.sock.getsockname()[1]
        self.datagrams: list[bytes] = []
        self.arrivals: list[float] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _loop(self):
        while not self._stop.is_set():
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            self.arrivals.append(time.monotonic())
            self.datagrams.append(data)

    def close(self, settle: float = 0.3):
        time.sleep(settle)          # let in-flight datagrams land
        self._stop.set()
        self._thread.join()
        self.sock.close()


@dataclass
class Run:
    args: list
    datagrams: list
    arrivals: list
    log: list
    returncode: int
    stderr: str
    seconds: float
    rate: int
    count: int
    faults: dict = field(default_factory=dict)

    @property
    def sent(self):
        """Log entries that went on the wire, in the order they were sent."""
        return sorted((r for r in self.log if not r["dropped"]), key=lambda r: r["emit"])

    @property
    def dropped(self):
        return [r for r in self.log if r["dropped"]]

    def wire(self, kind=None):
        return [d for d in self.datagrams if kind is None or d[3:6] == kind.encode()]


def read_log(path: Path) -> list:
    if not path.exists():
        return []
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f]


def execute(sim: Path, workdir: Path, *args, rate=50, count=100, seed=7, start=START, log=True, timeout=None) -> Run:
    workdir.mkdir(parents=True, exist_ok=True)
    log_path = workdir / "truth.jsonl"
    if log_path.exists():
        log_path.unlink()
    col = Collector()
    cmd = [str(sim), "--port", str(col.port), "--rate", str(rate), "--count", str(count),
           "--seed", str(seed), "--start", str(start), *map(str, args)]
    if log:
        cmd += ["--log", str(log_path)]
    t0 = time.monotonic()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout or count / rate + 30)
    finally:
        seconds = time.monotonic() - t0
        col.close()
    return Run(cmd, col.datagrams, col.arrivals, read_log(log_path), p.returncode, p.stderr, seconds, rate, count)
