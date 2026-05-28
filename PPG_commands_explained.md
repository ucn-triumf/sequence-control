# PPG Commands in `sequence_control_multi_valve.cxx`

This document explains how the VME-PPG32 pulse pattern generator is programmed
in the UCN sequencer frontend. It covers the hardware instruction format, the
VME register map, and a step-by-step walkthrough of every command written in
the code.

---

## 1. Background: The PPG32 Hardware

The PPG32 is a VME FPGA board with a **100 MHz internal clock** (10 ns per
tick) and **32 NIM outputs**. Its core is a small program memory that holds up
to 4096 instructions. Each instruction is **128 bits wide**, written to the
board four 32-bit registers at a time. The program executes sequentially,
driving the NIM output lines high/low according to each instruction's output
mask.

---

## 2. The 128-Bit Instruction Format

Every instruction encodes four things, spread across four 32-bit registers:

| Register | Bits in instruction | Meaning |
|----------|---------------------|---------|
| `reg1`   | 0–31                | **SET mask** — which output channels to drive HIGH |
| `reg2`   | 32–63               | **CLEAR mask** — which output channels to drive LOW |
| `reg3`   | 64–95               | **Delay** — number of extra 10 ns clock cycles to hold this state |
| `reg4`   | 96–127              | **Type + Data** — instruction opcode and loop count or branch address |

The `reg4` word encodes the opcode in bits 20–21 and a 20-bit payload in
bits 0–19:

| Opcode      | `reg4` value      | Meaning |
|-------------|-------------------|---------|
| Halt        | `0x000000`        | Stop execution |
| Continue    | `0x100000`        | Proceed to next instruction |
| Loop        | `0x200000 + N`    | Begin loop, repeat N times |
| End Loop    | `0x300000`        | Jump back to matching Loop |
| Call        | `0x400000 + addr` | Call subroutine at address |
| Return      | `0x500000`        | Return from subroutine |
| Branch      | `0x600000 + addr` | Unconditional jump to address |

> **Timing note:** The actual dwell time for a Continue instruction is
> `(3 + delay) × 10 ns`. The hardware always spends 3 clock cycles on
> instruction overhead before adding the programmed delay.

---

## 3. VME Register Map Used in the Code

The base address is `PPG_BASE = 0x00c00000` (line 136). Key offsets:

| Offset  | Name    | Purpose |
|---------|---------|---------|
| `+0x00` | CSR     | Control/status register |
| `+0x08` | Address | Pointer to which instruction slot to write next |
| `+0x0C` | reg1    | SET mask of the instruction being staged |
| `+0x10` | reg2    | CLEAR mask |
| `+0x14` | reg3    | Delay value |
| `+0x18` | reg4    | Type + data word |

Writing to `PPG_BASE + 0x08` selects the instruction slot, then writing to
`+0x0C`–`+0x18` loads that instruction into program memory.

---

## 4. The CSR Control Bits

Before and after writing instructions, the code pokes the CSR at
`PPG_BASE + 0x00`:

| Value written | Effect |
|---------------|--------|
| `0x8`         | **Reset** — clears the program counter and halts execution |
| `0x0`         | Idle (after reset); ready for an internal software trigger |
| `0x4`         | **External trigger mode** — waits for a rising edge on NIM input 4 to start |
| `0x1`         | **Software start** — immediately begins executing from instruction 0 |

For example, at the top of `set_ppg_sequence` (line 372) the board is reset
first, and in `callback_initiate` (line 511) it is software-started:

```cpp
mvme_write_value(myvme, PPG_BASE, 0x8);  // reset
mvme_write_value(myvme, PPG_BASE, 0x1);  // software trigger
```

---

## 5. The `set_command` Helper (line 286)

`set_command` is the single primitive for loading one instruction into the PPG.
All higher-level functions are built from calls to it:

