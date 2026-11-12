"""Stopping the simulator mid-run must exit cleanly and leave a complete log."""
import signal
import subprocess
import time

import pytest

import harness


@pytest.mark.parametrize("sig", [signal.SIGINT, signal.SIGTERM], ids=["SIGINT", "SIGTERM"])
def test_signal_stops_the_run_with_status_0_and_a_summary(sim_bin, tmp_path, sig):
    col = harness.Collector()
    log = tmp_path / "truth.jsonl"
    p = subprocess.Popen([str(sim_bin), "--port", str(col.port), "--rate", "20", "--start", str(harness.START),
                          "--log", str(log)], stderr=subprocess.PIPE, text=True)
    time.sleep(1.5)
    p.send_signal(sig)
    err = p.communicate(timeout=5)[1]
    col.close()
    assert p.returncode == 0
    assert "datagrams sent" in err
    assert 20 <= len(col.datagrams) <= 20 * 5 * 3
    assert len(col.datagrams) % 5 == 0
    assert len(harness.read_log(log)) == len(col.datagrams)


def test_messages_still_held_at_shutdown_are_flushed_and_logged(sim_bin, tmp_path):
    col = harness.Collector()
    log = tmp_path / "truth.jsonl"
    # Every sentence is delayed 3 s, so after 1.5 s nothing has been released yet.
    p = subprocess.Popen([str(sim_bin), "--port", str(col.port), "--rate", "20", "--p-delay", "1",
                          "--delay-min", "3000", "--delay-max", "3000", "--start", str(harness.START),
                          "--log", str(log)], stderr=subprocess.PIPE, text=True)
    time.sleep(1.5)
    assert col.datagrams == []
    p.send_signal(signal.SIGINT)
    p.communicate(timeout=5)
    col.close()
    entries = harness.read_log(log)
    assert p.returncode == 0
    assert len(col.datagrams) == len(entries) >= 100
    assert all(r["fault"] == "delay" for r in entries)


def test_count_stops_the_run_on_its_own(sim_bin, tmp_path):
    r = harness.execute(sim_bin, tmp_path, count=15, rate=50)
    assert r.returncode == 0 and len(r.datagrams) == 75
