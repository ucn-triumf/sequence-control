# UCN Sequencer control 
# Derek Fujimoto
# May 2026

import PPG
import time
import midas
import midas.client

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
"""


class UCNSequencer(object):

    """
    Output channel map

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
    """

    def __init__(self):

        # initialize connection to PPG via VME crate
        self.ppg = PPG.PPG()
        self.enabled = False            # enable flag
        self.external_trigger = True    # use external (hardware) trigger
        self.nperiods = 0               # number of periods in cycle
        self.ncycles = 0                # number cycles in supercycle
        self.durations = [[]]           # [cycle][period] duration in s

    def do_timing_sequence(self):
        """Short sequence of pulses at BOR and EOR for timing calibration
        
        Called at the start and end of every run, this fires 10 short pulses on 
        channel 29 at a 0.2s cadence so that downstream DAQ systems can lock to 
        an absolute time reference.
        """

        print("Setting up timing synchronization sequence");

        # Reset the PPG
        self.ppg.reset()
        self.ppg.halt(0) # probably redundant
        
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
        
        # Wait 3 seconds for the sequence to finish (overkill - should be 2.5s)
        time.sleep(3)

        if self.ppg.is_running:
            raise RuntimeError('PPG still running after timing sequence')

    def set_ppg_sequence(self):
        """Set the ppg sequence with valve states"""

        self.ppg.reset()

        # sequencer disabled... halt and set internal trigger status
        if not self.enabled:
            self.ppg.set_internal_trigger()
            self.ppg.halt(0)
            print('Sequencer disabled')
            return
        
        # Set the first command to 0 (halt program)... this ensure that if the sequence gets immediately triggered when we set to external trigger, then there will be a blank sequence to execute. If we don't do this, then we will restart the old sequence whenever we change parameters.
        self.ppg.halt()

        # Use the external hardware trigger to inititate the sequence
        if self.external_trigger:
            self.ppg.set_external_trigger()

        # track command index
        idx = 0

        # Add blank 100ns at the start of sequence
        self.ppg.hold(idx, mask_high=0x0, mask_low=0xffffffff, delay_ns=100)
        idx += 1

        # Send 250ns pulse to V1725, indicating the start of the cycle
        self.ppg.hold(idx, mask_high=0x80000000, mask_low=0x7fffffff, delay_ns=250)
        idx += 1

        # We do a loop for each period. almost split times into a loop over 100 of DurationTime/100.0 seconds each.
        # This is to get around 32-bit limitation in max limit per command (max of 42s otherwise).
        # Keep track of how many periods and the period times;
        
        valid_periods = 0
        
        for periodi in range(self.nperiods):




        
        # int validPeriods =0;
        # std::string times = std::string("");char stmp[100];

        # for(int i = 0; i < config_global.numberPeriodsInCycle; i++){
            
        #     double dtime = config_global.DurationTimePeriod[i][gCycleIndex];

        #     if(fabs(dtime) < 0.1) continue; // Ignore period of zero duration...

        #     // Figure out which valves are enabled.  For each open valve we set two 
        #     // outlets high.
        #     int enabled_outputs = 0;
        #     if(config_global.Valve1State[i]){enabled_outputs += ((0x3) << 0);}
        #     if(config_global.Valve2State[i]){enabled_outputs += ((0x3) << 2);}
        #     if(config_global.Valve3State[i]){enabled_outputs += ((0x3) << 4);}
        #     if(config_global.Valve4State[i]){enabled_outputs += ((0x3) << 6);}
        #     if(config_global.Valve5State[i]){enabled_outputs += ((0x3) << 8);}
        #     if(config_global.Valve6State[i]){enabled_outputs += ((0x3) << 10);}
        #     if(config_global.Valve7State[i]){enabled_outputs += ((0x3) << 12);}
        #     if(config_global.Valve8State[i]){enabled_outputs += ((0x3) << 14);}

        #     // Add another output signal which indicates which period we are in.
        #     enabled_outputs += ((0x1) << (16+i));

        #     // Now write the actual commands
        #     unsigned int ppg_time = (unsigned int)(dtime*1e8/100.0);
        #     set_command(command_index++,0x0,   0x0, 0x0, 0x200064);
        #     set_command(command_index++,enabled_outputs, ~enabled_outputs,ppg_time,0x100000);
        #     set_command(command_index++,0x0,   0x0, 0x0, 0x300000);

        #     validPeriods++;
        #     sprintf(stmp,"%.1f ",dtime);	    
        #     times += stmp;
        # }

        # // Add a couple extra commands to finish the sequence.  
        # set_command(command_index++,0x0,  0xffffffff,0x1,0x100000);
        # set_command(command_index++,0x0, 0xffffffff,0x1,0x0);

        # // Write a description of the new cycle, but only if it hasn't been written in last five seconds
        # struct timeval now;
        # gettimeofday(&now,NULL);
        
        # if(now.tv_sec - lastPrint.tv_sec > 5){
        #     if(config_global.ExternalTrigger){    
        #     cm_msg(MINFO,"Settings","Setup new cycle: %i periods (%i non-zero): period times = %s sec: cycle/super-cycle index = %i/%i. External hardware trigger.",
        #         config_global.numberPeriodsInCycle,validPeriods,times.c_str(),gCycleIndex,gSuperCycleIndex);    
        #     }else{
        #     cm_msg(MINFO,"Settings","Setup new cycle: %i periods (%i non-zero): period times = %s sec: cycle/super-cycle index = %i/%i. Internal Software trigger.",
        #         config_global.numberPeriodsInCycle,validPeriods,times.c_str(),gCycleIndex,gSuperCycleIndex);    

        #     }
        # }
        # lastPrint = now;


        # printf("set value\n");
        # mvme_write_value(myvme, PPG_BASE+8 , 0x0);
        # printf("returning\n");
        # return 0;
