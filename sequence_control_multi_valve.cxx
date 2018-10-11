
/********************************************************************\

UCN Sequence Control frontend

Sequencing done using TRIUMF Programmable Pulse Generator (PPG) VME module

This is a new version of this program, written for 2018 run, to be able to 
control a larger number of valves in a semi-arbitrary sequence...

Some definitions and conventions:

1) One cycle corresponds to a single irradiation and the subsequent measurements
2) One period is a time within a cycle with valves in a particular state
3) A period has an associate DurationTime and a state for each valve.
4) The DurationTime must be greater than 5 seconds.  Alternately, if it is exactly zero
then that period will be ignored.
5) One super-cycle corresponds to multiple cycles.
6) We can either set the number of super-cycles or just set it to accumulate 
an infinite number of super-cycles
7) From the PPGs point of view, a PPG sequence corresponds to a single cycle.
8) The PPG sequence will start when the irradiation starts.


\********************************************************************/

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <assert.h>
#include "midas.h"
#include "mvmestd.h"
#include <math.h>



extern "C" {

}

/* make frontend functions callable from the C framework */
#ifdef __cplusplus
extern "C" {
#endif

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
   char *frontend_name = "fe2018sequencer";
/* The frontend file name, don't change it */
   char *frontend_file_name = __FILE__;

/* frontend_loop is called periodically if this variable is TRUE    */
   BOOL frontend_call_loop = FALSE;

/* a frontend status page is displayed with this frequency in ms */
   INT display_period = 000;

/* maximum event size produced by this frontend */
   INT max_event_size = 100*1024;

/* maximum event size for fragmented events (EQ_FRAGMENTED) */
   INT max_event_size_frag = 1024*1024;

/* buffer size to hold events */
   INT event_buffer_size = 200*1024;

  extern INT run_state;
  extern HNDLE hDB;

/*-- Function declarations -----------------------------------------*/
  INT frontend_init();
  INT frontend_exit();
  INT begin_of_run(INT run_number, char *error);
  INT end_of_run(INT run_number, char *error);
  INT pause_run(INT run_number, char *error);
  INT resume_run(INT run_number, char *error);
  INT frontend_loop();

  INT read_event(char *pevent, INT off);
/*-- Bank definitions ----------------------------------------------*/

/*-- Equipment list ------------------------------------------------*/

  EQUIPMENT equipment[] = {

    {"UCNSequencer2018",               /* equipment name */
     {1, TRIGGER_ALL,         /* event ID, trigger mask */
      "SYSTEM",               /* event buffer */
      EQ_PERIODIC,              /* equipment type */
      LAM_SOURCE(0, 0xFFFFFF),                      /* event source */
      "MIDAS",                /* format */
      TRUE,                   /* enabled */
      //      RO_RUNNING,             /* read only when running */
      RO_ALWAYS,             /* read only when running */

      100,                    /* poll for 500ms */
      0,                      /* stop run after this event limit */
      0,                      /* number of sub events */
      0,                      /* don't log history */
      "", "", "",}
     ,
     read_event,      /* readout routine */
     NULL, NULL,
     NULL,       /* bank list */
    }
    ,

    {""}
  };

#ifdef __cplusplus
}
#endif
/********************************************************************\
              Callback routines for system transitions

  These routines are called whenever a system transition like start/
  stop of a run occurs. The routines are called on the following
  occations:

  frontend_init:  When the frontend program is started. This routine
                  should initialize the hardware.

  frontend_exit:  When the frontend program is shut down. Can be used
                  to releas any locked resources like memory, commu-
                  nications ports etc.

  begin_of_run:   When a new run is started. Clear scalers, open
                  rungates, etc.

  end_of_run:     Called on a request to stop a run. Can send
                  end-of-run event and close run gates.

  pause_run:      When a run is paused. Should disable trigger events.

  resume_run:     When a run is resumed. Should enable trigger events.
\********************************************************************/

