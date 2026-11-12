import itertools

import pytest

import harness


@pytest.fixture(scope="session")
def sim_bin():
    return harness.find_simulator()


@pytest.fixture(scope="session")
def run_sim(sim_bin, tmp_path_factory):
    """run_sim(*simulator_args, rate=50, count=100, seed=7, fresh=False) -> harness.Run

    Identical invocations are run once and shared between tests (the simulator is deterministic);
    pass fresh=True to force a new process.
    """
    cache = {}
    ids = itertools.count()

    def run(*args, fresh=False, **kw):
        key = (args, tuple(sorted(kw.items())))
        if fresh or key not in cache:
            result = harness.execute(sim_bin, tmp_path_factory.mktemp(f"run{next(ids)}"), *args, **kw)
            if fresh:
                return result
            cache[key] = result
        return cache[key]

    return run
