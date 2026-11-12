# Marine Test Harness

A C++ tool that sends fake NMEA 0183 boat data over UDP, with faults you can turn on. Use it to test software that reads NMEA data without needing a boat.

It sends GGA, RMC, HDT, DPT and MWV sentences from a virtual ship sailing a loop in Puget Sound. It can break the feed in seven ways: bad checksums, cut-off sentences, duplicates, reordering, drops, delays and bursts. It can also write a log of every message it made, so you know what the right answer is.

The same seed and start time give the same output every time, so you can replay a bad feed exactly.

## What you need

- Linux
- CMake 3.20 or newer
- A C++20 compiler (g++ or clang++)
- Python 3 with `pytest` (only for the Python tests)
- An internet connection the first time you build, to download GoogleTest

## Build

```
cmake -S . -B build
cmake --build build -j
```

This makes `build/nmea_sim` and `build/tests/unit/unit_tests`.

## Run the simulator

Send a clean feed to `127.0.0.1:10110` at 10 ticks per second until you press Ctrl-C:

```
./build/nmea_sim
```

Turn on some faults, log everything, and stop after 100 ticks:

```
./build/nmea_sim --count 100 --p-checksum 0.05 --p-drop 0.05 --p-duplicate 0.03 --log truth.jsonl
```

Replay the same feed again:

```
./build/nmea_sim --count 100 --seed 7 --start 1700000000 --p-drop 0.1 --log truth.jsonl
```

Run it twice with these options and you get the same bytes both times.

To see the data on the receiving end, listen on the port in another terminal:

```
nc -ul 10110
```

### Options

| Option | Meaning | Default |
|---|---|---|
| `--host ADDR` | where to send | 127.0.0.1 |
| `--port N` | UDP port | 10110 |
| `--rate 1-50` | ticks per second (each tick sends all 5 sentences) | 10 |
| `--count N` | number of ticks, 0 means run until Ctrl-C | 0 |
| `--speed KNOTS` | ship speed | 10 |
| `--start SECS` | start time as Unix seconds | now |
| `--seed N` | seed for the faults | 1 |
| `--log PATH` | write the ground-truth log here | no log |

Faults. Each one is a chance from 0 to 1 for each sentence. They all default to 0, and they must add up to 1 or less.

| Option | Fault |
|---|---|
| `--p-checksum P` | wrong checksum |
| `--p-truncate P` | sentence cut short |
| `--p-duplicate P` | sent twice |
| `--p-reorder P` | held back behind later sentences |
| `--p-drop P` | not sent (still in the log) |
| `--p-delay P` | sent late |
| `--p-burst P` | a group of sentences sent together |

Fault settings: `--max-reorder N` (3), `--delay-min MS` (200), `--delay-max MS` (3000), `--burst-min N` (3, at least 2), `--burst-max N` (8).

### The log

With `--log`, each message gets one JSON line:

```
{"seq":12,"copy":0,"emit":9,"t":1.200,"fault":"none","dropped":false,"original":"$GPGGA...*5A\r\n","text":"$GPGGA...*5A\r\n"}
```

- `seq`: the number of the sentence the simulator made
- `copy`: 0, or 1 for the extra copy from a duplicate
- `emit`: position in the stream that was sent, or `null` if dropped
- `t`: simulated seconds since the start
- `fault`: `none`, `bad_checksum`, `truncate`, `duplicate`, `reorder`, `drop`, `delay` or `burst`
- `original`: the correct sentence
- `text`: what was actually sent (empty if dropped)

## Run the tests

C++ unit tests (133 cases):

```
cd build && ctest
```

Python tests (136 cases, takes about 80 seconds):

```
python3 -m venv .venv
.venv/bin/pip install -r tests/python/requirements.txt
cd tests/python && ../../.venv/bin/python -m pytest
```

The Python tests build and run `build/nmea_sim` by themselves. To use a different binary, set `NMEA_SIM=/path/to/nmea_sim`.

## File structure

| Path | What it has |
|---|---|
| `src/main.cpp` | the `nmea_sim` program |
| `src/common`, `include/common` | checksum, UTC time, ship state |
| `src/sim`, `include/sim` | sentence builder, fault injector, ground-truth log, parser |
| `src/udp`, `include/udp` | UDP sender and receiver |
| `tests/unit` | GoogleTest tests |
| `tests/python` | Python tests that run the simulator and check its output |
| `requirements.md` | what the project does |

## Limits

- Linux only for now.
- There is no receiver program yet. The parser and UDP receiver are libraries used by the tests.
