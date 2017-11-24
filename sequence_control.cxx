
/********************************************************************\

UCN Sequence Control frontend

Sequencing done using TRIUMF Programmable Pulse Generator (PPG) VME module

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



extern "C" {

}

/* make frontend functions callable from the C framework */
#ifdef __cplusplus
extern "C" {
#endif

/*-- Globals -------------------------------------------------------*/

/* The frontend name (client name) as seen by other MIDAS clients   */
   char *frontend_name = "fesequencer";
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

    {"UCNSequencer",               /* equipment name */
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

struct SEQUENCE_SETTINGS {
  double delayTime;
  double valveOPenTime;
  bool enable;
  double blank;
  bool autocycling;
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

  printf("ODB settings: %i %i %f %f ",config_global.enable,config_global.autocycling, config_global.delayTime,config_global.valveOPenTime );
  
  //if(gEnabled){
  //  cm_msg(MINFO,"PPG","Sequencer enabled");
  //}else{
  //  cm_msg(MINFO,"PPG","Sequencer disabled");
  //}

  gEnabled = config_global.enable;
  gAutoCycling =  config_global.autocycling;
  printf("auto-cycling %i\n",gAutoCycling);
  // If not using autocycling, then set the delay
  if(!gAutoCycling){
    if(config_global.delayTime >= 0.0 && config_global.delayTime <= 1000.0){
      gDelayTime = config_global.delayTime;
    }else{
      cm_msg(MINFO,"PPG","Invalid value for delayTime = %f (must be between 0-1000sec)", config_global.delayTime);
      cm_msg(MINFO,"PPG","Setting delayTime to 1sec");
      gDelayTime = 1;
    }
  }

  if(config_global.valveOPenTime >= 5.0 && config_global.valveOPenTime <= 1000.0){
    gValveOpenTime = config_global.valveOPenTime;
  }else{
    cm_msg(MINFO,"PPG","Invalid value for valveOpenTime = %f (must be between 5-1000sec)", config_global.valveOPenTime);
    cm_msg(MINFO,"PPG","Setting valveOpenTime to 5sec");
    gValveOpenTime = 5.0;  
  }
  
}




// Here's the plan for how to map the PPG outputs
// ch 1 -> UCN gate valve control
// ch 3/4 -> end irradiation signal
// ch 5/6 -> start of valve open signal
// ch 7/8 -> valve close signal
// ch 9/10 -> spare
// ch 11 -> in delay period signal
// ch 12 -> in valve open period
// Helper method to write the relevant commands.
void set_command(int i,  unsigned int reg1, unsigned int reg2, unsigned int reg3, unsigned int reg4){

    mvme_write_value(myvme, PPG_BASE+8 , i);
    
    // Write the 128 bits of instruction to 4 registers
    mvme_write_value(myvme, PPG_BASE+0x0c , reg1);
    mvme_write_value(myvme, PPG_BASE+0x10 , reg2);
    mvme_write_value(myvme, PPG_BASE+0x14 , reg3);   
    mvme_write_value(myvme, PPG_BASE+0x18 , reg4);   

}  

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

  if(gAutoCycling){ // reset delay value
    gDelayTime = gAutoCycleDelays[gAutoCycleIndex];
  }

  //  printf("Setting up new sequence: delayTime=%f, UCN valve open time=%f \n",gDelayTime,gValveOpenTime);
  if(gAutoCycling){
    cm_msg(MINFO,"Settings","Setting up new sequence (auto-cycling): delayTime=%f, UCN valve open time=%f", gDelayTime,gValveOpenTime);   
  }else{
    cm_msg(MINFO,"Settings","Setting up new sequence: delayTime=%f, UCN valve open time=%f", gDelayTime,gValveOpenTime);     
  }



  // ----------------------------------------
  // Start writing the instruction set
  // All commands consist of 128-bits, spread across 4 32-bit words
  
  // First command; send pulse indicating start.
  set_command(0,0xc,0xfffffff3,0x1,0x100000);

  // Delay for gDelayTime; split this into a loop over 100 of gDelayTime/100.0 seconds each.
  // This is to get around 32-bit limitation in max limit per command.
  unsigned int delay = (unsigned int)(gDelayTime*1e8/100.0);   // 10e8 cycles per second, loop of 100 (0x64) commands.
  set_command(1,0x0,   0x0, 0x0, 0x200064);
  set_command(2,0x400, 0xfffffbff,delay,0x100000);
  set_command(3,0x0,   0x0, 0x0, 0x300000);
  

  // Open valve and wait for specified time; again, set it to 
  // On first clock, send pulse to V1720...
  unsigned int opentime = (unsigned int)(gValveOpenTime*1e8/100.0); 
  set_command(4,0x831, 0xfffff7ce,0x1,0x100000);
  set_command(5,0x0,   0x0, 0x0, 0x200064);
  set_command(6,0x801, 0xfffff7fe,opentime,0x100000);
  set_command(7,0x0,   0x0, 0x0, 0x300000);

  // end of valve open time; close valve, send signal; then turn off all outputs.
  set_command(8,0xc0, 0xffffff3f,0x1,0x100000);
  set_command(9,0x0,  0xffffffff,0x1,0x100000);
  set_command(10,0x0, 0xffffffff,0x1,0x0);

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
  status = db_find_key (hDB, 0, "/Equipment/UCNSequencer/Settings", &settings_handle_global_);
  if (status != DB_SUCCESS) cm_msg(MINFO,"SetBoardRecord","Key not found. Return code: %d",  status);
  
  INT size = sizeof(SEQUENCE_SETTINGS);

  //hotlink
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
    }
    
  }else{ // if we were in sequence last time and aren't now, then sequence is finished.
    if(previous_reg0_bit0){
      if(first_event){
	first_event = 0;
      }else{
	sequence_finished = 1;
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

