# sequence-control

Repository for MIDAS frontend program that controls the UCN PPG-32 sequencer.

See [[PPG_commands_explained]] for details on c++ code behaviour. 

Python structure: 

* `VME.py`: low-level wrappers for talking to the VME crate
* `PPG.py`: general ppg commands
* `Sequencer.py`: translation of `sequence_control_multi_valve.cxx`... unfinished


