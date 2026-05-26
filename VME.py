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


## constants from mvmestd.h ========================================

# ---- status codes ------------------------------------------------

MVME_SUCCESS        = 1                              
MVME_NO_INTERFACE   = 2                        
MVME_NO_CRATE       = 3                            
MVME_UNSUPPORTED    = 4                          
MVME_INVALID_PARAM  = 5                      
MVME_NO_MEM         = 6                            
MVME_ACCESS_ERROR   = 7                        

# ---- constants ---------------------------------------------------

# data modes

MVME_DMODE_D8       =   1   # D8
MVME_DMODE_D16      =   2   # D16
MVME_DMODE_D32      =   3   # D32
MVME_DMODE_D64      =   4   # D64
MVME_DMODE_RAMD16   =   5   # RAM memory of VME adapter
MVME_DMODE_RAMD32   =   6   # RAM memory of VME adapter
MVME_DMODE_LM       =   7   # local memory mapped to VME 

MVME_DMODE_DEFAULT  = MVME_DMODE_D32

# block transfer modes

MVME_BLT_NONE       = 1 # normal programmed IO
MVME_BLT_BLT32      = 2 # 32-bit block transfer
MVME_BLT_MBLT64     = 3 # multiplexed 64-bit block transfer
MVME_BLT_2EVME      = 4 # two edge block transfer
MVME_BLT_2ESST      = 5 # two edge source synchrnous transfer
MVME_BLT_BLT32FIFO  = 6 # FIFO mode, don't increment address
MVME_BLT_MBLT64FIFO = 7 # FIFO mode, don't increment address
MVME_BLT_2EVMEFIFO  = 8 # two edge block transfer with FIFO mode

# vme bus address modifiers

MVME_AM_A32_SB      = (0x0F) # A32 Extended Supervisory Block
MVME_AM_A32_SP      = (0x0E) # A32 Extended Supervisory Program
MVME_AM_A32_SD      = (0x0D) # A32 Extended Supervisory Data
MVME_AM_A32_NB      = (0x0B) # A32 Extended Non-Privileged Block
MVME_AM_A32_NP      = (0x0A) # A32 Extended Non-Privileged Program
MVME_AM_A32_ND      = (0x09) # A32 Extended Non-Privileged Data
MVME_AM_A32_SMBLT   = (0x0C) # A32 Multiplexed Block Transfer (D64)
MVME_AM_A32_NMBLT   = (0x08) # A32 Multiplexed Block Transfer (D64)

MVME_AM_A32         = MVME_AM_A32_SD
MVME_AM_A32_D64     = MVME_AM_A32_SMBLT

MVME_AM_A24_SB      = (0x3F) # A24 Standard Supervisory Block Transfer
MVME_AM_A24_SP      = (0x3E) # A24 Standard Supervisory Program Access
MVME_AM_A24_SD      = (0x3D) # A24 Standard Supervisory Data Access
MVME_AM_A24_NB      = (0x3B) # A24 Standard Non-Privileged Block Transfer
MVME_AM_A24_NP      = (0x3A) # A24 Standard Non-Privileged Program Access
MVME_AM_A24_ND      = (0x39) # A24 Standard Non-Privileged Data Access
MVME_AM_A24_SMBLT   = (0x3C) # A24 Multiplexed Block Transfer (D64)
MVME_AM_A24_NMBLT   = (0x38) # A24 Multiplexed Block Transfer (D64)

MVME_AM_A24         = MVME_AM_A24_SD
MVME_AM_A24_D64     = MVME_AM_A24_SMBLT

MVME_AM_A16_SD      = (0x2D) # A16 Short Supervisory Data Access
MVME_AM_A16_ND      = (0x29) # A16 Short Non-Privileged Data Access

MVME_AM_A16         = MVME_AM_A16_SD

MVME_AM_DEFAULT     = MVME_AM_A32

# interface with crate
class VME(object):
    """
    VME crate to read/write
    """

    def __init__(self):
        
        # load library
        try:
            path = os.path.abspath(__file__)
        except NameError:
            path = '.'
        
        path = os.path.basename(path)
        self.lib = ctypes.CDLL(os.path.join(path, 'build', 'libvme.so'))

        # set inputs
        self.myvme = mvme_interface()

    def open(self):
        """Open connection - kills python on fail with msg to stdout"""
        self.lib.mvme_open(ctypes.pointer(self.myvme), 0)

    def set_address_mod(self, am:int):
        """Set address modifier"""
        self.lib.mvme_set_am(ctypes.pointer(self.myvme), 
                             ctypes.c_int(am))

    def set_data_mode(self, dmode:int):
        """Set data mode"""
        self.lib.mvme_set_dmode(ctypes.pointer(self.myvme), 
                                ctypes.c_int(dmode))
    
    def get_address_mod(self):
        """Get address modifier"""
        x = ctypes.c_int()
        self.lib.mvme_get_am(ctypes.pointer(self.myvme),
                             ctypes.pointer(x))
        return x.value
    
    def get_data_mode(self):
        """Get data mode"""
        x = ctypes.c_int()
        self.lib.mvme_get_dmode(ctypes.pointer(self.myvme),
                                ctypes.pointer(x))
        return x.value

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
