"""Command-line handling: bad options must fail fast with a message and a non-zero status."""
import subprocess

import pytest


def sim(sim_bin, *args, timeout=15):
    return subprocess.run([str(sim_bin), *map(str, args)], capture_output=True, text=True, timeout=timeout)


@pytest.mark.parametrize("args", [
    ["--rate", 0], ["--rate", 51], ["--rate", "fast"],
    ["--port", 0], ["--port", 70000], ["--port", "x"],
    ["--count", -1], ["--speed", -1], ["--speed", 101],
    ["--start", -5], ["--seed", "x"],
    ["--p-checksum", -0.1], ["--p-drop", 1.5], ["--p-delay", "abc"],
    ["--p-drop", 0.6, "--p-delay", 0.6],
    ["--max-reorder", 0],
    ["--burst-min", 1], ["--burst-min", 5, "--burst-max", 4],
    ["--delay-min", -1], ["--delay-min", 500, "--delay-max", 100],
    ["--bogus", 1],
    ["--rate"],
    ["--help"],
], ids=lambda a: " ".join(map(str, a)))
def test_invalid_usage_exits_2_with_a_message(sim_bin, args):
    r = sim(sim_bin, *args)
    assert r.returncode == 2
    assert r.stderr.strip()
    assert r.stdout == ""


def test_usage_text_lists_every_fault_flag(sim_bin):
    err = sim(sim_bin, "--help").stderr
    for flag in ["--p-checksum", "--p-truncate", "--p-duplicate", "--p-reorder", "--p-drop", "--p-delay",
                 "--p-burst", "--max-reorder", "--delay-min", "--delay-max", "--burst-min", "--burst-max",
                 "--seed", "--start", "--log", "--rate", "--count", "--speed", "--host", "--port"]:
        assert flag in err, flag


def test_unwritable_log_path_exits_1(sim_bin):
    r = sim(sim_bin, "--count", 1, "--log", "/nonexistent_dir_for_nmea_test/x.jsonl")
    assert r.returncode == 1 and "cannot open" in r.stderr


def test_bad_host_exits_1(sim_bin):
    r = sim(sim_bin, "--count", 1, "--host", "not.an.address")
    assert r.returncode == 1 and r.stderr.strip()


def test_probabilities_that_sum_to_exactly_one_are_accepted(sim_bin):
    r = sim(sim_bin, "--count", 2, "--rate", 50, "--p-drop", 0.5, "--p-checksum", 0.5, "--port", 9)
    assert r.returncode == 0
