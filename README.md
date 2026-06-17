# The UCN Sequencer

This document is an orientation guide to the UCN sequencer: what it does, the
hardware it drives, the concepts it is built around, and how the software is
organized. It is meant to be read first, before diving into the source files.
For a low-level walkthrough of how the PPG hardware is programmed, see
[`PPG_commands_explained.md`](PPG_commands_explained.md).

---

## 1. What the sequencer does

The TUCAN source operates as a repeating timed sequence. Neutrons are produced, 
guided through a network of pneumatic valves, stored, and counted. Each of these 
actions has to happen at a precise, repeatable moment relative to the start of a 
measurement, and the start of every measurement has to be timestamped so the 
data-acquisition (DAQ) systems can line up their recorded events against it.

The **sequencer** is the MIDAS frontend program that orchestrates this. Its job is
to:

1. Drive a set of digital output lines (NIM logic levels) that open and close
   valves and signal which phase of the experiment is currently active.
2. Emit a start-of-cycle pulse that downstream digitizers (V1725) and the
   chronobox use to timestamp the cycle.
3. Step automatically through a programmed list of timing configurations, run
   after run, cycle after cycle, without operator intervention.
4. Record what it did into the MIDAS data stream and the online database (ODB)
   so the configuration is stored alongside the physics data.

All of the precise timing is produced by a dedicated piece of hardware — the
**PPG32 pulse pattern generator**. The Python frontend's role is to translate a
human-friendly configuration (valve states and durations) into a PPG program,
load it, arm it, and supervise its execution. The frontend does **not** generate
the timing itself; software jitter would be far too coarse. It hands a compiled
program to the FPGA and steps back.

---

## 2. The core vocabulary

Four nested concepts define everything the sequencer does. Understanding them is
the key to understanding the rest of the system.

| Term | Meaning |
|------|---------|
| **Valve** | A single physical pneumatic valve in the UCN beamline. Each valve has a state (open/closed) that can differ in every period. |
| **Period** | A phase of the experiment with a fixed valve configuration held for a fixed duration. E.g. "irradiation", "storage", "counting". A period is the smallest timed unit. |
| **Cycle** | An ordered list of periods executed back-to-back. One cycle is one complete pass through all enabled periods. Each cycle can assign different durations to its periods. |
| **Supercycle** | One pass through all enabled cycles. After the last enabled cycle finishes, the supercycle counter increments and the sequence wraps back to the first cycle. |

So the structure is: a **supercycle** contains several **cycles**; each cycle
contains several **periods**; each period sets the state of every **valve** and
holds it for a duration.

A concrete way to picture the configuration is a table:

* Columns are indexed by cycle.
* For each cycle, every enabled period has a **duration** (seconds).
* For each period, every valve has an **open/closed** state.

The same valves and periods exist in every cycle; what changes from cycle to
cycle is the **period durations** (and which cycles/periods are enabled). This is
how, for example, you run the same measurement at several different storage times
within a single supercycle.

---

## 3. The hardware

### 3.1 PPG32 pulse pattern generator

The PPG32 is a VME FPGA board. Its key properties:

* **100 MHz internal clock** → 10 ns timing resolution (one "tick").
* **32 NIM digital outputs** → drive valves, period indicators, and trigger pulses.
* **Program memory** holding up to **4096 instructions**, each **128 bits** wide.
* Sequential execution: the board steps through its instruction list, driving the
  32 output lines according to each instruction's bit masks, until it hits a halt.

Each instruction is written as four 32-bit registers encoding:

1. A **SET mask** — which of the 32 outputs to drive HIGH.
2. A **CLEAR mask** — which outputs to drive LOW.
3. A **delay** — how many extra 10 ns ticks to hold this state.
4. An **opcode + payload** — halt, continue, loop start/end, subroutine call/return,
   or branch (with a loop count or jump address).

> **Timing detail:** the real dwell time of an instruction is `(3 + delay) × 10 ns`.
> The hardware always spends 3 ticks of overhead before applying the programmed
> delay.

Because a single 32-bit delay register tops out at roughly **42 s** (`2³² × 10 ns`),
long periods are built by wrapping the hold instruction in a **loop**. The
sequencer splits each period into 100 equal sub-intervals and loops 100 times,
which extends the maximum achievable period far beyond the single-instruction
limit.

### 3.2 Triggering

The PPG can be started two ways:

* **Internal (software) trigger** — the frontend writes a start command and the
  program begins immediately. Useful for testing and for the timing-calibration
  sequence.
* **External (hardware) trigger** — the program is armed and then waits silently
  for a rising edge on **NIM input 4** before it begins. This is the normal
  running mode: the cycle start is locked to an external timing signal (e.g. the
  beam/kicker).

The choice is exposed in the web UI as the "Use external (hardware) trigger"
checkbox and stored in the ODB as `HardwareTrigger`.

### 3.3 Output channel map

The 32 outputs are assigned as follows (bits are numbered 0–31 for channels 1–32):

