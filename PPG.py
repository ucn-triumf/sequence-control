# PPG interface for UCN sequencer
# Derek Fujimoto
# May 2026

import VME
import time

# https://daq00.triumf.ca/DaqWiki/index.php/VME-PPG32

class PPG(object):

    """
        The PPG32 is a VME FPGA board with a **100 MHz internal clock** (10 ns per tick) and **32 NIM outputs**. Its core is a small program memory that holds up to 4096 instructions. Each instruction is **128 bits wide**, written to the board four 32-bit registers at a time. The program executes sequentially, driving the NIM output lines high/low according to each instruction's output mask.
    """

    BASE_ADDR = 0x00c00000

    def __init__(self):

        # initialize connection to vme crate
        self.vme = VME.VME()
        self.vme.open()

        # Set am to A32 non-privileged Data
        self.vme.set_address_mod(VME.MVME_AM_A32_ND)
        assert self.vme.get_address_mod() == VME.MVME_AM_A32_ND

        # Set dmode to D32
        self.vme.set_data_mode(VME.MVME_DMODE_D32)
        assert self.vme.get_data_mode() == VME.MVME_DMODE_D32

    def _set_command(self, idx:int, mask_high=None, mask_low=None, delay_10ns=None, instr=None):
        """Every instruction encodes four things, spread across four 32-bit registers
        
        Args:
            idx (int):          instruction slot index (address)
            mask_high (int):     bits 0-31, mask indicating which output channels to drive high 
            mask_low (int):   bits 32-63, mask indicating which output channels to drive low 
            delay_10ns (int):   bits 64-95, number of extra 10ns clcok cycles to hold this state
            instr (int):        bits 96-127, instruction opcode and loop count or branch address
                                opcode: bits 20-21
                                payload: bits 0-19
     
        PPG instructions types (opcode):  
            0 - Halt
            1 - Continue
            2 - new Loop        ( 20 bit payload used for count - i.e. maximum 1 million )
            3 - End Loop
            4 - Call Subroutine ( 20 bit payload used for address )
            5 - Return from subroutine
            6 - Branch          ( 20 bit payload used for address )        

        Timing note: 
            The actual dwell time for any instruction is (3 + delay) × 10 ns. The hardware always spends 3 clock cycles on instruction overhead before adding the programmed delay.
        """

        # address: pointer to which instruction slot to write next
        self.vme.write_value(self.BASE_ADDR+8 , idx)
            
        # Write the 128 bits of instruction to 4 registers
        if mask_high is not None:   self.vme.write_value(self.BASE_ADDR+0x0c , mask_high)
        if mask_low is not None:    self.vme.write_value(self.BASE_ADDR+0x10 , mask_low)
        if delay_10ns is not None:  self.vme.write_value(self.BASE_ADDR+0x14 , delay_10ns)   
        if instr is not None:       self.vme.write_value(self.BASE_ADDR+0x18 , instr)

    def start(self):
        """Immediately start execution from instruction 0"""

        # set address pointer to 0 (is this needed?)
        self.vme.write_value(self.BASE_ADDR+8, 0x0)

        # start execution
        self.vme.write_value(self.BASE_ADDR, 0x1)

    def reset(self):
        """Clears the program counter and halts execution"""
        self.vme.write_value(self.BASE_ADDR, 0x8)

    def halt(self, idx:int):
        """Stop execution and set all inputs to low
        
        Args:
            idx (int):          instruction slot index (address)
        """
        self._set_command(idx, 0x0, 0xffffffff, 0, 0x0)

    def hold(self, idx:int, mask_high:int, mask_low:int, delay_ns:int):
        """Set mask then proceed to next instruction after time delay
        
        Args:
            idx (int):          instruction slot index (address)
            mask_high (int):    bits 0-31, mask indicating which output channels to drive high 
            mask_low (int):     bits 32-63, mask indicating which output channels to drive low 
            delay_ns (int):     bits 64-95, hold this state for duration in ns. 
                                Must be an even multiple of 10 ns
        """

        if delay_ns % 10 != 0:
            raise RuntimeError("delay_ns must be an even multiple of 10 ns")

        self._set_command(idx, mask_high, mask_low, delay_ns//10, 0x100000)

    def mark_loop_start(self, idx:int, nloops:int):
        """Mark the start of a block of code that should be looped over
        
        Args:
            idx (int):      instruction slot index (address)
            nloops (int):   number of loops
        """
        if nloops > 0xfffff:
            raise RuntimeError(f"Number of loops too large ({hex(nloops)} > 0xfffff)")
        self._set_command(idx, 0x0, 0x0, 0x0, 0x200000+nloops)

    def mark_loop_end(self, idx:int):
        """Mark the end of a block of code that should be looped over
        
        Args:
            idx (int): instruction slot index (address)
        """
        self._set_command(idx, 0x0, 0x0, 0x0, 0x300000)

    def subroutine_call(self, idx:int, addr:int):
        """Call subroutine from 20 bit address
        
        Args:
            idx (int):  instruction slot index (address)
            addr (int): 20 bit address
        """
        if addr > 0xfffff:
            raise RuntimeError(f"Address too large ({hex(addr)} > 0xfffff)")
        self._set_command(idx, 0x0, 0x0, 0x0, 0x400000+addr)

    def subroutine_return(self, idx:int):
        """Return from subroutine

        Args:
            idx (int): instruction slot index (address)
        """
        self._set_command(idx, 0x0, 0x0, 0x0, 0x500000)

    def branch(self, idx:int, addr:int):
        """branch to 20 bit address
        
        Args:
            idx (int):  instruction slot index (address)
            addr (int): 20 bit address
        """
        if addr > 0xfffff:
            raise RuntimeError(f"Address too large ({hex(addr)} > 0xfffff)")
        self._set_command(idx, 0x0, 0x0, 0x0, 0x600000+addr)

    def set_internal_trigger(self):
        """Set internal (software) trigger mode"""
        self.vme.write_value(self.BASE_ADDR, 0x0)

    def set_external_trigger(self):
        """Set external (hardware) trigger mode"""
        self.vme.write_value(self.BASE_ADDR, 0x4)

    @property
    def is_running(self):
        """Get status bit as a boolean
    
        0   out of sequence
        1   in sequence
        """
        return bool(self.vme.read_value(self.BASE_ADDR) & 1)

    def test(self):
        """Run some test commands"""

        # write to test registers
        print(f"Writing to {self.BASE_ADDR}")
        test0 = self.vme.read_value(self.BASE_ADDR)
        self.vme.write_value(self.BASE_ADDR+4 , 0xbeefbeef)
        test1 = self.vme.read_value(self.BASE_ADDR+4)
        test2 = self.vme.read_value(self.BASE_ADDR+0x20)
        test3 = self.vme.read_value(self.BASE_ADDR+0x18)
        test4 = self.vme.read_value(self.BASE_ADDR+0x2C)
        print(f"Test registers: {test0}, {test1}, {test2}, {test3}, {test4}")


class PPG_Mock(object):

    """
        Test ppg object with false output. Doesn't actually control any board.
    """

    BASE_ADDR = 0x00c00000
    DEBUG_MSG = False

    def __init__(self):
        self.print('PPG init')
        self.t0 = 0
        self.runtime = 0
        self.nloops = 1

    def print(self, *args, **kwargs):
        if self.DEBUG_MSG:
            print(*args, **kwargs)

    def _set_command(self, idx:int, mask_high=None, mask_low=None, delay_10ns=None, instr=None):
        self.print(f'PPG._set_command(idx={idx}, mask_high={mask_high}, mask_low={mask_low}, delay_10ns={delay_10ns}, instr={instr})')

    def start(self):
        self.print('PPG.start()')
        self.t0 = time.monotonic()

    def reset(self):
        self.print('PPG.reset()')
        self.t0 = 0
        self.runtime = 0

    def halt(self, idx:int):
        self.print(f'PPG.halt(idx={idx})')
        self.t0 = 0

    def hold(self, idx:int, mask_high:int, mask_low:int, delay_ns:int):
        self.print(f'PPG.hold(idx={idx}, mask_high={mask_high}, mask_low={mask_low}, delay_ns={delay_ns})')
        self.runtime += delay_ns * 1e-9 * self.nloops

    def mark_loop_start(self, idx:int, nloops:int):
        self.print(f'PPG.mark_loop_start(idx={idx}, nloops={nloops})')
        self.nloops = nloops

    def mark_loop_end(self, idx:int):
        self.print(f'PPG.mark_loop_end(idx={idx})')
        self.nloops = 1

    def subroutine_call(self, idx:int, addr:int):
        self.print(f'PPG.subroutine_call(idx={idx}, addr={addr})')

    def subroutine_return(self, idx:int):
        self.print(f'PPG.subroutine_return(idx={idx})')

    def branch(self, idx:int, addr:int):
        self.print(f'PPG.branch(idx={idx}, addr={addr})')

    def set_internal_trigger(self):
        self.print(f'PPG.set_internal_trigger()')

    def set_external_trigger(self):
        self.print(f'PPG.set_external_trigger()')
        time.sleep(2) # "trigger" after 2 sec
        self.t0 = time.monotonic() 

    @property
    def is_running(self):
        return time.monotonic() - self.t0 < self.runtime