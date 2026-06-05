# UCN Sequencer control 
# Derek Fujimoto
# May 2026

import PPG
import time
import midas
import midas.frontend
import collections

"""Current connection status (May 27 2026):

PPG32 Output Channels 

1 IV001
2
3 UCN:SEQ:PPG2
4
5 UCN:SEQ:PPG3
6
7 UCN:SEQ:PPG4
8
9 UCN:SEQ:PPG6
10
...
28
29 V1725 CH11
30
31
32 V1725 CH10

PPG32 Input Channels

1
2
3               
4 UCN:SEQ:PPG7


The EPICS task refresh time is 100 ms. 
    Counting room <-> PLC communication is 0.1 s, 
    PLC <-> device communication is 0.01 s. 
"""

class UCNSequencer(midas.frontend.EquipmentBase):

    """
    **Output channel map**

    The 32 NIM output channels map to physics signals. Bits in the masks are numbered 0–31 for channels 1–32:

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

    **Input channel map**

    | Input     | Function                                                  |
    |-----------|-----------------------------------------------------------|
    | 4         | External start trigger (rising edge starts the sequence)  |
    | 3         | External 20 MHz clock input                               |
    | 2         | Unassigned                                                |
    | 1         | Unassigned                                                |
    """

    # Max number of valves - defined by the number of PPG channels (could be increased if we drop the monitoring channels)
    NVALVES = 9

    # name of this equipment
    NAME = 'UCNSequencer26'

    # default settings
    DEFAULT_SETTINGS = collections.OrderedDict([
        ("Enabled", False),
        ("HardwareTrigger", True),
        ("CyclesEnabled", [True]*10),
        ("PeriodsEnabled", [True]*5),
        ("PeriodDurations", [0.0]*50),      # interleaved [period0_cycle{0-n} period1_cycle{0-n}]
        ("ValveStates", [False]*NVALVES*5), # interleaved [period0_valve{0-n} period1_valve{0-n}]
                                            # 1 = energized (open for normally closed valves)
        ("ValveNames", [""]*NVALVES),
        ("PeriodNames", [""]*5),
        ("CurrentCycle", 0),
        ("CurrentPeriod", 0),
        ("CurrentSupercycle", 0),
        ("StopAtCycleEnd", False),
        ("StopAtSupercycleEnd", False),
        ("StopAtSupercycleN", -1),
    ])

    def __init__(self, client):
        """
        Args: 
            client (midas.client)        
        """

        # initialize connection to PPG via VME crate
        # self.ppg = PPG.PPG()

        # for messaging and reading ODB
        self.client = client

        # global variables # TODO: These may need fixing / readback
        self.enabled = False            # enable flag
        self.external_trigger = True    # use external (hardware) trigger
        self.nperiods = 0               # number of periods in cycle
        self.ncycles = 0                # number cycles in supercycle
        self.durations = [[]]           # [cycle][period] duration in s
        self.cyclei = 0                 # cycle index
        self.supcyclei = 0              # super cycle index
        self.valve_open = [False] * self.NVALVES
        self.time_last_print = 0        # time of last message print

        default_common = midas.frontend.InitialEquipmentCommon()
        default_common.equip_type = midas.EQ_PERIODIC
        default_common.buffer_name = "SYSTEM"
        default_common.trigger_mask = 0xffff
        default_common.event_id = 0x0001
        default_common.period_ms = 100
        default_common.read_when = midas.RO_ALWAYS
        default_common.log_history = 0
        default_common.hidden = False

        # You MUST call midas.frontend.EquipmentBase.__init__ in your equipment's __init__ method!
        midas.frontend.EquipmentBase.__init__(self, client, self.NAME, default_common, self.DEFAULT_SETTINGS)

    def detailed_settings_changed_func(self, path, idx, new_value):
        """
        You can define this function to be told about when the values in
        /Equipment/MyMultiPeriodicEquipment_1/Settings have changed.
        self.settings is updated automatically, and has already changed
        by this time this function is called.
        
        In this version you get told which setting has changed (down to
        specific array elements).
        """

        # enable/disable triggers start/stop ppg
        # cannot change settings of current cycle
        # hard/soft trigger states only if disabled

    def do_timing_sequence(self):
        """Short sequence of pulses at BOR and EOR for timing calibration
        
        Called at the start and end of every run, this fires 10 short pulses on 
        channel 29 at a 0.2s cadence so that downstream DAQ systems can lock to 
        an absolute time reference.
        """

        print("Setting up timing synchronization sequence")

        # Reset the PPG
        self.ppg.reset()
        self.ppg.halt(0) # probably redundant given the earlier reset
        
        # Use the software trigger to initiate the sequence (i.e. the start command)
        self.ppg.set_internal_trigger()

        # Add blank 100ns at the start of sequence...
        self.ppg.hold(0, 0x0, 0xffffffff, 7)

        # Pulse sequence: 
        #   ch 29 on for 250ns, remaining time off. 
        #   Cycle Period of 0.2s. 
        #   Repeat 10 times
        self.ppg.mark_loop_start(1, nloops=10)
        self.ppg.hold(2, 
                      mask_high=0x10000000, 
                      mask_low=0xefffffff, 
                      delay_ns=250)
        self.ppg.hold(3, 
                      mask_high=0x00000000, 
                      mask_low=0xffffffff, 
                      delay_ns=int(0.2e9 - 250 - 60)) # 30 ns for each hold instruction
        self.ppg.mark_loop_end(4)

        # End the sequence
        self.ppg.halt(5)
        
        # Start the sequence
        self.ppg.start()
        
        # Wait for the sequence to finish
        t0 = time.monotonic()
        while self.ppg.is_running:
            if time.monotonic()-t0 < 10:
                raise TimeoutError("PPG hung in do_timing_sequence")
            time.sleep(0.1)

    def exit(self):
        """Stop PPG on exit"""
        self.ppg.reset()
        self.ppg.set_internal_trigger()

    def set_ppg_sequence_cycle(self):
        """Set the ppg sequence for a single channel with valve states. Doesn't 
        start the sequence since we are potentially waiting on a hardware trigger"""

        self.ppg.reset()

        # sequencer disabled... halt and set internal trigger status
        if not self.enabled:
            self.ppg.set_internal_trigger()
            self.ppg.halt(0)
            print('Sequencer disabled')
            return
        
        # Set the first command to halt program. This ensures that if the 
        # sequence gets immediately triggered when we set to external trigger, 
        # then there will be a blank sequence to execute. If we don't do this, 
        # then we will restart the old sequence whenever we change parameters.
        self.ppg.halt(0)

        # now we can safely program the  rest of the sequence with the halt in place

        #### PPG SEQUENCE START ####

        # track command index
        idx = 1

        # Send 250ns pulse to V1725, indicating the start of the cycle
        self.ppg.hold(idx, mask_high=0x80000000, mask_low=0x7fffffff, delay_ns=250)
        idx += 1
        
        # track number of periods and times
        valid_periods = 0
        times = []
        
        for periodi in range(self.nperiods):

            # period duration in seconds
            duration = self.durations[self.cyclei][periodi]

            # skip zero duration periods
            if duration < 0.1:
                continue

            # Figure out which valves are enabled.  For each open valve we set 
            # two outlets high.
            enabled_outputs = 0
            for i in range(self.NVALVES):
                if self.valve_open[i]:
                    enabled_outputs += (0x3) << i*2

            # Add another output signal which indicates which period we are in.
            enabled_outputs += (0x1) << (16+i)

            # Now write the actual commands to open/close valves
            # Looping to get around 32-bit limitation in max limit per command 
            # (max of 42s otherwise).
            self.ppg.mark_loop_start(idx, nloops=100)
            idx += 1
            self.ppg.hold(idx, 
                          mask_high= enabled_outputs, 
                          mask_low = ~enabled_outputs, 
                          delay_ns = duration*1e9/100.0)
            idx += 1
            self.ppg.mark_loop_end(idx)
            idx += 1

            valid_periods += 1
            times.append(duration)

        # Close all valves
        self.ppg.hold(idx, 
                      mask_high= 0x0, 
                      mask_low = 0xffffffff, 
                      delay_ns = 1)
        idx += 1

        # stop 
        self.ppg.halt(idx)

        # Add blank 100ns at the start of sequence, overwriting the halt at slot 0
        self.ppg.hold(0, mask_high=0x0, mask_low=0xffffffff, delay_ns=100)

        # Set trigger source to inititate the sequence, arming the sequence for 
        # execution on next trigger 
        if self.external_trigger:
            self.ppg.set_external_trigger()
        else:
            self.ppg.set_internal_trigger()

        #### PPG SEQUENCE END ####

        # Print a description of the new cycle to midas, but only if it hasn't 
        # been written in last five seconds
        t0 = time.monotonic() # avoid daylight savings time shenanigans
        if t0 - self.time_last_print > 5:
            self.client.msg(f'Setup new cycle: {self.nperiods} ({valid_periods} non-zero): ' +
                            f'period times = {times} (seconds): ' +
                            f'cycle/supercycle index = {self.cyclei}/{self.supcyclei} ' + 
                            'External hardware' if self.external_trigger else 'Internal software' + 
                            ' trigger.')  
            self.time_last_print = t0

    def start_ppg(self):
        """Implement software trigger"""
        if self.external_trigger: 
            raise RuntimeError("Cannot send software trigger with external trigger set to True")
        self.ppg.start()

    def readout_func(self):
        """
        Repeatedly check if we are in a cycle, if so don't do anything. 
        If we are out of a cycle, then start the next cycle

        Bank format for USEQ:
            word 0 -> second portion of current time
            word 1 -> millisecond portion of current time
            word 2 -> second portion of cycle start time
            word 3 -> millisecond portion of cycle start time
            word 4 -> bit 0: are we in cycle? 
                      bit 1: did cycle just start?
                      bit 2: did cycle just end?
            word 5 -> is sequencer enabled?
            word 6 -> cycle index
            word 7 -> super-cycle index
        
        NSEQ is a full copy of the sequencer settings currently in the program.
        """

        # check if enabled
        if not self.settings['Enabled']:
            return
        

class SequencerFE(midas.frontend.FrontendBase):
    """
    Sequencer frontend
    """
    def __init__(self):
        midas.frontend.FrontendBase.__init__(self, "fe_ucnsequencer")
        self.add_equipment(UCNSequencer(self.client))
        self.client.msg("UCN Sequencer initialized.")

    def begin_of_run(self, run_number):

        # check if equipment is enabled
        # TODO: fill this in

        # do timing sequence
        self.set_all_equipment_status("Timing sequence", "greenLight")
        # self.equipment[UCNSequencer.NAME].do_timing_sequence()
        self.set_all_equipment_status("Running", "greenLight")

    def end_of_run(self, run_number):
        self.set_all_equipment_status("Ready", "greenLight")

    def frontend_exit(self):
        self.equipment[UCNSequencer.NAME].exit()
        self.client.msg("UCN Sequencer frontend stopped.")

if __name__ == "__main__":
    with SequencerFE() as fe:
        fe.run()
