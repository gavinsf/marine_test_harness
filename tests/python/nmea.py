"""Independent NMEA 0183 checks for the black-box tests.

Deliberately does not share any code with the C++ builder or parser: it re-implements the
checksum and the field rules from the NMEA 0183 spec, so a bug the builder and parser share
cannot hide from these tests.
"""
import math
import re
from dataclasses import dataclass

# Talker IDs the simulator is expected to use for each sentence type.
TALKER_FOR = {"GGA": "GP", "RMC": "GP", "HDT": "HE", "DPT": "SD", "MWV": "WI"}
FIELD_COUNT = {"GGA": 15, "RMC": 12, "HDT": 3, "DPT": 3, "MWV": 6}
MAX_LENGTH = 82            # '$' .. CRLF inclusive

_NUM = r"-?\d+(\.\d+)?"
_UNSIGNED = r"\d+(\.\d+)?"


def checksum(body: bytes) -> int:
    x = 0
    for b in body:
        x ^= b
    return x


def frame_problems(raw: bytes) -> list[str]:
    """Everything wrong with the framing: '$', CRLF, length, one '*', two uppercase hex digits, XOR."""
    problems = []
    if not raw.startswith(b"$"):
        problems.append("does not start with '$'")
    has_crlf = raw.endswith(b"\r\n")
    if not has_crlf:
        problems.append("does not end with CRLF")
    if len(raw) > MAX_LENGTH:
        problems.append(f"longer than {MAX_LENGTH} characters")
    text = raw[:-2] if has_crlf else raw
    if any(b < 0x20 or b > 0x7E for b in text):
        problems.append("contains a non-printable byte")
    if raw.count(b"*") != 1:
        problems.append("does not contain exactly one '*'")
        return problems
    star = raw.rfind(b"*")
    digits = raw[star + 1:-2] if has_crlf else raw[star + 1:]
    if not re.fullmatch(rb"[0-9A-F]{2}", digits):
        problems.append("checksum is not two uppercase hex digits")
    elif int(digits, 16) != checksum(raw[1:star]):
        problems.append("checksum does not match")
    return problems


def is_valid_frame(raw: bytes) -> bool:
    return not frame_problems(raw)


def fields_of(raw: bytes) -> list[str]:
    """Comma-separated fields between '$' and '*' (address field first)."""
    return raw[1:raw.rfind(b"*")].decode("ascii").split(",")


@dataclass
class Parsed:
    talker: str
    kind: str
    fields: list[str]      # fields[0] is the address, e.g. "GPGGA"


def parse(raw: bytes) -> Parsed:
    f = fields_of(raw)
    return Parsed(f[0][:2], f[0][2:], f)


# --------------------------------------------------------------------------- field rules

def _time(s):
    m = re.fullmatch(r"(\d\d)(\d\d)(\d\d)\.(\d\d)", s)
    if not m:
        return "time is not hhmmss.ss"
    h, mi, se = int(m[1]), int(m[2]), int(m[3])
    return None if h < 24 and mi < 60 and se < 60 else "time out of range"


def _date(s):
    m = re.fullmatch(r"(\d\d)(\d\d)(\d\d)", s)
    if not m:
        return "date is not ddmmyy"
    d, mo = int(m[1]), int(m[2])
    return None if 1 <= d <= 31 and 1 <= mo <= 12 else "date out of range"


def _coord(value, hemi, deg_digits, max_deg, hemis, name):
    pattern = rf"\d{{{deg_digits + 2}}}\.\d{{4}}"
    if not re.fullmatch(pattern, value):
        return f"{name} is not {'d' * deg_digits}mm.mmmm"
    if hemi not in hemis:
        return f"{name} hemisphere must be one of {hemis}"
    deg, minutes = int(value[:deg_digits]), float(value[deg_digits:])
    if minutes >= 60 or deg > max_deg or (deg == max_deg and minutes > 0):
        return f"{name} out of range"
    return None