MVME_INTERFACE *myvme = 0;
DWORD PPG_BASE = 0x00c00000;

HNDLE settings_handle_global_;              //!< Handle for the global settings record

extern HNDLE hDB;
HNDLE h;


// Is sequencing enabled
bool gEnabled = true;

// Do we use automatic cycling, instead of using values from ODB?
bool gAutoCycling = false;
// autocycle paramets
int gAutoCycleIndex = 0; // cycle index
const int gMaxAutoCycleIndex = 9; // 
int gAutoCycleDelays[gMaxAutoCycleIndex] = {0, 120, 10, 80, 30, 50, 20, 5, 170};

// This is how long (in sec) we delay opening the UCN valve.
double gDelayTime;
// This is how long (in sec) we leave gate valve open.
double gValveOpenTime;

struct SEQUENCE_SETTINGS2 {
  double delayTime;
  double valveOPenTime;
  bool enable;
  double blank;
  bool autocycling;
} static config_global2;

static const int MaxPeriods = 10;
static const int MaxCycles = 20;

struct SEQUENCE_SETTINGS {
  bool enable;  // enable the sequencing
  int numberPeriodsInCycle; // number of periods in cycle.
  int numberCyclesInSuper; // number of cycles in super-cycle
  int numberSuperCycles; // number of super-cycles to do
  bool infiniteCycles; // Should we just continue super-cycles infinitely?
  //bool Valve1State[MaxPeriods]; // valve 1 state for each period in a cycle.
  double DurationTimePeriod[MaxPeriods][MaxCycles]; // Time in seconds for periods for each cycle.
  int Valve1State[MaxPeriods]; // valve 1 state for each period in a cycle.
  int Valve2State[MaxPeriods];
  int Valve3State[MaxPeriods];
  int Valve4State[MaxPeriods];
  int Valve5State[MaxPeriods];
  int Valve6State[MaxPeriods];
  int Valve7State[MaxPeriods];
  int Valve8State[MaxPeriods];

} static config_global;


// Get the current settings from the ODB
void setVariables(){

  int status=0;
  INT size = sizeof(SEQUENCE_SETTINGS);
  //printf("size %i\n",size);
  //get actual record
  status = db_get_record(hDB, settings_handle_global_, &config_global, &size, 0);
  if (status != DB_SUCCESS){
    cm_msg(MERROR,"SetBoardRecord","Couldn't get record. Return code: %d", status);
    return ;
  }

  // Set the enable state (keep separate copy, so we can disable if desired)
  gEnabled = config_global.enable;
  
  // Check that we don't have too many periods
  if(config_global.numberPeriodsInCycle > MaxPeriods){
    cm_msg(MERROR,"Settings","The requested numberPeriodsInCycle of %i is greater than allowed max (%i); disabling sequencer.\n",config_global.numberPeriodsInCycle,MaxPeriods);
    gEnabled = false;
    return;
  }

  // Check we don't have too many cycles
  if(config_global.numberCyclesInSuper > MaxCycles){
    cm_msg(MERROR,"Settings","The requested numberCyclesInSuper of %i is greater than allowed max (%i); disabling sequencer.\n",config_global.numberCyclesInSuper,MaxCycles);
    gEnabled = false;
    return;
  }

  float beam_on_epics, beam_off_epics; 
  size = sizeof(float);
  status = db_get_value(hDB, 0,"/Equipment/BeamlineEpics/Variables/Measured[30]", &beam_on_epics, &size, TID_FLOAT, FALSE);
  status = db_get_value(hDB, 0,"/Equipment/BeamlineEpics/Variables/Measured[31]", &beam_off_epics, &size, TID_FLOAT, FALSE);
  beam_on_epics *= 0.000888111;
  beam_off_epics *= 0.000888111;
  printf("beam_on, beam_off %f %f\n",beam_on_epics, beam_off_epics);

  // Check all the DurationTimes... should be either >5second or exactly zero.  
  // Also check the total time for each period; should be 10seconds less than the kicker ON/OFF period.
  // Only check for the set of periods and cycles that are being requested.
  for(int j = 0; j < config_global.numberCyclesInSuper; j++){

    double total_time_cycle = 0.0;
   
    for(int i = 0; i < config_global.numberPeriodsInCycle; i++){

      double dtime = config_global.DurationTimePeriod[i][j];
      if(dtime < 0.0 || dtime > 4000){
	cm_msg(MERROR,"Settings","The requested DurationTime of %.2f for Period%i[%i] is not valid; must be in the range [0,4000s]; disabling sequencer.\n",dtime,i+1,j);
	gEnabled = false;
	return;	
      }
      total_time_cycle += dtime;

    }

    if(total_time_cycle != 0)
      printf("Total time for cycle %i is %f\n",j+1,total_time_cycle);

    if(total_time_cycle > beam_on_epics + beam_off_epics - 10){
      cm_msg(MERROR,"Settings","The total time for cycle %i of %.2f seconds is longer than the kicker cycle time of %.2f (with 10sec margin); disabling sequencer.\n",j+1,total_time_cycle,beam_on_epics + beam_off_epics);
      gEnabled = false;
      return;      
    }
  }

  //  cm_msg(MERROR,"Settings","Finished setting and validating the new sequencer settings.\n");

}




