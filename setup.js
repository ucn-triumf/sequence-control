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
    // update_beam_status(); // missing epics variables
    update_seq_status();

    // update every 1 second
    setInterval(update_run_control, 1000);
    setInterval(setTotalDuration, 1000);
    // setInterval(update_beam_status, 1000); // missing epics variables
    setInterval(update_seq_status, 1000);
    setInterval(disable_and_highlight_cycle_in_progress, 1000);
}
