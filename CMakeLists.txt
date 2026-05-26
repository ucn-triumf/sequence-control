cmake_minimum_required(VERSION 3.15)
project(vme C CXX)

# ── Resolve environment variables ─────────────────────────────────────────────
# CMake reads them from the shell environment with $ENV{}.    
set(MIDASSYS $ENV{MIDASSYS})
set(INC_DIR ${MIDASSYS}/include)   # MIDAS headers
set(DRV_DIR ${MIDASSYS}/drivers)   # MIDAS driver headers
set(LIB_DIR ${MIDASSYS}/lib)       # MIDAS pre-built libs / objects

# ── Imported targets for pre-built MIDAS components ───────────────────────────
# An IMPORTED STATIC library tells CMake "this .a file already exists on disk;
# I did not build it, just use it when linking."
add_library(midas STATIC IMPORTED)
set_target_properties(midas PROPERTIES
    IMPORTED_LOCATION ${LIB_DIR}/libmidas.a
)

# ── Shared library for ctypes ─────────────────────────────────────────────────
# SHARED produces a .so on Linux. ctypes.CDLL("./libvme.so") can then call
# any function from gefvme.c (mvme_open, mvme_set_am, mvme_set_dmode,
# mvme_read_value, mvme_write_value, etc.) directly — they are plain C and
# already have C linkage with no wrapper needed.
#
# mfe.o is intentionally omitted: it contains MIDAS's main() and frontend
# event loop. A shared library has no entry point, so mfe.o would cause
# duplicate-symbol errors. libmidas.a still provides the full MIDAS API.
add_library(vme SHARED
    ${MIDASSYS}/drivers/vme/vmic/gefvme.c
)
target_compile_options(vme PRIVATE -O2 -fPIC -g -Wall -Wuninitialized)
target_include_directories(vme PRIVATE ${INC_DIR})
target_link_libraries(vme PRIVATE
    midas
    -lm -lz -lutil -lnsl -lpthread -lrt
)