// Helper method to write the relevant commands.
void set_command(int i,  unsigned int reg1, unsigned int reg2, unsigned int reg3, unsigned int reg4){

    mvme_write_value(myvme, PPG_BASE+8 , i);
    
    // Write the 128 bits of instruction to 4 registers
    mvme_write_value(myvme, PPG_BASE+0x0c , reg1);
    mvme_write_value(myvme, PPG_BASE+0x10 , reg2);
    mvme_write_value(myvme, PPG_BASE+0x14 , reg3);   
    mvme_write_value(myvme, PPG_BASE+0x18 , reg4);   
    
    printf("PPG command line %i 0x%x 0x%x 0x%x 0x%x\n",i,reg1,reg2,reg3,reg4);

}  

// Here's the plan for how to map the PPG outputs
// ch 1 -> Valve 1 state
// ch 2 -> Valve 1 state monitor
// ch 3 -> Valve 2 state
// ch 4 -> Valve 2 state monitor
// ch 5 -> Valve 3 state
// ch 6 -> Valve 3 state monitor
// ch 7 -> Valve 4 state
// ch 8 -> Valve 4 state monitor
// ch 9 -> Valve 5 state
// ch 10 -> Valve 5 state monitor
// ch 11 -> Valve 6 state
// ch 12 -> Valve 6 state monitor
// ch 13 -> Valve 7 state
// ch 14 -> Valve 7 state monitor
// ch 15 -> Valve 8 state
// ch 16 -> Valve 8 state monitor
INT set_ppg_sequence(){

  // Reset the PPG
  mvme_write_value(myvme, PPG_BASE , 0x8);
  printf("Resetting parameters\n");
  if(!gEnabled){
    mvme_write_value(myvme, PPG_BASE , 0x0);
    set_command(0,0,0,0,0);
    printf("Disabled, nothing to do\n");
    return 0;
  }

  // Set the first command to 0 (halt program)... this ensure that if the sequence gets immediately 
  // triggered when we set to external trigger, then there will be a blank sequence to execute.
  // If we don't do this, then we will restart the old sequence whenever we change parameters.
  set_command(0,0,0,0,0);  
  
  // Use the external trigger to inititate the sequence
  mvme_write_value(myvme, PPG_BASE , 0x4);

  cm_msg(MINFO,"Settings","Setting up new sequence: delayTime=%f, UCN valve open time=%f", gDelayTime,gValveOpenTime);     
  

  // Reminder: these are PPG instructions types;  
  // 0 - Halt
  // 1 - Continue
  // 2 - new Loop        ( 20 bit data used for count - i.e. maximum 1 million )
  // 3 - End Loop
  // 4 - Call Subroutine ( 20 bit data used for address )
  // 5 - Return from subroutine
  // 6 - Branch          ( 20 bit data used for address )


  // ----------------------------------------
  // Start writing the instruction set
  // All commands consist of 128-bits, spread across 4 32-bit words
  
  int command_index = 0;

  // Add blank 100ns at the start of sequence...
  set_command(command_index++,0x0,   0xffffffff, 0x10, 0x100000);

  // We do a loop for each period. almost split times into a loop over 100 of DurationTime/100.0 seconds each.
  // This is to get around 32-bit limitation in max limit per command (max of 42s otherwise)

  for(int i = 0; i < config_global.numberPeriodsInCycle; i++){
    
    int cycle_index = 0;

    double dtime = config_global.DurationTimePeriod[i][cycle_index];

    if(fabs(dtime) < 0.1) continue; // Ignore period of zero duration...

    // figure out which valves are enabled.  For each open valve we set two 
    // outlets high.
    int enabled_outputs = 0;
    if(config_global.Valve1State[i]){enabled_outputs += ((0x3) << 0);}
    if(config_global.Valve2State[i]){enabled_outputs += ((0x3) << 2);}
    if(config_global.Valve3State[i]){enabled_outputs += ((0x3) << 4);}
    if(config_global.Valve4State[i]){enabled_outputs += ((0x3) << 6);}
    if(config_global.Valve5State[i]){enabled_outputs += ((0x3) << 8);}
    if(config_global.Valve6State[i]){enabled_outputs += ((0x3) << 10);}
    if(config_global.Valve7State[i]){enabled_outputs += ((0x3) << 12);}
    if(config_global.Valve8State[i]){enabled_outputs += ((0x3) << 14);}
    unsigned int ppg_time = (unsigned int)(dtime*1e8/100.0);
    set_command(command_index++,0x0,   0x0, 0x0, 0x200064);
    set_command(command_index++,enabled_outputs, ~enabled_outputs,ppg_time,0x100000);
    set_command(command_index++,0x0,   0x0, 0x0, 0x300000);

  }

  // First command; send pulse indicating start.
  //set_command(0,0xc,0xfffffff3,0x1,0x100000);

  // Delay for gDelayTime; split this into a loop over 100 of gDelayTime/100.0 seconds each.
  // This is to get around 32-bit limitation in max limit per command.
  //  unsigned int delay = (unsigned int)(gDelayTime*1e8/100.0);   // 10e8 cycles per second, loop of 100 (0x64) commands.
  //set_command(1,0x0,   0x0, 0x0, 0x200064);
  //set_command(2,0x400, 0xfffffbff,delay,0x100000);
  //set_command(3,0x0,   0x0, 0x0, 0x300000);
  

  // Open valve and wait for specified time; again, set it to 
  // On first clock, send pulse to V1720...
  //unsigned int opentime = (unsigned int)(gValveOpenTime*1e8/100.0); 
  //set_command(4,0x831, 0xfffff7ce,0x1,0x100000);
  //set_command(5,0x0,   0x0, 0x0, 0x200064);
  //set_command(6,0x801, 0xfffff7fe,opentime,0x100000);
  //set_command(7,0x0,   0x0, 0x0, 0x300000);

  // end of valve open time; close valve, send signal; then turn off all outputs.
  //set_command(8,0xc0, 0xffffff3f,0x1,0x100000);
  //set_command(9,0x0,  0xffffffff,0x1,0x100000);
  
  set_command(command_index++,0x0,  0xffffffff,0x1,0x100000);
  set_command(command_index++,0x0, 0xffffffff,0x1,0x0);

  mvme_write_value(myvme, PPG_BASE+8 , 0x0);
  return 0;

}


