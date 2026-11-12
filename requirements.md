## Requirements

This file describes what the project does today. Every requirement below is built and covered by a test.

### Feed Simulator (SIM)

The simulator is the `nmea_sim` program. It sends NMEA 0183 sentences over UDP.

| ID | Requirement | Priority |
|---|---|---|
| REQ-001 | The simulator shall generate GGA, RMC, HDT, DPT and MWV sentences, each with a correct checksum when no fault is applied. | Must |
| REQ-002 | The simulator shall format latitude as `ddmm.mmmm` and longitude as `dddmm.mmmm` with the correct N/S/E/W indicator. | Must |
| REQ-003 | The simulator shall move the virtual ship along a fixed list of waypoints and advance to the next waypoint when the current one is reached. After the last waypoint it shall go back to the first. | Must |
| REQ-004 | The simulator shall send at a configurable rate from 1 to 50 ticks per second (`--rate`). Each tick sends one of each sentence type, and all sentences in a tick share one timestamp. | Must |
| REQ-005 | The simulator shall send each sentence as one UDP datagram to a configurable host and port (`--host`, `--port`). | Must |
| REQ-006 | The simulator shall let each fault type be turned on separately with its own probability: bad checksum, truncate, duplicate, reorder, drop, delay and burst. All faults shall be off by default. | Must |
| REQ-007 | The simulator shall let the user set the parameters of the reorder, delay and burst faults (`--max-reorder`, `--delay-min`, `--delay-max`, `--burst-min`, `--burst-max`). | Should |
| REQ-008 | Given the same options, the same `--seed` and the same `--start` time, the simulator shall produce the same bytes and the same fault pattern on every run. | Must |
| REQ-009 | Fault decisions and all values in the sentences (position, time, depth, wind) shall come from simulated time and not from the wall clock, so a change in `--rate` does not change which faults happen. | Must |
| REQ-010 | The simulator shall write a ground-truth log (`--log`) with one JSON line for every message. Each line holds the sentence number, copy number, position in the sent stream, send time, fault name, whether it was dropped, the original valid sentence and the text that was sent. | Must |
| REQ-011 | A dropped message shall not be sent but shall still appear in the ground-truth log. | Must |
| REQ-012 | The ground-truth log shall be replaced on each run and shall not be created unless `--log` is given. | Should |
| REQ-013 | When the run ends, the simulator shall send any messages it is still holding (reordered, delayed or part of a burst) so that every logged message is accounted for. | Must |
| REQ-014 | The simulator shall stop cleanly on SIGINT or SIGTERM, or after `--count` ticks, exit with status 0 and print a summary. | Must |
| REQ-015 | The simulator shall exit with status 2 and print the usage text for invalid options, and with status 1 for runtime errors such as an unwritable log path or a bad host. | Should |

### Sentence Parser (PARSE)

The parser is the `NMEAParser` class. It checks a sentence in stages and stops at the first stage that fails.

| ID | Requirement | Priority |
|---|---|---|
| REQ-020 | The parser shall check the frame: a start character (`$` or `!`), a `*` followed by exactly two hex digits, valid characters and a length limit (82 characters plus a small tolerance). | Must |
| REQ-021 | The parser shall check the checksum, which is the XOR of the bytes between the start character and `*`, and report a bad checksum separately from a bad frame. | Must |
| REQ-022 | The parser shall split the sentence into fields and keep empty fields. | Must |
| REQ-023 | The parser shall check the field count for the sentence type. It shall report an unsupported type separately from a wrong field count. | Must |
| REQ-024 | The parser shall check the format and range of each field. Empty optional fields shall become `std::nullopt`. | Must |
| REQ-025 | Each failing stage shall return its own error (`Malformed`, `BadChecksum`, `WrongFieldCount`, `InvalidField`, `UnsupportedSentence`, `TooLong`) and a text reason for the last failure. | Must |
| REQ-026 | On success the parser shall return a typed result (`Gga`, `Rmc`, `Hdt`, `Dpt` or `Mwv`) with the talker ID and the decoded fields. | Must |
| REQ-027 | Each stage shall be callable on its own. | Should |
| REQ-028 | The parser shall not crash on truncated, empty or garbled input. | Must |

### Code Layout (ARCH)

| ID | Requirement | Priority |
|---|---|---|
| REQ-030 | The code shall be split into libraries: `common` (checksum, UTC time, ship state), `sim` (sentence builder, injector, ground-truth log, parser) and `udp` (sender and receiver). | Should |
| REQ-031 | The UDP layer shall have a sender and a receiver. The receiver shall have a timeout and shall be able to bind to port 0 and report the port it got. | Should |
| REQ-032 | The project shall build with CMake and C++20 on Linux. | Must |

### Automated Tests (TEST)

| ID | Requirement | Priority |
|---|---|---|
| REQ-040 | The C++ unit tests (GoogleTest) shall cover the checksum, sentence builder, parser, injector, ground-truth log and UDP sender and receiver. There are 133 cases and they run with `ctest`. | Must |
| REQ-041 | The Python black-box tests (pytest) shall run the `nmea_sim` program, receive its UDP output and check it. There are 136 cases and each case reports its own PASS or FAIL. | Must |
| REQ-042 | The Python tests shall check the output with their own spec checker (`nmea.py`) and not with the C++ parser. The checker shall have its own self-check tests. | Must |
| REQ-043 | The Python tests shall cover each fault type, the clean feed, determinism, the ground-truth log, the command line and shutdown. | Must |
| REQ-044 | The Python tests shall find and build `nmea_sim` when needed, or use the path in the `NMEA_SIM` environment variable. | Should |

### Not Built Yet

These were planned earlier and do not exist in the code. They are not requirements of the current project.

- A receiver program that reads the feed and keeps vessel state, counters, a stale-data alarm or a CSV track export.
- TCP output and TCP input.
- A feed-loss fault (stop sending for a set time, then resume).
- Windows support. The code uses POSIX calls (`gmtime_r`, `netinet/in.h`).
