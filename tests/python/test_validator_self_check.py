"""The checks in nmea.py must themselves reject bad input, or the black-box tests prove nothing."""
import pytest

import nmea


def frame(body: str, cs=None) -> bytes:
    cs = nmea.checksum(body.encode()) if cs is None else cs
    return f"${body}*{cs:02X}\r\n".encode()


GOOD = {
    "GGA": frame("GPGGA,123519.00,4807.0380,N,01131.0000,E,1,08,0.9,545.4,M,46.9,M,,"),
    "RMC": frame("GPRMC,123519.00,A,4807.0380,N,01131.0000,E,022.4,084.4,230394,003.1,W"),
    "HDT": frame("HEHDT,274.1,T"),
    "DPT": frame("SDDPT,25.0,0.0"),
    "MWV": frame("WIMWV,180.0,R,15.0,N,A"),
}


@pytest.mark.parametrize("kind", GOOD)
def test_known_good_sentence_passes(kind):
    assert nmea.spec_problems(GOOD[kind]) == []


def test_checksum_is_the_xor_of_the_bytes_between_dollar_and_star():
    assert nmea.is_valid_frame(frame("HEHDT,274.1,T"))
    assert not nmea.is_valid_frame(frame("HEHDT,274.1,T", cs=0x00))


@pytest.mark.parametrize("raw, why", [
    (b"HEHDT,274.1,T*2D\r\n", "start"),
    (frame("HEHDT,274.1,T")[:-2], "CRLF"),
    (b"$HEHDT,274.1,T\r\n", "'*'"),
    (b"$HEHDT,274.1,T*\r\n", "uppercase hex"),
    (b"$HEHDT,274.1,T*2\r\n", "uppercase hex"),
    (frame("HEHDT,274.1,T").replace(b"*", b"*a"), "checksum"),
    (b"$" + b"A" * 100 + b"*00\r\n", "longer"),
    (b"$HEH\x01DT,274.1,T*00\r\n", "non-printable"),
])
def test_bad_framing_is_flagged(raw, why):
    assert any(why in p for p in nmea.frame_problems(raw)), nmea.frame_problems(raw)


def test_lowercase_hex_checksum_is_flagged():
    raw = frame("HEHDT,274.1,T")
    lower = raw[:-4] + raw[-4:-2].lower() + b"\r\n"
    if lower != raw:    # only meaningful when the checksum has a letter
        assert nmea.frame_problems(lower)


@pytest.mark.parametrize("body, why", [
    ("GPGGA,123519.00,4807.0380,N,01131.0000,E,1,08,0.9,545.4,M,46.9,M,", "fields"),              # one field short
    ("GPGGA,1235.00,4807.0380,N,01131.0000,E,1,08,0.9,545.4,M,46.9,M,,", "time"),
    ("GPGGA,123519.00,480.0380,N,01131.0000,E,1,08,0.9,545.4,M,46.9,M,,", "latitude"),
    ("GPGGA,123519.00,4807.0380,X,01131.0000,E,1,08,0.9,545.4,M,46.9,M,,", "latitude"),
    ("GPGGA,123519.00,4860.0000,N,01131.0000,E,1,08,0.9,545.4,M,46.9,M,,", "latitude"),            # 60 minutes
    ("GPGGA,123519.00,4807.0380,N,1131.0000,E,1,08,0.9,545.4,M,46.9,M,,", "longitude"),
    ("GPGGA,123519.00,4807.0380,N,01131.0000,E,1,08,0.9,545.4,F,46.9,M,,", "altitude unit"),
    ("GPRMC,123519.00,X,4807.0380,N,01131.0000,E,022.4,084.4,230394,,", "status"),
    ("GPRMC,123519.00,A,4807.0380,N,01131.0000,E,022.4,360.0,230394,,", "course"),
    ("GPRMC,123519.00,A,4807.0380,N,01131.0000,E,022.4,084.4,321394,,", "date"),
    ("GPRMC,123519.00,A,4807.0380,N,01131.0000,E,-1.0,084.4,230394,,", "speed"),
    ("HEHDT,360.0,T", "heading"),
    ("HEHDT,274.1,M", "reference"),
    ("HXHDT,274.1,T", "talker"),
    ("SDDPT,-1.0,0.0", "depth"),
    ("WIMWV,180.0,R,15.0,Q,A", "unit"),
    ("WIMWV,400.0,R,15.0,N,A", "wind angle"),
    ("GPXXX,1,2,3", "unexpected"),
])
def test_bad_fields_are_flagged(body, why):
    problems = nmea.spec_problems(frame(body))
    assert any(why in p for p in problems), problems


def test_distance_helper():
    assert nmea.distance_nm(47.0, -122.0, 48.0, -122.0) == pytest.approx(60.0, rel=0.01)


def test_coordinate_conversion():
    assert nmea.to_degrees("4736.0000", "N", 2) == pytest.approx(47.6)
    assert nmea.to_degrees("12224.0000", "W", 3) == pytest.approx(-122.4)
