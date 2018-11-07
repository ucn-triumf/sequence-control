
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
      RO_ALWAYS|RO_ODB,             /* read only when running */

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

// index of which cycle we are in supercycle
int gCycleIndex = 0;

// index of which supercycle we are in
int gSuperCycleIndex = 0;

// Maximum number of periods per cycle
static const int MaxPeriods = 10;
// Maximum number of cycles per super-cycle
static const int MaxCycles = 20;

// On first event make sure to write NSEQ bank with current configuration.
bool gFirstEvent = 1;

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

  // We check the enable status bit first.  If the sequencer ODB variabl is disabled then we don't want to 
  // update the whole config_global variable.  This will ensure that the local copy of config_global
  // remains unchanged while editting of the ODB variables is on-going.
  int status=0;
  bool sequencer_disabled;
  INT size = sizeof(float);
  status = db_get_value(hDB, 0,"/Equipment/UCNSequencer2018/Settings/enable", &sequencer_disabled, &size, TID_BOOL, FALSE);
  if (status != DB_SUCCESS){
    cm_msg(MERROR,"setVariables","Couldn't get the sequencer enable status.  Return code: %d", status);
    return ;
  }


  // Set the enable state (keep separate copy, so we can disable if desired)
  gEnabled = sequencer_disabled;

  // if the sequencer isn't enabled then don't bother checking the rest of parameters.
  if(!gEnabled){
    printf("Settings... %i %i %i\n",config_global.numberPeriodsInCycle,
	   config_global.DurationTimePeriod[0][1], config_global.DurationTimePeriod[0][0]);
    return;
  }

  // Now grab the full copy of the ODB message...
  size = sizeof(SEQUENCE_SETTINGS);
  status = db_get_record(hDB, settings_handle_global_, &config_global, &size, 0);
  if (status != DB_SUCCESS){
    cm_msg(MERROR,"SetBoardRecord","Couldn't get record. Return code: %d", status);
    return ;
  }


  if(sequencer_disabled != config_global.enable){
    cm_msg(MERROR,"SetBoardRecord","Warning, two reads of enable bit do not match: %i %i\n",
	   sequencer_disabled,config_global.enable);
  }


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

  // Reset the cycle index now that we have validated new paramters
  gCycleIndex = 0;
  gSuperCycleIndex = 0;

  //cm_msg(MINFO,"Settings","Finished setting and validating the new sequencer settings.\n");

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