```cpp
void set_command(int i,
                 unsigned int reg1,   // SET mask
                 unsigned int reg2,   // CLEAR mask
                 unsigned int reg3,   // delay
                 unsigned int reg4) { // type + data
    mvme_write_value(myvme, PPG_BASE+0x08, i);     // select instruction slot i
    mvme_write_value(myvme, PPG_BASE+0x0c, reg1);
    mvme_write_value(myvme, PPG_BASE+0x10, reg2);
    mvme_write_value(myvme, PPG_BASE+0x14, reg3);
    mvme_write_value(myvme, PPG_BASE+0x18, reg4);
}
```

---

## 6. Output Channel Map (lines 342–368)

The 32 NIM output channels map to physics signals. Bits in `reg1`/`reg2` are
numbered 0–31 for channels 1–32:

| Bits     | Channels | Signal |
|----------|----------|--------|
| 0–1      | 1–2      | Valve 1 state + monitor (`0x3 << 0`) |
| 2–3      | 3–4      | Valve 2 state + monitor (`0x3 << 2`) |
| 4–5      | 5–6      | Valve 3 state + monitor (`0x3 << 4`) |
| 6–7      | 7–8      | Valve 4 state + monitor (`0x3 << 6`) |
| 8–9      | 9–10     | Valve 5 state + monitor (`0x3 << 8`) |
| 10–11    | 11–12    | Valve 6 state + monitor (`0x3 << 10`) |
| 12–13    | 13–14    | Valve 7 state + monitor (`0x3 << 12`) |
| 14–15    | 15–16    | Valve 8 state + monitor (`0x3 << 14`) |
| 16–25    | 17–26    | Period 0–9 indicator signals (`0x1 << (16+i)`) |
| 28       | 29       | Timing calibration pulse (`0x10000000`) |
| 31       | 32       | Start-of-cycle pulse to V1725/chronobox (`0x80000000`) |

Each valve drives **two adjacent channels** simultaneously (one for the valve
drive signal and one for the monitor), hence the `(0x3 << 2k)` pattern.

---

## 7. Building a Sequence: `set_ppg_sequence` (line 369)

This function writes a complete PPG program for one cycle. Here is each block
in execution order.

### Step 1 — Safety halt (line 383)

```cpp
set_command(0, 0, 0, 0, 0);
```

Writes a Halt at slot 0 before doing anything else. If the PPG fires
prematurely (e.g. on an external trigger edge that arrives while the program
is still being written), it executes this Halt immediately rather than
re-running stale instructions from a previous cycle.

### Step 2 — 160 ns clear (line 407)

```cpp
set_command(command_index++, 0x0, 0xffffffff, 0x10, 0x100000);
```

| Field  | Value        | Interpretation |
|--------|--------------|----------------|
| SET    | `0x0`        | No outputs raised |
| CLEAR  | `0xffffffff` | All 32 outputs driven LOW |
| Delay  | `0x10` = 16  | (3+16) × 10 ns = 190 ns dwell |
| Type   | Continue     | Proceed to next instruction |

Ensures a clean slate at the start of every cycle.

### Step 3 — Start-of-cycle pulse (line 409)

```cpp
set_command(command_index++, 0x80000000, 0x7fffffff, 25, 0x100000);
```

| Field  | Value          | Interpretation |
|--------|----------------|----------------|
| SET    | `0x80000000`   | Channel 32 (bit 31) HIGH — triggers V1725 digitizer |
| CLEAR  | `0x7fffffff`   | All other 31 channels LOW |
| Delay  | 25             | (3+25) × 10 ns = 280 ns pulse width |
| Type   | Continue       | |

The V1725 digitizer and chronobox use this edge to timestamp the cycle start.

### Step 4 — Per-period loop construct (lines 417–447)

For each period `i` with a nonzero duration, three instructions are written.

#### Why a loop?

A 32-bit delay register at 100 MHz can hold at most `2³² × 10 ns ≈ 42 s`.
UCN periods can be longer. The solution is to split each period into
**100 equal sub-iterations** using the PPG's loop instruction:

```cpp
unsigned int ppg_time = (unsigned int)(dtime * 1e8 / 100.0);
```

This computes `dtime [s] × 10⁸ [cycles/s] / 100`, so each loop body holds for
`dtime / 100` seconds. Looping 100 times gives the full `dtime`.

#### The three instructions