void callback_func(INT h, INT hseq, void *info)
{
  printf("Callback!\n");
  setVariables();
  set_ppg_sequence();

}

INT frontend_init()
{
  int status=0;

  printf("Initializing\n");
  setbuf(stdout,NULL);
  setbuf(stderr,NULL);

  // Open VME interface   
  status = mvme_open(&myvme, 0);
  
  // Set am to A32 non-privileged Data
  mvme_set_am(myvme, MVME_AM_A32_ND);
  //mvme_set_am(myvme, MVME_AM_A24);
  
  // Set dmode to D32
  mvme_set_dmode(myvme, MVME_DMODE_D32);

  // Setup hotlink
  status = db_find_key (hDB, 0, "/Equipment/UCNSequencer2018/Settings", &settings_handle_global_);
  if (status != DB_SUCCESS) cm_msg(MINFO,"SetBoardRecord","Key not found. Return code: %d",  status);
  
  INT size = sizeof(SEQUENCE_SETTINGS);

  //hotlink
  printf("Setup hotlink\n");
  status = db_open_record(hDB, settings_handle_global_, &config_global, size, MODE_READ, callback_func, NULL);
  printf("Finished setting hotlink: %i %i %i %i %i %i %i %i %i\n",
	 config_global.enable,
	 config_global.infiniteCycles,
	 config_global.numberPeriodsInCycle,
	 config_global.numberCyclesInSuper,
	 config_global.numberSuperCycles,
	 config_global.Valve7State[0],
	 config_global.Valve7State[1],
	 config_global.Valve8State[0],
	 config_global.Valve8State[1]);
  if (status != DB_SUCCESS){
    cm_msg(MERROR,"SetBoardRecord","Couldn't create hotlink . Return code: %d\n", status);
    return status;
  }
  printf("Finished setting hotlink\n");
  // Write to test registers
  if(1){
    printf("Writing to 0x%x\n",PPG_BASE);
    int test0 = mvme_read_value(myvme, PPG_BASE);
    mvme_write_value(myvme, PPG_BASE+4 , 0xdeadbeef);
    int test1 = mvme_read_value(myvme, PPG_BASE+4);
    int test2 = mvme_read_value(myvme, PPG_BASE+0x20);
    int test3 = mvme_read_value(myvme, PPG_BASE+0x18);
    int test4 = mvme_read_value(myvme, PPG_BASE+0x2C);
    printf("Test registers: 0x%x 0x%x 0x%x 0x%x 0x%x \n",test0,test1,test2,test3,test4);
  }

  // Grab values from PDB and update sequence 
  setVariables();
  set_ppg_sequence();

  return SUCCESS;
}