def to_degrees(value: str, hemi: str, deg_digits: int) -> float:
    d = int(value[:deg_digits]) + float(value[deg_digits:]) / 60.0
    return -d if hemi in ("S", "W") else d


def _degrees360(s, name):
    if not re.fullmatch(_UNSIGNED, s):
        return f"{name} is not a number"
    return None if 0.0 <= float(s) < 360.0 else f"{name} outside 0-359.9"


def _num(s, name, minimum=None):
    if not re.fullmatch(_NUM, s):
        return f"{name} is not a number"
    return f"{name} below {minimum}" if minimum is not None and float(s) < minimum else None


def _optional_num(s, name):
    return None if s == "" else _num(s, name)


def spec_problems(raw: bytes) -> list[str]:
    """Framing problems, then the per-sentence field rules. Empty list = conforms."""
    problems = frame_problems(raw)
    if problems:
        return problems
    p = parse(raw)
    f = p.fields
    if p.kind not in TALKER_FOR:
        return [f"unexpected sentence type {p.kind}"]
    if p.talker != TALKER_FOR[p.kind]:
        problems.append(f"talker {p.talker} should be {TALKER_FOR[p.kind]} for {p.kind}")
    if len(f) != FIELD_COUNT[p.kind]:
        return problems + [f"{p.kind} has {len(f)} fields, expected {FIELD_COUNT[p.kind]}"]

    checks = []
    if p.kind == "GGA":
        checks = [_time(f[1]), _coord(f[2], f[3], 2, 90, "NS", "latitude"),
                  _coord(f[4], f[5], 3, 180, "EW", "longitude"),
                  None if re.fullmatch(r"[0-8]", f[6]) else "quality not 0-8",
                  None if re.fullmatch(r"\d\d", f[7]) else "satellite count not two digits",
                  _num(f[8], "HDOP", 0), _num(f[9], "altitude"),
                  None if f[10] == "M" else "altitude unit must be M",
                  _optional_num(f[11], "geoid separation"),
                  None if f[12] == "M" or (f[11] == "" and f[12] in ("", "M")) else "geoid unit must be M",
                  _optional_num(f[13], "DGPS age")]
    elif p.kind == "RMC":
        checks = [_time(f[1]), None if f[2] in ("A", "V") else "status must be A or V",
                  _coord(f[3], f[4], 2, 90, "NS", "latitude"), _coord(f[5], f[6], 3, 180, "EW", "longitude"),
                  _num(f[7], "speed", 0), _degrees360(f[8], "course"), _date(f[9]),
                  _optional_num(f[10], "magnetic variation"),
                  None if (f[10] == "") == (f[11] == "") and f[11] in ("", "E", "W") else "variation and its E/W must go together"]
    elif p.kind == "HDT":
        checks = [_degrees360(f[1], "heading"), None if f[2] == "T" else "reference must be T"]
    elif p.kind == "DPT":
        checks = [_num(f[1], "depth", 0), _num(f[2], "offset")]
    elif p.kind == "MWV":
        checks = [_degrees360(f[1], "wind angle"), None if f[2] in ("R", "T") else "reference must be R or T",
                  _num(f[3], "wind speed", 0), None if f[4] in ("K", "M", "N", "S") else "unit must be K, M, N or S",
                  None if f[5] in ("A", "V") else "status must be A or V"]
    return problems + [c for c in checks if c]


# --------------------------------------------------------------------------- geometry / time

def seconds_of_day(hhmmss: str) -> float:
    return int(hhmmss[0:2]) * 3600 + int(hhmmss[2:4]) * 60 + float(hhmmss[4:])


def distance_nm(lat1, lon1, lat2, lon2) -> float:
    r = 3440.065
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp, dl = p2 - p1, math.radians(lon2 - lon1)
    h = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(min(1.0, math.sqrt(h)))
