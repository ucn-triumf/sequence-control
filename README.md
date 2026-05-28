# sequence-control

Repository for MIDAS frontend program that controls the UCN PPG-32 sequencer.

See [[PPG_commands_explained]] for details on c++ code behaviour. 

Python structure: 

* `VME.py`: low-level wrappers for talking to the VME crate
* `PPG.py`: general ppg commands
* `Sequencer.py`: translation of `sequence_control_multi_valve.cxx`... unfinished

## Logic changes as compared to `sequence_control_multi_valve.cxx`

* `set_ppg_sequence_cycle`: 
  * Fixed potential race condition: halt set at socket 0, then external trigger enabled, then hold set a socket 0. Moved socket 0 hold command after writing the rest of the instructions to avoid triggering the PPG mid-write
  * Also removed the write to socket 0 at the end since this code was moved into `PPG.start`. 

* frontend
  * No more timing sequence at the end of the run (possible cause for hang on run end?)