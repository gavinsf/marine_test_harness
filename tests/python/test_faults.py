"""One class per fault case. Each runs the real simulator with that fault on and checks, from the
wire and the ground-truth log, that the fault was applied exactly as described."""
from collections import Counter

import pytest

import nmea
from harness import latin1

GROUPS_OF_SENTENCES = 5       # sentences per tick


def faulty(run_sim, flag, p, *extra, **kw):
    return run_sim(flag, p, *extra, **kw)


def seqs_on_wire(run):
    return [r["seq"] for r in run.sent]


def faults_in(run):
    return Counter(r["fault"] for r in run.log)


class TestBadChecksum:
    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-checksum", 1)

    def test_every_sentence_is_still_sent(self, run):
        assert len(run.datagrams) == run.count * 5

    def test_only_the_checksum_is_wrong(self, run):
        for d in run.datagrams:
            problems = nmea.frame_problems(d)
            assert problems == ["checksum does not match"], (d, problems)

    def test_everything_before_the_checksum_is_unchanged(self, run):
        for r in run.log:
            sent, orig = latin1(r["text"]), latin1(r["original"])
            assert sent[:sent.rfind(b"*")] == orig[:orig.rfind(b"*")]
            assert len(sent) == len(orig)

    def test_log_labels_every_message(self, run):
        assert faults_in(run) == {"bad_checksum": run.count * 5}


class TestTruncated:
    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-truncate", 1)

    def test_every_sentence_is_still_sent(self, run):
        assert len(run.datagrams) == run.count * 5

    def test_each_one_is_a_shorter_prefix_of_the_original(self, run):
        for r in run.log:
            sent, orig = latin1(r["text"]), latin1(r["original"])
            assert len(sent) < len(orig)
            assert sent.endswith(b"\r\n")
            assert orig.startswith(sent[:-2])

    def test_none_of_them_passes_validation(self, run):
        assert not any(nmea.is_valid_frame(d) for d in run.datagrams)

    def test_log_labels_every_message(self, run):
        assert faults_in(run) == {"truncate": run.count * 5}


class TestDuplicate:
    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-duplicate", 1)

    def test_every_sentence_arrives_twice(self, run):
        assert len(run.datagrams) == run.count * 5 * 2
        assert set(Counter(seqs_on_wire(run)).values()) == {2}

    def test_the_copies_are_byte_identical_and_valid(self, run):
        by_seq = {}
        for r in run.sent:
            by_seq.setdefault(r["seq"], []).append(r)
        for seq, pair in by_seq.items():
            assert sorted(x["copy"] for x in pair) == [0, 1]
            assert pair[0]["text"] == pair[1]["text"] == pair[0]["original"]
        assert all(nmea.is_valid_frame(d) for d in run.datagrams)

    def test_the_second_copy_is_sent_later(self, run):
        emit = {(r["seq"], r["copy"]): r["emit"] for r in run.sent}
        assert all(emit[(s, 1)] > emit[(s, 0)] for s in range(run.count * 5))


class TestDropped:
    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-drop", 1)

    def test_nothing_goes_on_the_wire(self, run):
        assert run.datagrams == []

    def test_every_drop_is_still_logged_with_the_sentence_that_was_lost(self, run):
        assert len(run.dropped) == run.count * 5
        for r in run.dropped:
            assert r["text"] == "" and r["emit"] is None and r["fault"] == "drop"
            assert nmea.is_valid_frame(latin1(r["original"]))


class TestPartialDrop:
    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-drop", 0.2, count=200)

    def test_the_wire_is_missing_exactly_the_logged_drops(self, run):
        assert len(run.datagrams) + len(run.dropped) == 200 * 5
        assert {r["seq"] for r in run.dropped}.isdisjoint(seqs_on_wire(run))

    def test_drop_rate_is_close_to_the_configured_probability(self, run):
        assert len(run.dropped) / (200 * 5) == pytest.approx(0.2, abs=0.05)

    def test_what_remains_is_in_order_and_valid(self, run):
        seqs = seqs_on_wire(run)
        assert seqs == sorted(seqs)
        assert all(nmea.is_valid_frame(d) for d in run.datagrams)


