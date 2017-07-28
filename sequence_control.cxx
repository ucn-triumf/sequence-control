
/********************************************************************\

  Name:         fevme.cxx
  Created by:   K.Olchanski

  Contents:     Frontend for the generic VME DAQ using CAEN ADC, TDC, TRIUMF VMEIO and VF48
$Id$
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

      500,                    /* poll for 500ms */
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
DWORD PPG_BASE = 0xc00000;

/*-- Frontend Init -------------------------------------------------*/
//int ppg_add=( 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7,	\
//0x8,  0x9,  0xa  0xb  0xc  0xd  0xe  0xf				\
//              0x10 0x11 0x12 0x13 0x14 0x15 0x16 0x17			\
//0x18 0x19 0x1a 0x1b 0x1c 0x1d 0x1e 0x1f				\
//                0x20 0x21 0x22 0x23 0x24 0x25 0x26 0x27 )


HNDLE settings_handle_global_;              //!< Handle for the global settings record

extern HNDLE hDB;
HNDLE h;


int gFactor = 1;
bool gEnabled = true;

struct SEQUENCE_SETTINGS {
  BOOL enable;
  INT speed;
} static config_global;


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
  if(config_global.speed >= 1 && config_global.speed <= 11){
    gFactor = config_global.speed;
  }else{
    cm_msg(MINFO,"PPG","Invalid value for speed = %i (must be between 1-8)", config_global.speed );
  }
  
}





int ppg_inst[]={							  \
  0x00,  0xffffffff,  0x4,    0x100000,   0x0,   0x0,   0x0,  0x200fff,	\
  0x10101010,   0x08080808,   0x1000000,  0x100000,  0x20202020,   0x10101010,   0x1000000,  0x100000, \
  0x40404040,   0x20202020,   0x1000000,  0x100000,  0x80808080,   0x40404040,   0x1000000,  0x100000, \
  0x01010101,   0x80808080,   0x1000000,  0x100000,  0x02020202,   0x01010101,   0x1000000,  0x100000, \
  0x04040404,   0x02020202,   0x1000000,  0x100000,  0x08080808,   0x04040404,   0x1000000,  0x100000, \
  0x0,   0x0,   0x0, 0x300000,   0x0,   0x0,   0x0,  0x000000 };
  

INT set_ppg_sequence(){

  mvme_write_value(myvme, PPG_BASE , 0x8);
  mvme_write_value(myvme, PPG_BASE , 0x0);
  if(!gEnabled) return 0;

  int inst_index = 0;
  for(int i = 0; i < 12; i++){
    
    // Set the instruction address
    mvme_write_value(myvme, PPG_BASE+8 , i);
    

    // Write the 128 bits of instruction to 4 registers
    mvme_write_value(myvme, PPG_BASE+0x0c , ppg_inst[inst_index]); inst_index++;
    mvme_write_value(myvme, PPG_BASE+0x10 , ppg_inst[inst_index]); inst_index++;

    if(i >= 2 && i <= 9){
      int delay = 0x400000 * gFactor;
      mvme_write_value(myvme, PPG_BASE+0x14 , delay); inst_index++;    
    }else{
      mvme_write_value(myvme, PPG_BASE+0x14 , ppg_inst[inst_index]); inst_index++;
    }
    mvme_write_value(myvme, PPG_BASE+0x18 , ppg_inst[inst_index]); inst_index++;
    
  }

  mvme_write_value(myvme, PPG_BASE+8 , 0x0);
  mvme_write_value(myvme, PPG_BASE , 0x1);


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
  printf("read event!\n");

  /* init bank structure */
  bk_init32(pevent);


  return bk_size(pevent);
}

