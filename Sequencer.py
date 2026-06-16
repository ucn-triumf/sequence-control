# UCN Sequencer control 
# Derek Fujimoto
# May 2026

from PPG import PPG_Mock as PPG # testing
import time
import midas
import midas.frontend
import midas.event
import collections
import numpy as np

# TODO: Overview of how this works. See https://github.com/ucn-triumf/sequence-control/blob/master/sequence_control_multi_valve.cxx
# TODO: clear after-run comment on run start

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
        ("CurrentSupercycle", 0),
        ("StopAtCycleEnd", False),
        ("StopAtSupercycleEnd", False),
        ("StopAtSupercycleN", 1000),
        ("StartAtCycleN", 0),
    ])

    def __init__(self, client):
        """
        Args: 
            client (midas.client)        
        """

        # initialize connection to PPG via VME crate
        self.ppg = PPG()

        # for messaging and reading ODB
        self.client = client

        # flag to wait for trigger - handles the case where we set the next 
        # cycle, are not running, but wait a long time for the next trigger to 
        # arrive
        self.waiting_for_trigger = False

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

        # disable enable - prevent ppg programming after crash
        self.set('Enable', False)

        # was in cycle - use for detecting cycle start
        self.was_incycle = False

        # number of cycles started
        self.ncycles_started = 0

        # period durations set in PPG programming
        self.period_times = []

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
            if time.monotonic()-t0 > 10:
                raise TimeoutError("PPG hung in do_timing_sequence")
            time.sleep(0.1)
        print('Timing sequence finished')

    def exit(self):
        """Stop PPG on exit"""
        self.set('Enabled', False)
        self.set('StartAtCycleN', 0)
        self.ppg.reset()
        self.ppg.halt(0)
        self.ppg.set_internal_trigger()
        self.waiting_for_trigger = False

    def set_ppg_cycle_sequence(self):
        """Set the ppg sequence for a single channel with valve states. Doesn't 
        start the sequence since we are potentially waiting on a hardware trigger"""

        self.ppg.reset()

        # sequencer disabled... halt and set internal trigger status
        if not self.settings['Enabled']:
            self.ppg.set_internal_trigger()
            self.ppg.halt(0)
            return
        
        # some constants from the ODB - don't allow changes mid-programming of the PPG
        period_durations = self.get('PeriodDurations')
        period_enabled = self.get('PeriodsEnabled')
        current_cycle = self.get('CurrentCycle')
        valve_open = self.get('ValveStates')
        valve_names = self.get('ValveNames')
        ncycles = len(self.get('CyclesEnabled'))
        nperiods = len(period_enabled)

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
        self.period_times = []
        
        for periodi in range(nperiods):

            # skip disabled periods
            if not period_enabled[periodi]:
                continue

            # period duration in seconds
            duration = period_durations[periodi * ncycles + current_cycle]

            # skip zero duration periods
            if duration > 0.1:

                # Figure out which valves are enabled.  For each open valve we set 
                # two outlets high.
                enabled_outputs = 0
                nvalves = len(valve_names)
                for valvei in range(nvalves):
                    if valve_open[valvei * nvalves + periodi]:
                        enabled_outputs += (0x3) << valvei*2

                # Add another output signal which indicates which period we are in.
                enabled_outputs += (0x1) << (16+periodi)

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

            # record  - keep zero duration periods for bank in SEQC
            self.period_times.append(duration) 

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
        if self.hardware_trigger:
            self.ppg.set_external_trigger()

        else:
            self.ppg.set_internal_trigger()

        #### PPG SEQUENCE END ####

        # Print a description of the new cycle to midas
        self.client.msg(f'Setup cycle {current_cycle} of '+
                        f'supercycle {self.current_supercycle} with ' +
                        ('hardware' if self.hardware_trigger else 'software') + 
                        ' trigger. Periods: ' + 
                        '/'.join(map(str, self.period_times))
                        )  

    def start_ppg(self):
        """Implement software trigger"""
        if self.hardware_trigger: 
            raise RuntimeError("Cannot send software trigger with external trigger set to True")
        self.ppg.start()

    def iterate_cycle(self):
        """Set up for the next cycle, assume that the current cycle is finished"""

        # check stop after this cycle
        if self.get("StopAtCycleEnd"):
            self.exit()
            self.client.stop_run()

        # next cycle number
        cyclei = (self.current_cycle+1) % self.ncycles
        
        # get the next enabled cycle
        nattempts = 0
        while not self.cycles_enabled[cyclei]:
            cyclei = (cyclei + 1) % self.ncycles

            # check that there are enabled cycles, if not hang with error message
            if nattempts > self.ncycles:
                nattempts = 0
                self.client.msg('No cycles enabled! Enable a cycle to continue.', 
                                is_error=True)
                self.client.communicate(5000)
            
            nattempts += 1

        # iterate number supercycles
        if cyclei <= self.current_cycle:
            self.set('CurrentSupercycle', self.current_supercycle+1)

            # check for end at end of this supercycle
            if self.get('StopAtSupercycleEnd'):
                self.exit()
                self.client.stop_run()

            # check for stop at end of next supercycle
            if self.get("StopAtSupercycleN") == self.current_supercycle:
                self.set('StopAtSupercycleEnd', True)

        # iterate current cycle
        self.set('CurrentCycle', cyclei)
        self.ncycles_started += 1
        self.client.communicate(10) # needed so self.settings picks up the changes

    def create_bank_SEQC(self):
        """Make midas bank (SEQuencer Cycle) to record the crude timings of the 
        cycle start and stop, as well as the period durations during each cycle.
        
        Bank format (4 byte words): 
            * word 0 -> second portion of current time
            * word 1 -> millisecond portion of current time
            * word 2 -> 1 if cycle just started, 0 if cycle stop
            * word 3 -> cycle unique id
            * word 4 -> cycle count
            * word 5 -> super-cycle count
            * word 6 -> number of enabled periods (nperiods)
            * word 7 - 7+nperiods -> period durations in seconds
        """

        # create storage array
        nperiods = len(self.period_times)
        data = np.zeros(nperiods+7, dtype=np.uint32) # force uint32 cast at assignment

        # get time
        t = time.time()
        data[0] = t                              # time, seconds
        data[1] = np.round((t - data[0])*1000)   # time, ms

        # start / stop status
        # if we're in the cycle, the cycle must have just started
        data[2] = self.is_incycle

        # current cycle id
        data[3] = self.current_cycle

        # number of cycles started (but not necessarily finished)
        data[4] = self.ncycles_started

        # number of supercycles started (but not necessarily finished)
        data[5] = self.current_supercycle + 1

        # number of periods enabled
        data[6] = nperiods

        # period durations in order
        for i, periodt in enumerate(self.period_times):
            data[7+i] = periodt

        # make the bank
        bank = midas.event.Bank()
        bank.name = 'SEQC'
        bank.type = midas.TID_UINT32 # TID_UINT32 = TID_DWORD
        bank.data = data

        # udpate the ODB
        self.client.odb_set(f'/Equipment/{self.NAME}/Variables/SEQC', data)

        return bank
        
    def create_bank_SEQV(self):
        """Make midas bank (SEQuencer Valve) to record the valve settings at BOR.

        Bank format (4 byte words): 
            * word 0 -> number of valves
            * word 1 -> number of enabled periods
            * word 2 -> valve states of first enabled period
                * bit1: valve 1 
                * bit2: valve 2
                * ...
            * word 3 -> valve states of second enabled period
            * ...

        Note:
            * Supports a max of 32 valves - this is ok since the PPG has only 32 channel anyway
            * Valve bit number from the bank format does not correspond to the PPG channel, rather the name of the valve (see bank SEQN)
        """

        # enabled periods
        periods_enabled = self.get('PeriodsEnabled')
        nperiods = len(periods_enabled)
        nperiods_enabled = sum(periods_enabled)

        # valves
        valve_states = self.get("ValveStates")
        nvalves = len(valve_states) // nperiods

        # init bank data
        data = np.zeros(2+nperiods_enabled, dtype=np.uint32)
        data[0] = nvalves
        data[1] = nperiods_enabled

        # set valve states
        idx = 0
        enbl_periodi = 0 # enabled period index
        for periodi in range(nperiods):

            # skip disabled periods
            if not periods_enabled[periodi]:
                idx += nvalves
                continue

            # set valve state bits
            for valvei in range(nvalves):
                if valve_states[idx]:
                    data[enbl_periodi+2] |= 1<<valvei # +2 accounts for nvalues and nenabled
                idx += 1
            enbl_periodi += 1

        # make bank
        bank = midas.event.Bank()
        bank.name = 'SEQV'
        bank.type = midas.TID_UINT32 # TID_UINT32 = TID_DWORD
        bank.data = data

        # update ODB
        self.client.odb_set(f'/Equipment/{self.NAME}/Variables/SEQV', data)
        return bank
    
    def create_bank_SEQN(self):
        """Make midas bank (SEQuencer period/valve Names) to record the settings at BOR.

        Bank format (raw bytes, encoding strings): 
            * enabled period names
            * valve names

        Note:
            * names are delminated by the "Unit Separator" ASCII 31 (0x1F)
            * the two sets of names are deliminated by the "Group Separator" ASCII 29 (0x1D)"" 
            * strings are encoded as UTF-8
            * Ex: "period1\x1fperiod1\x1fperiod3\x1dvalve1\x1fvalve2\x1fvalve3"
        """

        # get names
        periods_enabled = np.array(self.get('PeriodsEnabled')).astype(bool)
        period_names = np.array(self.get('PeriodNames'))[periods_enabled]
        valve_names = self.get('ValveNames')

        # make data
        data = (chr(31).join(period_names) + 
                chr(29) + 
                chr(31).join(valve_names))
        
        # make bank
        bank = midas.event.Bank()
        bank.name = 'SEQN'
        bank.type = midas.TID_BYTE
        bank.data = data.encode("UTF-8")

        # update ODB
        self.client.odb_set(f'/Equipment/{self.NAME}/Variables/SEQN', 
                            data.replace(chr(31), ',').replace(chr(29), ';'))
        
        return bank
    
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
            self.waiting_for_trigger = False
            self.was_incycle = False
            return

        # midas event to return
        event = midas.event.Event()
        event.header.event_id = 1
        event.header.serial_number = self.ncycles_started

        # the cycle has ended - must go before setting up the PPG in case of 
        # fast trigger
        if not self.is_incycle and self.was_incycle:
            event.add_bank(self.create_bank_SEQC())
            self.was_incycle = False
            self.waiting_for_trigger = False
        
        # if we are not running, and not waiting for the PPG to trigger, then 
        # set up the next cycle
        if not self.is_incycle and not self.waiting_for_trigger:

            # start of run - create settings banks
            if self.current_cycle < 0:
                event.add_bank(self.create_bank_SEQN())
                event.add_bank(self.create_bank_SEQV())

            # setup the next cycle
            self.waiting_for_trigger = True
            self.iterate_cycle()
            self.set_ppg_cycle_sequence()

        # the cycle has started
        if self.is_incycle and self.waiting_for_trigger:
            self.waiting_for_trigger = False
            event.add_bank(self.create_bank_SEQC())
            self.was_incycle = True
        
        # return event if filled with a bank
        if len(event.banks.keys()) > 0:
            return event
        
    def reset(self):
        """Reset in preparation for start of run"""
        self.set('CurrentCycle', self.get('StartAtCycleN')-1)
        self.set('CurrentPeriod', 0)
        self.set('CurrentSupercycle', 0)
        self.set('StopAtCycleEnd', False)
        self.set('StopAtSupercycleEnd', False)
        self.was_incycle = False
        self.ncycles_started = 0
        self.client.communicate(10)

    def set(self, name, value):
        """Set an ODB setting"""
        self.client.odb_set(f'/Equipment/{self.NAME}/Settings/{name}', value)

    def get(self, name):
        """get an ODB setting"""
        return self.client.odb_get(f'/Equipment/{self.NAME}/Settings/{name}')

    # these fetches cannot be done through the setting dict since it only 
    # updates every event loop and we need the current version
    @property
    def ncycles(self):              return len(self.get('CyclesEnabled'))
    @property
    def cycles_enabled(self):       return self.get('CyclesEnabled')
    @property
    def current_cycle(self):        return self.get('CurrentCycle')
    @property
    def current_supercycle(self):   return self.get('CurrentSupercycle')
    @property
    def hardware_trigger(self):     return self.get('HardwareTrigger')
    @property
    def nperiods(self):             return len(self.get('PeriodsEnabled'))
    @property
    def is_incycle(self):           return self.ppg.is_running

