// Sequencer functions

// global variables
const NAME = "UCNSequencer26";
window.NCYCLES = 0;
window.NPERIODS = 0;
window.NVALVES = 0;

function setup(){

    // draw elements in base webpage
    populate_run_control();
    populate_timings();

    // update run control status and buttons every 1 second
    setInterval(update_run_control, 1000)
    setInterval(setTotalDuration, 1000)
}