struct timeval lastPrint;

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
// ch 29 -> Signal to V1720 indicating start of period 2.
// ch 30 -> Signal to V1720 indicating start of period 1.
// ch 31 -> Signal to V1720 indicating start of cycle (period 0).
INT set_ppg_sequence(){

  // Reset the PPG
  mvme_write_value(myvme, PPG_BASE , 0x8);
  if(!gEnabled){
    mvme_write_value(myvme, PPG_BASE , 0x0);
    set_command(0,0,0,0,0);
    printf("Sequencer disabled, nothing to do\n");
    return 0;
  }

  // Set the first command to 0 (halt program)... this ensure that if the sequence gets immediately 
  // triggered when we set to external trigger, then there will be a blank sequence to execute.
  // If we don't do this, then we will restart the old sequence whenever we change parameters.
  set_command(0,0,0,0,0);  
  
  // Use the external trigger to inititate the sequence
  mvme_write_value(myvme, PPG_BASE , 0x4);


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
  // Then send pulse to V1720, indicating the start of the cycle
  set_command(command_index++,0x80000000,   0x7fffffff, 0x1, 0x100000);

  // We do a loop for each period. almost split times into a loop over 100 of DurationTime/100.0 seconds each.
  // This is to get around 32-bit limitation in max limit per command (max of 42s otherwise).
  // Keep track of how many periods and the period times;
  int validPeriods =0;
  std::string times = std::string("");char stmp[100];

  for(int i = 0; i < config_global.numberPeriodsInCycle; i++){
    
    double dtime = config_global.DurationTimePeriod[i][gCycleIndex];

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

    // Then send pulse to V1720, indicating the end of period
    if(i == 0) set_command(command_index++,0x40000000,   0xbfffffff, 0x1, 0x100000);
    if(i == 1) set_command(command_index++,0x20000000,   0xdfffffff, 0x1, 0x100000);


    validPeriods++;
    sprintf(stmp,"%.1f ",dtime);	    
    times += stmp;
  }

  // Add a couple extra commands to finish the sequence.  
  set_command(command_index++,0x0,  0xffffffff,0x1,0x100000);
  set_command(command_index++,0x0, 0xffffffff,0x1,0x0);

  // Write a description of the new cycle, but only if it hasn't been written in last five seconds
  struct timeval now;
  gettimeofday(&now,NULL);
  
  if(now.tv_sec - lastPrint.tv_sec > 5){
    cm_msg(MINFO,"Settings","Setup new cycle: %i periods (%i non-zero): period times = %s sec: cycle/super-cycle index = %i/%i.",
	   config_global.numberPeriodsInCycle,validPeriods,times.c_str(),gCycleIndex,gSuperCycleIndex);     
  }
  lastPrint = now;


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

  printf("Initializing UCNSequencer \n");
  setbuf(stdout,NULL);
  setbuf(stderr,NULL);

  // Open VME interface   
  status = mvme_open(&myvme, 0);
  
  // Set am to A32 non-privileged Data
  mvme_set_am(myvme, MVME_AM_A32_ND);
  
  // Set dmode to D32
  mvme_set_dmode(myvme, MVME_DMODE_D32);

  // Setup hotlink
  status = db_find_key (hDB, 0, "/Equipment/UCNSequencer2018/Settings", &settings_handle_global_);
  if (status != DB_SUCCESS) cm_msg(MINFO,"SetBoardRecord","Key not found. Return code: %d",  status);
  
  INT size = sizeof(SEQUENCE_SETTINGS);

  //setup hotlink to settings directory
  status = db_open_record(hDB, settings_handle_global_, &config_global, size, MODE_READ, callback_func, NULL);
  if (status != DB_SUCCESS){
    cm_msg(MERROR,"SetBoardRecord","Couldn't create hotlink . Return code: %d\n", status);
    return status;
  }

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


  
  cm_msg(MINFO,"BOR","Using the auto-sequencing.");             
  gCycleIndex = 0;
  gSuperCycleIndex = 0;
  set_ppg_sequence();
  gFirstEvent = 1;

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
// Bank format for USEQ
// word 0 -> second portion of current time
// word 1 -> millisecond portion of current time
// word 2 -> second portion of cycle start time
// word 3 -> millisecond portion of cycle start time
// word 4 -> bit 0: are we in cycle? bit 1: did cycle just start?
//           bit 2: did cycle just end?
// word 5 -> is sequencer enabled?
// word 6 -> cycle index
// word 7 -> super-cycle index
// 
// NSEQ is a full copy of the sequencer settings currently in the program.
int previous_reg0_bit0 = 1;
int first_event = 1;
struct timeval cycle_start_time;
INT read_event(char *pevent, INT off)
{

  //printf("read event\n");
  /* init bank structure */
  bk_init32(pevent);

  uint32_t *pdata32;
  double *pdata;
  
  /* create structured ADC0 bank of double words (i.e. 4-byte words)  */
  bk_create(pevent, "USEQ", TID_DWORD, (void **)&pdata32);

  // Get sub-second time;
  struct timeval now;
  gettimeofday(&now,NULL);
  *pdata32++ = now.tv_sec;
  *pdata32++ = now.tv_usec/1000;
  *pdata32++ = cycle_start_time.tv_sec;
  *pdata32++ = cycle_start_time.tv_usec/1000;
  
  
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
      cm_msg(MINFO,"read_event","Started cycle.");
      cycle_start_time = now;
    }
    
  }else{ // if we were in sequence last time and aren't now, then sequence is finished.
    if(previous_reg0_bit0){
      if(first_event){
	first_event = 0;
      }else{
      word1 |= (1<<2);
	sequence_finished = 1;
	double diff = now.tv_sec-cycle_start_time.tv_sec + ((float)(now.tv_usec-cycle_start_time.tv_usec))/1000000.0;
	printf("Finished cycle; cycle time was %.3f sec \n",diff);

      }
    }
  }
  *pdata32++ = word1;
  previous_reg0_bit0 = (reg0 & 1);
  *pdata32++ = (int)gEnabled;
  *pdata32++ = gCycleIndex;
  *pdata32++ = gSuperCycleIndex;


  int size2 = bk_close(pevent, pdata32);    

  // If a sequence finished, then update the index if auto-cycling.
  if(sequence_finished){
    printf("Finished sequence (index=%i) %i",gCycleIndex, config_global.numberCyclesInSuper);
    gCycleIndex++; 
    if(gCycleIndex >= config_global.numberCyclesInSuper){
      gCycleIndex = 0;
      gSuperCycleIndex++;
    }      
    set_ppg_sequence();  
  }
  
  // If last sequence finished or this is the first event in the run, then 
  // write a bank in which we store the complete set of settings for the PPG                                                                                        
  // include the current cycle index, so we now where we are...     
  if(sequence_finished || gFirstEvent){
    printf("Writing NSEQ bank\n");
    bk_create(pevent, "NSEQ", TID_DOUBLE, (void **)&pdata);
    *pdata++ = gCycleIndex;
    *pdata++ = config_global.enable;
    *pdata++ = config_global.numberPeriodsInCycle;
    *pdata++ = config_global.numberCyclesInSuper;
    *pdata++ = config_global.infiniteCycles;
    for(int i = 0; i < MaxPeriods; i++){
      for(int j = 0; j < MaxCycles; j++){
	*pdata++ = config_global.DurationTimePeriod[i][j];
      }
    }
    for(int i = 0; i < MaxPeriods; i++){
      *pdata++ = config_global.Valve1State[i];
      *pdata++ = config_global.Valve2State[i];
      *pdata++ = config_global.Valve3State[i];
      *pdata++ = config_global.Valve4State[i];
      *pdata++ = config_global.Valve5State[i];
      *pdata++ = config_global.Valve6State[i];
      *pdata++ = config_global.Valve7State[i];
      *pdata++ = config_global.Valve8State[i];
    }
    int size3 = bk_close(pevent, pdata);    
    
  }

  gFirstEvent = 0;

  return bk_size(pevent);
}