| Bits | Channels | Signal |
|------|----------|--------|
| 0–1   | 1–2   | Valve 1 drive + monitor (`0x3 << 0`) |
| 2–3   | 3–4   | Valve 2 drive + monitor (`0x3 << 2`) |
| …     | …     | … (each valve uses two adjacent channels) |
| 14–15 | 15–16 | Valve 8 drive + monitor (`0x3 << 14`) |
| 16–25 | 17–26 | Period 0–9 "which period am I in" indicator (`0x1 << (16+i)`) |
| 28    | 29    | Timing-calibration pulse |
| 31    | 32    | Start-of-cycle pulse to the V1725 / chronobox (`0x80000000`) |

Each valve occupies **two adjacent channels** — one drive line and one monitor
line — which is why valve `k` is set with the pattern `0x3 << 2k`.

**Inputs:** input 4 is the external start trigger (rising edge), input 3 is an
external 20 MHz clock input; inputs 1–2 are unassigned.

### 3.4 VME crate

The PPG32 lives in a VME crate. The frontend talks to it through a small C shared
library (`build/libvme.so`, the MVME standard interface) wrapped by Python via
`ctypes`. Every PPG register access is ultimately a single-word read or write to a
VME bus address at base `0x00c00000`.

---

## 4. Software architecture

The code is layered, from the metal up to the browser:

```
 Web UI (HTML + JS)          operator view / control
        │  (ODB via mjsonrpc)
        ▼
 MIDAS ODB                   shared configuration + status
        │
        ▼
 Sequencer.py                MIDAS frontend + experiment logic
        │
        ▼
 PPG.py                      PPG32 instruction encoding / commands
        │
        ▼
 VME.py                      ctypes wrapper around libvme.so
        │
        ▼
 PPG32 hardware              NIM outputs → valves + triggers
```

### 4.1 `VME.py` — the VME transport

The lowest layer. It loads `libvme.so`, opens the crate, sets the address modifier
(A32 non-privileged data) and data mode (D32), and exposes `read_value` /
`write_value` for single-word bus access. Nothing here knows anything about the
PPG; it only moves 32-bit words to and from VME addresses.

### 4.2 `PPG.py` — the PPG command set

Wraps the raw register protocol into named instructions. The central primitive is
`_set_command(idx, mask_high, mask_low, delay_10ns, instr)`, which writes one
128-bit instruction into program slot `idx`. Built on top of it:

* `hold(idx, mask_high, mask_low, delay_ns)` — set outputs and hold for a duration.
* `halt(idx)` — stop execution, drive all outputs low.
* `mark_loop_start` / `mark_loop_end` — bracket a loop body.
* `subroutine_call` / `subroutine_return` / `branch` — flow control (available but
  not used by the current sequence builder).
* `start()`, `reset()` — control execution.
* `set_internal_trigger()` / `set_external_trigger()` — choose the trigger source.
* `is_running` — a property that reads CSR bit 0 to tell whether the program is
  still executing.

`PPG.py` also defines **`PPG_Mock`**, a drop-in replacement that emulates timing in
software (it tracks accumulated runtime and reports `is_running` accordingly)
without touching any hardware. The mock's `set_external_trigger` even sleeps 2 s to
imitate waiting for a trigger. This is what allows the frontend to be developed and
tested on a machine with no VME crate attached.

### 4.3 `Sequencer.py` — the frontend and the experiment logic

This is the heart of the program. It defines two classes:

* **`UCNSequencer`** — a MIDAS `EquipmentBase`. It owns the PPG object, holds the
  default ODB settings, builds PPG programs, advances the cycle/supercycle
  counters, and packs the per-cycle records into MIDAS banks.
* **`SequencerFE`** — a MIDAS `FrontendBase` that wires the equipment in and
  handles run transitions (begin/end of run).

The most important methods:

* **`set_ppg_cycle_sequence()`** — compiles the current cycle into a PPG program.
  It reads the live ODB settings, writes a safety halt at slot 0 first (so a
  premature trigger does nothing), emits the start-of-cycle pulse, then for each
  enabled non-zero-duration period assembles the valve+period output mask and
  writes a 100-iteration loop holding it for the period duration. It closes all
  valves, halts, overwrites slot 0 with a short blank, and finally arms the chosen
  trigger source. It does **not** call `start()` — for hardware triggering the PPG
  must sit armed and wait.

* **`readout_func()`** — the periodic (100 ms) MIDAS callback that drives the state
  machine. It checks `is_incycle` (i.e. whether the PPG is still running) against
  the previous state to detect cycle start and cycle end, emits the appropriate
  MIDAS banks at each transition, and — when idle and not waiting for a trigger —
  calls `iterate_cycle()` and `set_ppg_cycle_sequence()` to arm the next cycle. A
  `waiting_for_trigger` flag handles the case where the next program is armed but
  the external trigger has not yet arrived.

