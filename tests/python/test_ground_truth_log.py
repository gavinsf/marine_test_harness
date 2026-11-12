"""The ground-truth log is what the harness checks the receiver against, so its format is tested too."""
import json
import re

import pytest

import nmea
from harness import latin1

KEYS = {"seq", "copy", "emit", "t", "fault", "dropped", "original", "text"}
FAULT_NAMES = {"none", "bad_checksum", "truncate", "duplicate", "reorder", "drop", "delay", "burst"}


@pytest.fixture(scope="module")
def run(run_sim):
    return run_sim("--p-checksum", 0.1, "--p-truncate", 0.1, "--p-duplicate", 0.1, "--p-drop", 0.1,
                   "--p-reorder", 0.1, "--p-delay", 0.1, "--p-burst", 0.05, count=100)


def test_one_json_object_per_line_with_exactly_the_documented_keys(run):
    assert run.log
    assert all(set(r) == KEYS for r in run.log)


def test_field_types_and_values(run):
    for r in run.log:
        assert isinstance(r["seq"], int) and r["seq"] >= 0
        assert r["copy"] in (0, 1)
        assert isinstance(r["t"], float) and r["t"] >= 0
        assert r["fault"] in FAULT_NAMES
        assert isinstance(r["dropped"], bool)
        assert isinstance(r["original"], str) and isinstance(r["text"], str)
        assert (r["emit"] is None) == r["dropped"]


def test_dropped_entries_are_exactly_the_drop_fault(run):
    assert all(r["dropped"] == (r["fault"] == "drop") for r in run.log)


def test_every_sentence_appears_and_only_duplicates_appear_twice(run):
    count = {}
    for r in run.log:
        count[r["seq"]] = count.get(r["seq"], 0) + 1
    assert set(count) == set(range(run.count * 5))
    for r in run.log:
        assert count[r["seq"]] == (2 if r["fault"] == "duplicate" else 1)


def test_times_never_go_backwards_and_have_millisecond_precision(run):
    ts = [r["t"] for r in run.log]
    assert ts == sorted(ts)
    raw_lines = open(run.args[run.args.index("--log") + 1], encoding="utf-8").read().splitlines()
    assert all(re.search(r'"t":\d+\.\d{3},', line) for line in raw_lines)


def test_log_lines_are_single_lines_of_ascii(run):
    path = run.args[run.args.index("--log") + 1]
    data = open(path, "rb").read()
    assert data.endswith(b"\n")
    assert all(b < 0x80 for b in data)
    for line in data.split(b"\n")[:-1]:
        json.loads(line)


def test_original_is_always_the_valid_sentence_and_text_is_what_was_sent(run):
    for r in run.log:
        assert nmea.spec_problems(latin1(r["original"])) == []
    assert [latin1(r["text"]) for r in run.sent] == run.datagrams


def test_no_log_file_is_written_unless_asked_for(run_sim, sim_bin, tmp_path):
    import harness
    r = harness.execute(sim_bin, tmp_path, count=10, log=False)
    assert r.log == [] and not (tmp_path / "truth.jsonl").exists()
    assert len(r.datagrams) == 50


def test_log_is_replaced_not_appended_on_each_run(sim_bin, tmp_path):
    import harness
    first = harness.execute(sim_bin, tmp_path, count=10)
    # run again into the same file without deleting it first
    p = tmp_path / "truth.jsonl"
    import subprocess
    subprocess.run([str(sim_bin), "--port", "9", "--count", "5", "--rate", "50", "--log", str(p)],
                   capture_output=True, timeout=10)
    assert len(harness.read_log(p)) == 25
    assert len(first.log) == 50
