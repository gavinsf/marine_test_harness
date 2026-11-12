"""Same options, same bytes: the property that makes a failing run reproducible (REQ-007)."""
import pytest

FAULTS = ["--p-checksum", 0.05, "--p-truncate", 0.04, "--p-duplicate", 0.04, "--p-reorder", 0.06,
          "--p-drop", 0.06, "--p-delay", 0.05, "--p-burst", 0.02]


def log_text(run):
    import json
    return [json.dumps(r, sort_keys=True) for r in run.log]


@pytest.fixture(scope="module")
def a(run_sim):
    return run_sim(*FAULTS, count=100, seed=11)


def test_same_seed_gives_identical_datagrams(run_sim, a):
    b = run_sim(*FAULTS, count=100, seed=11, fresh=True)
    assert a.datagrams == b.datagrams


def test_same_seed_gives_an_identical_ground_truth_log(run_sim, a):
    b = run_sim(*FAULTS, count=100, seed=11, fresh=True)
    assert log_text(a) == log_text(b)


def test_a_different_seed_gives_a_different_fault_pattern(run_sim, a):
    c = run_sim(*FAULTS, count=100, seed=12)
    assert a.datagrams != c.datagrams
    assert [r["fault"] for r in a.log] != [r["fault"] for r in c.log]


def test_clean_feed_is_identical_for_any_seed(run_sim):
    assert run_sim(seed=1).datagrams == run_sim(seed=2).datagrams


def test_a_different_start_time_changes_timestamps_but_not_the_fault_pattern(run_sim, a):
    later = run_sim(*FAULTS, count=100, seed=11, start=1767225600 + 3600)
    assert a.datagrams != later.datagrams
    assert [(r["seq"], r["fault"], r["copy"]) for r in a.log] == [(r["seq"], r["fault"], r["copy"]) for r in later.log]


def test_fault_decisions_do_not_depend_on_the_tick_rate(run_sim):
    slow = run_sim("--p-drop", 0.2, count=20, rate=10, seed=5)
    fast = run_sim("--p-drop", 0.2, count=100, rate=50, seed=5)
    n = min(len(slow.log), len(fast.log))
    drops = lambda r: sorted(x["seq"] for x in r.log if x["fault"] == "drop" and x["seq"] < n)
    assert drops(slow) == drops(fast)
