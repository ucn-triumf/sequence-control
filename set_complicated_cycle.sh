# Script for setting complicated cycle

# Note, when setting ODB variables you need to remember that indexes start with zero.

# disable sequencer before making changes
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/enable n'

# Reset all the durations and valves states
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod1[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod2[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod3[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod4[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod5[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod6[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod7[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod8[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod9[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod10[*] 0'

odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve1State[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve2State[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve3State[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve4State[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve5State[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve6State[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve7State[*] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve8State[*] 0'



# Set Overall parameters
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/numberPeriodsInCycle 5'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/numCyclesInSuper 5'

# Set new durations
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod1[0] 2'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod1[1] 100'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod1[2] 200'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod1[3] 100'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod1[4] 200'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod2[0] 2'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod2[1] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod2[2] 2'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod2[3] 10'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod2[4] 20'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod3[0] 2'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod3[1] 6'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod3[2] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod3[3] 8'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod3[4] 0'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod4[0] 2'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod4[1] 10'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod4[2] 10'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod4[3] 10'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod4[4] 10'


# Set new cycles
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve1State[0] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve1State[4] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve2State[1] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve2State[4] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve3State[2] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve3State[4] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve4State[3] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve4State[4] 1'
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve5State[4] 1'


# re-enable sequencer before making changes
odbedit -c 'set /Equipment/UCNSequencer2018/Settings/enable y'