/*-- Frontend Exit -------------------------------------------------*/

INT frontend_exit()
{
  mvme_write_value(myvme, PPG_BASE , 0x8);
  mvme_write_value(myvme, PPG_BASE , 0x0);

  return SUCCESS;
}

/*-- Begin of Run --------------------------------------------------*/

INT begin_of_run(INT run_number, char *error)
{
                                                                         
  if(gAutoCycling){
    cm_msg(MINFO,"BOR","Using the auto-sequencing.");             
    gAutoCycleIndex = 0;
    set_ppg_sequence();
  }

  return SUCCESS;
}

/*-- End of Run ----------------------------------------------------*/
INT end_of_run(INT run_number, char *error)
{

  return SUCCESS;
}

/*-- Pause Run -----------------------------------------------------*/
INT pause_run(INT run_number, char *error)
{
  return SUCCESS;
}

/*-- Resume Run ----------------------------------------------------*/
INT resume_run(INT run_number, char *error)
{
  return SUCCESS;
}

/*-- Frontend Loop -------------------------------------------------*/
INT frontend_loop()
{
  /* if frontend_call_loop is true, this routine gets called when
     the frontend is idle or once between every event */
  return SUCCESS;
}

/********************************************************************\

  Readout routines for different events

\********************************************************************/

