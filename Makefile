# Makefile
# $Id$
#MIDASSYS=/home/olchansk/daq/midas/midas

OSFLAGS  =  -DOS_LINUX -Dextname
CFLAGS   =  -g -O2 -Wall -Wuninitialized -I$(INC_DIR) -I$(DRV_DIR) -I$(VMICHOME)/include
CXXFLAGS = $(CFLAGS) -DHAVE_ROOT -DUSE_ROOT -I$(ROOTSYS)/include

LIBS = -lm -lz -lutil -lnsl -lpthread -lrt -L/home/lindner/packages/vmisft-7433-3.6-KO5/vme_universe 

DRV_DIR         = $(MIDASSYS)/drivers
INC_DIR         = $(MIDASSYS)/include
LIB_DIR         = $(MIDASSYS)/lib

# MIDAS library
MIDASLIBS = $(LIB_DIR)/libmidas.a

# ROOT library
ROOTLIBS = $(shell $(ROOTSYS)/bin/root-config --libs) -lThread -Wl,-rpath,$(ROOTSYS)/lib

all: sequence_control_multi_valve.exe 

gefvme.o: $(MIDASSYS)/drivers/vme/vmic/gefvme.c
	g++ -c -o $@ -O2 -g -Wall -Wuninitialized $< -I$(MIDASSYS)/include

vmicvme.o: $(MIDASSYS)/drivers/vme/vmic/vmicvme.c
	g++ -c -o $@ -O2 -g -Wall -Wuninitialized $(CFLAGS) $<

%.o: $(MIDASSYS)/drivers/vme/%.c
	g++ -c -o $@ -O2 -g -Wall -Wuninitialized $< -I$(MIDASSYS)/include

#sequence_control.exe: $(MIDASLIBS) $(LIB_DIR)/mfe.o sequence_control.o vmicvme.o 
#	$(CXX) -o $@ $(CFLAGS) $(OSFLAGS) $^ $(MIDASLIBS) $(LIBS) #-lvme

sequence_control.exe: $(MIDASLIBS) $(LIB_DIR)/mfe.o sequence_control.o gefvme.o 
	$(CXX) -o $@ $(CFLAGS) $(OSFLAGS) $^ $(MIDASLIBS) $(LIBS) #-lvme

sequence_control_multi_valve.exe: $(MIDASLIBS) $(LIB_DIR)/mfe.o sequence_control_multi_valve.o gefvme.o 
	$(CXX) -o $@ $(CFLAGS) $(OSFLAGS) $^ $(MIDASLIBS) $(LIBS) #-lvme


%.o: %.c
	$(CXX) $(CFLAGS) $(OSFLAGS) -c $<

%.o: %.cxx
	$(CXX) $(CXXFLAGS) $(OSFLAGS) -c $<

clean::
	-rm -f *.o *.exe

# end