```cpp
// 1. Begin loop — repeat 100 times (0x64 = 100)
set_command(command_index++, 0x0, 0x0, 0x0, 0x200064);

// 2. Body — hold valve and period signals for dtime/100 seconds
set_command(command_index++, enabled_outputs, ~enabled_outputs, ppg_time, 0x100000);

// 3. End loop — jumps back to instruction 1 until count is exhausted
set_command(command_index++, 0x0, 0x0, 0x0, 0x300000);
```

The `enabled_outputs` bitmask is assembled just above (lines 426–436):

```cpp
if(config_global.Valve1State[i]) enabled_outputs |= (0x3 << 0);
if(config_global.Valve2State[i]) enabled_outputs |= (0x3 << 2);
// ... etc for valves 3–8 ...
enabled_outputs |= (0x1 << (16 + i));  // period i indicator
```

`~enabled_outputs` as the CLEAR mask ensures that every output not in
`enabled_outputs` is explicitly driven LOW during this period.

### Step 5 — Sequence termination (lines 450–451)

```cpp
set_command(command_index++, 0x0, 0xffffffff, 0x1, 0x100000); // clear all, Continue
set_command(command_index++, 0x0, 0xffffffff, 0x1, 0x0);       // clear all, Halt
```

The second-to-last instruction drives all outputs LOW with a minimal 1-cycle
hold. The final Halt stops the PPG. The `read_event` function (line 752)
detects the Halt by polling bit 0 of the CSR falling to 0, then loads the
next cycle's program.

---

## 8. The Timing Calibration Sequence: `do_timing_sequence` (line 303)

Called at the start and end of every run, this fires **10 short pulses on
channel 29** at a 0.2 s cadence so that downstream DAQ systems can lock to an
absolute time reference.

```
Slot 0: blank 160 ns (clear all outputs)
Slot 1: Loop 10 times
Slot 2: Channel 29 HIGH, 280 ns pulse
Slot 3: All outputs LOW, ~0.2 s hold
Slot 4: End loop
Slot 5: Halt
```

In code (lines 315–327):

```cpp
set_command(0, 0x0,        0xffffffff, 0x10,     0x100000);  // blank
set_command(1, 0x0,        0x0,        0x0,      0x20000a);  // Loop ×10
set_command(2, 0x10000000, 0xefffffff, 25,       0x100000);  // CH29 HIGH
set_command(3, 0x00000000, 0xffffffff, off_time, 0x100000);  // all LOW
set_command(4, 0x0,        0x0,        0x0,      0x300000);  // End loop
set_command(5, 0x0,        0xffffffff, 0x1,      0x0);       // Halt
```

`0x10000000` = bit 28 = channel 29.  
`off_time = (unsigned int)(0.2 × 1e8) − 25` fills the remaining ~0.2 s after
the 280 ns pulse.

---

## 9. End-of-Cycle Auto-Advance

After the PPG halts, `read_event` (line 775) detects the transition and
advances the cycle and super-cycle counters, then calls `set_ppg_sequence`
again with the updated `gCycleIndex`:

```
CSR bit 0 falls to 0
    └─ sequence_finished = 1
            └─ gCycleIndex++
                    └─ set_ppg_sequence()   ← loads next cycle's valve pattern
```

If using an external hardware trigger (`config_global.ExternalTrigger`), the
newly loaded program waits silently for the next NIM edge before firing.

---

## 10. Summary: Full Data Flow

```
ODB Settings
    └─ setVariables()
            └─ config_global (valve states, period durations, cycle count)
                    └─ set_ppg_sequence()
                            ├─ mvme_write_value(PPG_BASE, 0x8)      reset
                            ├─ set_command(0..N)                    write program
                            ├─ mvme_write_value(PPG_BASE+8, 0x0)    rewind address
                            └─ mvme_write_value(PPG_BASE, 0x4/0x1)  arm/start
                                    └─ PPG hardware executes program
                                            └─ NIM outputs drive valves at precise timing
                                                    └─ read_event() polls CSR bit 0
                                                            └─ on Halt: advance index,
                                                                        reload sequence
```
