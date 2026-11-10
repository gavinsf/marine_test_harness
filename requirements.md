## Requirements

### Feed Simulator (SIM)

| ID | Requirement | Priority |
|---|---|---|
| REQ-001 | The simulator shall generate GGA, RMC, HDT and DPT sentences, each with a correct checksum when no fault is applied. | Must |
| REQ-002 | The simulator shall format latitude as `ddmm.mmmm` and longitude as `dddmm.mmmm` with the correct N/S/E/W indicator. | Must |
| REQ-003 | The simulator shall move the virtual ship along a configured list of waypoints, advancing to the next waypoint when the current one is reached. | Must |
| REQ-004 | The simulator shall send sentences at a configurable rate between 1 Hz and 50 Hz. | Must |
| REQ-005 | The simulator shall support output over UDP and over TCP, selectable at start-up. | Must |
| REQ-006 | The simulator shall allow each fault type (bad checksum, truncation, duplicate, reorder, delay and jitter, burst, feed loss) to be enabled independently with its own parameters. | Must |
| REQ-007 | Given the same configuration and random seed, the simulator shall produce the same sequence of messages and faults. | Must |
| REQ-008 | The simulator shall write a ground-truth log containing, for every message sent, the send time, the raw text, and the fault applied (if any). | Must |
| REQ-009 | The simulator shall stop sending for a configured duration when the feed-loss fault is triggered, then resume. | Must |

### Receiver: Parsing and Validation (RX)

| ID | Requirement | Priority |
|---|---|---|
| REQ-010 | The receiver shall parse valid GGA, RMC, HDT and DPT sentences and update the vessel state (position, speed, heading, depth). | Must |
| REQ-011 | The receiver shall validate the checksum of every sentence and shall reject any sentence with an invalid checksum without changing the vessel state. | Must |
| REQ-012 | The receiver shall reject truncated or malformed sentences (missing `$`, missing `*`, missing or short checksum, wrong field count) without crashing and without changing the vessel state. | Must |
| REQ-013 | The receiver shall tolerate empty fields in a valid sentence and shall retain the last valid value for any field that is empty. | Should |
| REQ-014 | The receiver shall ignore sentence types it does not support and shall count them. | Should |

### Receiver: Fault Handling (RX)

| ID | Requirement | Priority |
|---|---|---|
| REQ-015 | The receiver shall detect duplicate sentences and shall not add duplicate points to the vessel track. | Must |
| REQ-016 | The receiver shall not move the vessel state backward in time when a sentence arrives out of order, and shall count it as out-of-order. | Must |
| REQ-017 | The receiver shall raise a stale-data alarm within 3 seconds after the last valid position sentence. | Must |
| REQ-018 | The receiver shall clear the stale-data alarm within 1 second after valid position sentences resume, without a restart. | Must |
| REQ-019 | The receiver shall keep the vessel state consistent (no invalid or reversed positions) when message delay jitter is up to 500 ms. | Should |
| REQ-020 | The receiver shall process every valid sentence in a burst of 500 sentences delivered within 100 ms, with none dropped. | Must |

### Receiver: Transport and Performance (RX)

| ID | Requirement | Priority |
|---|---|---|
| REQ-021 | The receiver shall accept more than one sentence in a single UDP datagram. | Should |
| REQ-022 | The receiver shall correctly reassemble sentences that are split across TCP segments or that share a TCP segment. | Must |
| REQ-023 | The receiver shall re-establish a TCP connection within 5 seconds after the simulator restarts. | Should |
| REQ-024 | With no faults injected, the receiver shall sustain 100 sentences per second for 10 minutes on the loopback interface with zero sentences lost. | Must |
| REQ-025 | For 95% of sentences, the receiver shall update the vessel state within 50 ms of receipt. | Should |

### Receiver: Reporting and Platform (RX)

| ID | Requirement | Priority |
|---|---|---|
| REQ-026 | The receiver shall export the vessel track as a CSV file with columns: timestamp, latitude, longitude, speed, heading, depth. | Must |
| REQ-027 | The receiver shall report counters for accepted, checksum-rejected, malformed, duplicate, out-of-order and unsupported sentences. | Must |
| REQ-028 | The simulator and receiver shall build and pass all automated tests on both Windows and Linux with identical results. | Must |