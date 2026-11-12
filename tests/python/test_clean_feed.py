"""Case: no faults. The feed must be a clean, spec-conforming stream of the five sentence types."""
import re

import pytest

import nmea

ORDER = ["GGA", "RMC", "HDT", "DPT", "MWV"]


@pytest.fixture(scope="module")
def clean(run_sim):
    return run_sim("--speed", 10)


def test_exits_cleanly_and_reports_a_summary(clean):
    assert clean.returncode == 0, clean.stderr
    assert "500 sentences in 100 ticks" in clean.stderr


def test_five_sentences_per_tick_each_in_its_own_datagram(clean):
    assert len(clean.datagrams) == clean.count * 5


def test_every_datagram_conforms_to_the_spec(clean):
    bad = [(d, nmea.spec_problems(d)) for d in clean.datagrams if nmea.spec_problems(d)]
    assert not bad, bad[:3]


def test_every_checksum_is_correct(clean):
    assert all(nmea.is_valid_frame(d) for d in clean.datagrams)


def test_sentence_types_repeat_in_the_order_gga_rmc_hdt_dpt_mwv(clean):
    kinds = [nmea.parse(d).kind for d in clean.datagrams]
    assert kinds == ORDER * clean.count


def test_talker_ids(clean):
    for d in clean.datagrams[:5]:
        p = nmea.parse(d)
        assert p.talker == nmea.TALKER_FOR[p.kind]
    assert [nmea.parse(d).talker for d in clean.datagrams[:5]] == ["GP", "GP", "HE", "SD", "WI"]


def test_latitude_and_longitude_formats_and_hemispheres(clean):
    for d in clean.wire("GGA") + clean.wire("RMC"):
        f = nmea.parse(d).fields
        lat, ns, lon, ew = (f[2], f[3], f[4], f[5]) if f[0].endswith("GGA") else (f[3], f[4], f[5], f[6])
        assert re.fullmatch(r"\d{4}\.\d{4}", lat) and ns == "N"      # Puget Sound: north,
        assert re.fullmatch(r"\d{5}\.\d{4}", lon) and ew == "W"      # west


def test_gga_and_rmc_in_one_tick_agree_on_time_and_position(clean):
    for gga, rmc in zip(clean.wire("GGA"), clean.wire("RMC")):
        g, r = nmea.parse(gga).fields, nmea.parse(rmc).fields
        assert g[1] == r[1]
        assert (g[2], g[3], g[4], g[5]) == (r[3], r[4], r[5], r[6])


def test_all_sentences_in_a_tick_share_one_timestamp(clean):
    gga, rmc = clean.wire("GGA"), clean.wire("RMC")
    assert [nmea.parse(d).fields[1] for d in gga] == [nmea.parse(d).fields[1] for d in rmc]


def test_utc_time_starts_at_start_and_advances_one_tick_at_a_time(clean):
    times = [nmea.seconds_of_day(nmea.parse(d).fields[1]) for d in clean.wire("GGA")]
    assert times[0] == 0.0                                    # --start is 2026-01-01 00:00:00
    steps = [b - a for a, b in zip(times, times[1:])]
    assert all(s == pytest.approx(1 / clean.rate, abs=0.0051) for s in steps), steps[:5]
    assert nmea.parse(clean.wire("RMC")[0]).fields[9] == "010126"


def test_ship_moves_at_the_requested_speed(run_sim):
    run = run_sim("--speed", 100)
    gga = [nmea.parse(d).fields for d in run.wire("GGA")]
    pos = [(nmea.to_degrees(f[2], f[3], 2), nmea.to_degrees(f[4], f[5], 3)) for f in gga]
    travelled = nmea.distance_nm(*pos[0], *pos[-1])
    expected = 100 * (run.count - 1) / run.rate / 3600
    assert travelled == pytest.approx(expected, rel=0.03)


def test_speed_and_course_fields_are_consistent_with_the_requested_speed(run_sim):
    run = run_sim("--speed", 37)
    rmc = nmea.parse(run.wire("RMC")[0]).fields
    assert float(rmc[7]) == pytest.approx(37.0)
    assert float(nmea.parse(run.wire("HDT")[0]).fields[1]) == pytest.approx(float(rmc[8]), abs=0.05)


def test_zero_speed_keeps_the_ship_still(run_sim):
    run = run_sim("--speed", 0, count=20)
    pos = {tuple(nmea.parse(d).fields[2:6]) for d in run.wire("GGA")}
    assert len(pos) == 1


def test_depth_and_wind_stay_in_a_plausible_range(clean):
    depth = [float(nmea.parse(d).fields[1]) for d in clean.wire("DPT")]
    angle = [float(nmea.parse(d).fields[1]) for d in clean.wire("MWV")]
    speed = [float(nmea.parse(d).fields[3]) for d in clean.wire("MWV")]
    assert all(5 <= x <= 45 for x in depth)
    assert all(0 <= x < 360 for x in angle)
    assert all(0 <= x <= 25 for x in speed)


def test_depth_and_wind_start_at_their_centres_and_change_slowly(clean):
    """Depth (25 m), wind angle (180 deg) and wind speed (15 kn) vary as slow waves, so after the
    2 s of this run they can have moved only a little from where they started."""
    depth = [float(nmea.parse(d).fields[1]) for d in clean.wire("DPT")]
    angle = [float(nmea.parse(d).fields[1]) for d in clean.wire("MWV")]
    speed = [float(nmea.parse(d).fields[3]) for d in clean.wire("MWV")]
    assert (depth[0], angle[0], speed[0]) == (25.0, 180.0, 15.0)
    assert all(abs(x - 25.0) <= 1.2 for x in depth)
    assert all(abs(x - 180.0) <= 15.0 for x in angle)
    assert all(abs(x - 15.0) <= 2.0 for x in speed)


def test_feed_is_paced_at_the_requested_rate(clean):
    expected = clean.count / clean.rate
    assert expected - 0.15 <= clean.seconds <= expected + 0.6, clean.seconds
    ticks = clean.arrivals[::5]
    gaps = sorted(b - a for a, b in zip(ticks, ticks[1:]))
    median = gaps[len(gaps) // 2]
    assert median == pytest.approx(1 / clean.rate, abs=0.005)


@pytest.mark.parametrize("rate", [1, 10, 25])
def test_other_rates(run_sim, rate):
    run = run_sim(rate=rate, count=rate * 2)
    assert len(run.datagrams) == rate * 2 * 5
    assert abs(run.seconds - 2.0) < 0.5


def test_no_faults_are_logged_and_text_equals_original(clean):
    assert len(clean.log) == clean.count * 5
    assert all(r["fault"] == "none" and not r["dropped"] and r["copy"] == 0 for r in clean.log)
    assert all(r["text"] == r["original"] for r in clean.log)


def test_log_matches_the_wire_exactly(clean):
    from harness import latin1
    assert [latin1(r["text"]) for r in clean.sent] == clean.datagrams
