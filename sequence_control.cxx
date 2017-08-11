
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
      RO_RUNNING,             /* read only when running */

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
// This is how long (in sec) we delay opening the UCN valve.
double gDelayTime;
// This is how long (in sec) we leave gate valve open.
double gValveOpenTime;

struct SEQUENCE_SETTINGS {
  double delayTime;
  double valveOPenTime;
  bool enable;
} static config_global;


// Get the current settings from the ODB
void setVariables(){

  int status=0;
  INT size = sizeof(SEQUENCE_SETTINGS);
  //get actual record
  status = db_get_record(hDB, settings_handle_global_, &config_global, &size, 0);
  if (status != DB_SUCCESS){
    cm_msg(MERROR,"SetBoardRecord","Couldn't get record. Return code: %d", status);
    return ;
  }

  gEnabled = config_global.enable;
  if(config_global.delayTime >= 0.0 && config_global.delayTime <= 100.0){
    gDelayTime = config_global.delayTime;
  }else{
    cm_msg(MINFO,"PPG","Invalid value for delayTime = %f (must be between 0-100sec)", config_global.delayTime);
    cm_msg(MINFO,"PPG","Setting delayTime to 1sec");
    gDelayTime = 1;
  }
  if(config_global.valveOPenTime >= 5.0 && config_global.valveOPenTime <= 100.0){
    gValveOpenTime = config_global.valveOPenTime;
  }else{
    cm_msg(MINFO,"PPG","Invalid value for valveOpenTime = %f (must be between 5-100sec)", config_global.valveOPenTime);
    cm_msg(MINFO,"PPG","Setting valveOpenTime to 5sec");
    gValveOpenTime = 5.0;  
  }
  
}




// Here's the plan for how to map the PPG outputs
// ch 1 -> UCN gate valve control
// ch 2 -> end irradiation signal
// ch 3 -> start of valve open signal
// ch 4 -> valve close signal
// ch 5 -> in delay period signal
// ch 6 -> in valve open period

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
  // Use the external trigger to inititate the sequence
  mvme_write_value(myvme, PPG_BASE , 0x4);
  if(!gEnabled) return 0;

  printf("Setting up new sequence: delayTime=%f, UCN valve open time=%f \n",gDelayTime,gValveOpenTime);

  // ----------------------------------------
  // Start writing the instruction set
  // All commands consist of 128-bits, spread across 4 32-bit words
  
  // First command; send pulse indicating start.
  set_command(0,0x2,0xfffffffd,0x1,0x100000);

  // Delay for gDelayTime; split this into a loop over 10 of gDelayTime/10.0 seconds each.
  // This is to get around 32-bit limitation in max limit per command.
  unsigned int delay = (unsigned int)(gDelayTime*1e8/10.0);   // 10e8 cycles per second, loop of 10 commands.
  set_command(1,0x0, 0x0, 0x0, 0x20000a);
  set_command(2,0x10, 0xffffffef,delay,0x100000);
  set_command(3,0x0, 0x0, 0x0, 0x300000);
  

  // Open valve and wait for specified time; again, set it to 
  // On first clock, send pulse to V1720...
  unsigned int opentime = (unsigned int)(gValveOpenTime*1e8/10.0); 
  set_command(4,0x25,0xffffffda,0x1,0x100000);
  set_command(5,0x0, 0x0, 0x0, 0x20000a);
  set_command(6,0x21, 0xffffffde,opentime,0x100000);
  set_command(7,0x0, 0x0, 0x0, 0x300000);

  // end of valve open time; close valve, send signal; then turn off all outputs.
  set_command(8,0x8,0xfffffff7,0x1,0x100000);
  set_command(9,0x0,0xffffffff,0x1,0x100000);
  set_command(10,0x0,0xffffffff,0x1,0x0);

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
  if(0){
    printf("Writing to 0x%x\n",PPG_BASE);
    int test0 = mvme_read_value(myvme, PPG_BASE);
    mvme_write_value(myvme, PPG_BASE+4 , 0xdeadbeef);
    int test1 = mvme_read_value(myvme, PPG_BASE+4);
    int test2 = mvme_read_value(myvme, PPG_BASE+0x20);
    int test3 = mvme_read_value(myvme, PPG_BASE+0x18);
    int test4 = mvme_read_value(myvme, PPG_BASE+0x2C);
    printf("Test registers: 0x%x 0x%x 0x%x 0x%x 0x%x \n",test0,test1,test2,test3,test4);
  }

  // Start sequence
  setVariables();
  set_ppg_sequence();

  return SUCCESS;
}

static int gHaveRun = 0;

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


INT read_event(char *pevent, INT off)
{

 

  /* init bank structure */
  bk_init32(pevent);

  uint32_t *pdata32;

  /* create structured ADC0 bank of double words (i.e. 4-byte words)  */
  bk_create(pevent, "SEQN", TID_DWORD, (void **)&pdata32);

  // Get sub-second time;
  struct timeval now;
  gettimeofday(&now,NULL);
  *pdata32++ = now.tv_usec/1000;
  
  
  // Get whether we are in sequence
  int reg0 = mvme_read_value(myvme, PPG_BASE);
  if(reg0 & 1)*pdata32++ = 1;
  else *pdata32++ = 0;

  int size2 = bk_close(pevent, pdata32);    

  return bk_size(pevent);
}