class TestReordered:
    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-reorder", 0.3, "--max-reorder", 3, count=200)

    def test_nothing_is_lost_or_duplicated(self, run):
        assert sorted(seqs_on_wire(run)) == list(range(200 * 5))

    def test_the_stream_really_is_out_of_order(self, run):
        seqs = seqs_on_wire(run)
        assert any(b < a for a, b in zip(seqs, seqs[1:]))

    def test_a_reordered_sentence_is_overtaken_by_at_most_max_reorder_later_ones(self, run):
        pos = {r["seq"]: i for i, r in enumerate(run.sent)}
        for r in run.log:
            overtaken = sum(1 for s, p in pos.items() if s > r["seq"] and p < pos[r["seq"]])
            if r["fault"] == "reorder":
                assert overtaken <= 3
            else:
                assert overtaken == 0, (r["seq"], overtaken)     # untouched sentences stay in order

    def test_content_is_unchanged_so_all_still_validate(self, run):
        assert all(r["text"] == r["original"] for r in run.log)
        assert all(nmea.is_valid_frame(d) for d in run.datagrams)


class TestDelayed:
    DELAY = ("--delay-min", 200, "--delay-max", 400)

    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-delay", 1, *cls.DELAY)

    def test_nothing_is_lost(self, run):
        assert sorted(seqs_on_wire(run)) == list(range(run.count * 5))

    def test_each_sentence_is_sent_between_min_and_max_late(self, run):
        end = run.count / run.rate
        checked = 0
        for r in run.log:
            generated = (r["seq"] // GROUPS_OF_SENTENCES) / run.rate
            if r["t"] >= end:           # flushed early at shutdown
                continue
            checked += 1
            assert generated + 0.2 - 0.001 <= r["t"] <= generated + 0.4 + 1 / run.rate + 0.001, r
        assert checked > run.count * 5 / 2

    def test_different_delays_deliver_the_stream_out_of_order(self, run):
        seqs = seqs_on_wire(run)
        assert any(b < a for a, b in zip(seqs, seqs[1:]))

    def test_content_is_unchanged(self, run):
        assert all(r["text"] == r["original"] for r in run.log)
        assert all(nmea.is_valid_frame(d) for d in run.datagrams)


class TestBurst:
    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return faulty(run_sim, "--p-burst", 1, "--burst-min", 3, "--burst-max", 5, count=200)

    def test_nothing_is_lost_and_order_is_preserved(self, run):
        assert seqs_on_wire(run) == list(range(200 * 5))

    def test_sentences_are_held_and_released_together(self, run):
        groups = {}
        for r in run.log:
            groups.setdefault(r["t"], []).append(r)
        end = run.count / run.rate
        sizes = [len(g) for t, g in groups.items() if t < end]
        assert sizes and min(sizes) >= 3
        spans_ticks = [g for g in groups.values() if len({r["seq"] // 5 for r in g}) > 1]
        assert spans_ticks, "no burst held sentences from more than one tick"

    def test_content_is_unchanged(self, run):
        assert all(nmea.is_valid_frame(d) for d in run.datagrams)


class TestAllFaultsTogether:
    FLAGS = ["--p-checksum", 0.05, "--p-truncate", 0.04, "--p-duplicate", 0.04, "--p-reorder", 0.06,
             "--p-drop", 0.06, "--p-delay", 0.05, "--p-burst", 0.02]

    @pytest.fixture(scope="class")
    @classmethod
    def run(cls, run_sim):
        return run_sim(*cls.FLAGS, count=300)

    def test_every_fault_type_occurred(self, run):
        seen = faults_in(run)
        for name in ("none", "bad_checksum", "truncate", "duplicate", "reorder", "drop", "delay", "burst"):
            assert seen[name] > 0, seen

    def test_the_wire_is_exactly_what_the_log_says_was_sent(self, run):
        assert [latin1(r["text"]) for r in run.sent] == run.datagrams

    def test_every_generated_sentence_is_accounted_for(self, run):
        assert {r["seq"] for r in run.log} == set(range(300 * 5))

    def test_emit_positions_are_contiguous(self, run):
        assert [r["emit"] for r in run.sent] == list(range(len(run.sent)))

    def test_valid_and_invalid_sentences_match_their_fault(self, run):
        for r in run.sent:
            valid = nmea.is_valid_frame(latin1(r["text"]))
            if r["fault"] in ("bad_checksum", "truncate"):
                assert not valid, r
            else:
                assert valid, r

    def test_every_original_is_a_valid_sentence(self, run):
        assert all(nmea.spec_problems(latin1(r["original"])) == [] for r in run.log)

    def test_summary_counts_match_the_log(self, run):
        assert f"{len(run.dropped)} dropped" in run.stderr
        assert f"{len(run.sent)} datagrams sent" in run.stderr
        assert f"{sum(1 for r in run.log if r['fault'] != 'none')} with a fault" in run.stderr