* **`iterate_cycle()`** — advances to the next enabled cycle, wrapping and
  incrementing the supercycle counter as needed, and honoring the various stop
  conditions (stop after this cycle, after this supercycle, after supercycle N).

* **`do_timing_sequence()`** — at begin- and end-of-run, fires 10 short pulses on
  channel 29 at a 0.2 s cadence using the internal trigger, giving downstream DAQ a
  shared absolute time reference. It blocks until the burst completes (with a 10 s
  timeout guard).

* **Bank builders** — `create_bank_SEQC` (cycle timing + period durations),
  `create_bank_SEQV` (valve states at begin-of-run), and `create_bank_SEQN`
  (period and valve names). These both go into the MIDAS event stream and are
  mirrored into the ODB `Variables` tree for the web UI.

* **`reset()` / `exit()`** — prepare counters at start of run, and cleanly halt the
  PPG and disable the sequencer at end of run or on exit.

### 4.4 The ODB settings

`UCNSequencer.DEFAULT_SETTINGS` defines the contract between the UI, the ODB, and
the frontend. The notable entries:

| Setting | Meaning |
|---------|---------|
| `Enabled` | Master on/off; programming the PPG only happens when enabled. |
| `HardwareTrigger` | External vs. internal trigger. |
| `CyclesEnabled` | Per-cycle enable flags (length = number of cycles). |
| `PeriodsEnabled` | Per-period enable flags (length = number of periods). |
| `PeriodDurations` | Flat array of durations, interleaved as `[period0_cycle{0..n}, period1_cycle{0..n}, …]`. |
| `ValveStates` | Flat array of valve open/closed flags, interleaved by period; `1` = energized. |
| `ValveNames`, `PeriodNames` | Human-readable labels. |
| `CurrentCycle`, `CurrentSupercycle` | Live counters. |
| `StopAtCycleEnd`, `StopAtSupercycleEnd`, `StopAtSupercycleN`, `StartAtCycleN` | Run-flow controls. |

The interleaved flat arrays are how 2-D (cycle × period) and (period × valve)
tables are stored in the ODB's 1-D arrays. The web UI reconstructs the table
dimensions from the array lengths.

---

## 5. The web interface

The operator interface is a MIDAS custom page, `sequencer26.html`, assembled from
several JavaScript modules that talk to the ODB through `mjsonrpc`:

* **`setup.js`** — entry point; draws every panel once at load, then registers
  1 s interval refreshes for each.
* **`run_control.js`** — start/stop run controls and run state.
* **`sequencer_control.js`** — the trigger mode and stop-condition controls.
* **`sequencer_status.js`** — live status: in-cycle flag, current cycle/supercycle,
  time left in cycle and supercycle, supercycle duration (computed from the
  enabled cycles' period durations).
* **`period_timings.js`** — the large editable table of period durations and valve
  states, including add/remove buttons for cycles, periods, and valves, and
  highlighting of the cycle currently in progress.
* **`beam_status.js`** — beamline / kicker / target status and predicted beam
  current.
* **`file_browser.js`** — save/load of sequencer configurations.

The page is purely a view and editor over the ODB. All authority lives in the
frontend; the browser only reads and writes ODB values.

---

## 6. End-to-end data flow

Putting it together, one run proceeds like this:

```
Operator edits valve states / durations in the browser
        └─ values written to ODB Settings
Run start (begin_of_run)
        ├─ reset() counters, clear end-of-run comment
        ├─ do_timing_sequence()  → 10 calibration pulses (channel 29)
        └─ Enabled = True
Periodic readout_func() (every 100 ms):
        ├─ PPG idle + not waiting?
        │     ├─ iterate_cycle()             advance cycle/supercycle, apply stops
        │     └─ set_ppg_cycle_sequence()    compile + arm next cycle's program
        │             └─ PPG armed; waits for trigger (or starts, if internal)
        ├─ trigger arrives → PPG runs → NIM outputs drive valves on schedule
        │     └─ start-of-cycle pulse timestamps the cycle at the V1725/chronobox
        ├─ cycle start detected → emit SEQC bank (and SEQN/SEQV at first cycle)
        └─ cycle end detected   → emit SEQC bank, arm the following cycle
Run stop (end_of_run)
        └─ exit() → halt PPG, disable sequencer
```

The transition sequence numbers are chosen (`TR_START` 550, `TR_STOP` 450) so the
sequencer begins **after** the digitizers at start of run and stops **before** them
at end of run, ensuring the DAQ is always recording while cycles are firing.

---

## 7. Where to go next

* [`PPG_commands_explained.md`](PPG_commands_explained.md) — bit-level detail of
  the instruction format, register map, and every command the sequence builder
  writes.
* `Sequencer.py` — the frontend and experiment logic; start with
  `set_ppg_cycle_sequence` and `readout_func`.
* `PPG.py` — the command set, plus `PPG_Mock` for hardware-free testing.
* `VME.py` — the VME transport layer.
* `sequencer26.html` and the `*.js` modules — the operator interface.