class SequencerFE(midas.frontend.FrontendBase):
    """
    Sequencer frontend
    """
    def __init__(self):
        midas.frontend.FrontendBase.__init__(self, "fe_ucnsequencer")
        self.add_equipment(UCNSequencer(self.client))

        # Set up the sequence settings so that the sequencer does BOR after the 
        # digitizers and does EOR before the digitizers
        self.client.set_transition_sequence(midas.TR_START, 550)
        self.client.set_transition_sequence(midas.TR_STOP,  450)

        self.client.msg("UCN Sequencer initialized.")

    def begin_of_run(self, run_number):

        seq = self.equipment[UCNSequencer.NAME]

        # reset parameters
        seq.reset()

        # do timing sequence
        self.set_all_equipment_status("Run timing sequence", "greenLight")
        seq.do_timing_sequence()

        # enable
        seq.set('Enabled', True)
        self.set_all_equipment_status("Running", "greenLight")

    def end_of_run(self, run_number):

        seq = self.equipment[UCNSequencer.NAME]

        # disable
        seq.exit()
        self.set_all_equipment_status("Ready", "greenLight")

    def frontend_exit(self):
        self.equipment[UCNSequencer.NAME].exit()
        self.client.msg("UCN Sequencer frontend stopped.")

if __name__ == "__main__":
    with SequencerFE() as fe:
        fe.run()
