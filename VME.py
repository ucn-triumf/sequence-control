# Talk to the VME crate
# Derek Fujimoto
# May 2026

import midas
import ctypes
import os

# interface struct wrapper
class mvme_interface(ctypes.Structure):
    _fields_ = [('initialized', ctypes.c_int),  # 1 if VME interface initialized
                ('handle', ctypes.c_int),       # internal handle
                ('index', ctypes.c_int),        # index of interface 0..n
                ('info', ctypes.c_void_p),      # internal info structure
                ('am', ctypes.c_int),           # Address modifier
                ('dmode', ctypes.c_int),        # Data mode (D8,D16,D32,D64)
                ('blt_mode',ctypes.c_int ),     # Block transfer mode
                ('table', ctypes.c_void_p),     # Optional table for some drivers
                ]

class VME(object):
    """
    VME crate to read/write
    """

    def __init__(self, ppg_base=0x00c00000):
        
        # load library
        try:
            path = os.path.abspath(__file__)
        except NameError:
            path = '.'
        
        path = os.path.basename(path)
        self.lib = ctypes.CDLL(os.path.join(path, 'build', 'libvme.so'))

        # set inputs
        self.ppg_base = ppg_base
        self.myvme = mvme_interface()

    def open(self):
        """Open connection - kills python on fail with msg to stdout"""
        return self.lib.mvme_open(ctypes.pointer(self.myvme), 0)

    def close(self):
        return self.lib.mvme_close(ctypes.pointer(self.myvme), 0)

    def set_address_mod(self, am:int):
        """Set address modifier"""
        return self.lib.mvme_set_am(ctypes.pointer(self.myvme), am)

    def set_data_mode(self, dmode:int):
        """Set data mode"""
        return self.lib.mvme_set_am(ctypes.pointer(self.myvme), dmode)
    
    def get_address_mod(self):
        """Get address modifier"""
        x = ctypes.c_int()
        self.lib.mvme_set_am(ctypes.pointer(self.myvme),
                             ctypes.pointer(x))
        return int(x)
    
    def get_data_mode(self):
        """Get data mode"""
        x = ctypes.c_int()
        self.lib.mvme_set_am(ctypes.pointer(self.myvme),
                             ctypes.pointer(x))
        return int(x)

    def read_value(self, address:int):
        """Read single value from VME bus
        
        Args:
            address (unsigned int)
            value (unsigned int)
        """
        return int(self.lib.mvme_read_value(ctypes.pointer(self.myvme), 
                                            ctypes.c_uint(address)))

    def write_value(self, address:int, value:int):
        """Write single value to VME bus
        
        Args:
            address (unsigned int)
            value (unsigned int)
        """
        return int(self.lib.mvme_read_value(ctypes.pointer(self.myvme), 
                                            ctypes.c_uint(address),
                                            ctypes.c_uint(value)))