/*-- Trigger event routines ----------------------------------------*/
extern "C" INT poll_event(INT source, INT count, BOOL test)
/* Polling routine for events. Returns TRUE if event
   is available. If test equals TRUE, don't return. The test
   flag is used to time the polling */
{
  //printf("poll_event %d %d %d!\n",source,count,test);

  for (int i=0 ; i<count ; i++)
  {
    int lam = 0;//v792_DataReady(gVme,gAdcBase);
    //printf("source: 0x%x, lam: 0x%x\n", source, lam);

    if (lam)
      if (!test)
        return TRUE;
  }

  return FALSE;


}

/*-- Interrupt configuration ---------------------------------------*/
extern "C" INT interrupt_configure(INT cmd, INT source, PTYPE adr)
{
   switch (cmd) {
   case CMD_INTERRUPT_ENABLE:
     break;
   case CMD_INTERRUPT_DISABLE:
     break;
   case CMD_INTERRUPT_ATTACH:
     break;
   case CMD_INTERRUPT_DETACH:
     break;
   }
   return SUCCESS;
}

/*-- Event readout -------------------------------------------------*/
// Bank format
// word 0 -> millisecond portion of current time
// word 1 -> bit 0: are we in sequence? bit 1: did sequence just start?
// word 2 -> is sequencer enabled?
// word 3 -> delaytime (in ms)
// word 4 -> opentime (in ms)
int previous_reg0_bit0 = 1;
int first_event = 1;
struct timeval cycle_start_time;
INT read_event(char *pevent, INT off)
{

  //printf("read event\n");
  /* init bank structure */
  bk_init32(pevent);

  uint32_t *pdata32;

  /* create structured ADC0 bank of double words (i.e. 4-byte words)  */
  bk_create(pevent, "SEQN", TID_DWORD, (void **)&pdata32);

  // Get sub-second time;
  struct timeval now;
  gettimeofday(&now,NULL);
  *pdata32++ = now.tv_usec/1000;
  
  
  // Save sequence status in word1.
  int reg0 = mvme_read_value(myvme, PPG_BASE);
  //printf("reg0 %i\n",reg0);
  int word1 = 0;
  int sequence_finished = 0;
  // Check whether we are in sequence
  if(reg0 & 1){
    word1 |= (1<<0);
    // if last time we were not in sequence, then set bit that transition happened...
    if(!previous_reg0_bit0){
      word1 |= (1<<1);
      printf("Started cycle \n");
      cycle_start_time = now;
    }
    
  }else{ // if we were in sequence last time and aren't now, then sequence is finished.
    if(previous_reg0_bit0){
      if(first_event){
	first_event = 0;
      }else{
	sequence_finished = 1;
	double diff = now.tv_sec-cycle_start_time.tv_sec + ((float)(now.tv_usec-cycle_start_time.tv_usec))/1000000.0;
	printf("Finished cycle; cycle time was %.3f sec \n",diff);

      }
    }
  }
  *pdata32++ = word1;
  previous_reg0_bit0 = (reg0 & 1);

  // save the enable, delayTime, opentim
  *pdata32++ = (int)gEnabled;
  *pdata32++ = (int)(gDelayTime*1000.0);
  *pdata32++ = (int)(gValveOpenTime*1000.0);

  int size2 = bk_close(pevent, pdata32);    

  // If a sequence finished, then update the index if auto-cycling.
  //printf("sequence_finished %i %i\n",sequence_finished, (reg0 & 1));
  if(sequence_finished){
    if(gAutoCycling){
      cm_msg(MINFO,"SetBoardRecord","Finished sequence (index=%i)",gAutoCycleIndex);   
      gAutoCycleIndex++; 
      if(gAutoCycleIndex >= gMaxAutoCycleIndex){
	gAutoCycleIndex = 0;
      }      
      set_ppg_sequence();
    }
  }

  return bk_size(pevent);
}

