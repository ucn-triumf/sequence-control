
/** Helper function for making add/rm cycle/period/valve buttons */
function make_button(img, onclick, disable, title, id){
    button = document.createElement("button");
    button.className = "image-button";
    button.innerHTML = `<img src="icons/${img}" width="15px">`;
    button.onclick = onclick;
    button.style.padding = '4px 0px 0px 1px';
    button.style.width = '20px';
    button.disabled = disable;
    button.title = title;
    button.id = id;
    return button
}

/** Fill in cycle and period and valve settings input table.
 *
 * Args:
 *     settings: ODB settings object. If null, fetches from server.
 */
function populate_timings(settings = null){

    // clear table contents
    var table_old = document.getElementById("tbl_timing");
    var table = document.createElement('tbody');
    table.id = "tbl_timing";
    table_old.parentNode.replaceChild(table, table_old);

    // use provided settings or fetch from server
    let promise = settings !== null
        ? Promise.resolve(settings)
        : mjsonrpc_db_get_values([`/Equipment/${NAME}/Settings`]).then(rpc => rpc.result.data[0]);

    promise.then(function(settings){

        // get sizes
        NCYCLES = settings.cyclesenabled.length;
        NPERIODS = settings.perioddurations.length / NCYCLES;
        NVALVES = settings.valvestates.length / NPERIODS;

        // check sizes
        if(NVALVES != settings.valvenames.length){
            mjsonrpc_error_alert(Error(`ODB array length mismatch: NCYCLES = CyclesEnabled.length = ${NCYCLES}, NPERIODS = PeriodDurations.length/NCYCLES = ${NPERIODS}, NVALVES = ValveStates.length/NPERIODS (${NVALVES}) = ValveNames.length (${settings.valvenames.length})`));
        }

        // update header rows span
        let header = document.getElementById("duration_header");
        header.colSpan = NCYCLES+1;

        header = document.getElementById("valve_header");
        header.colSpan = NVALVES+1;

        // write header rows
        let row1 = table.insertRow();   // header labels
        let row2 = table.insertRow();   // checkboxes/names
        let row3 = table.insertRow();   // ids

        // blank column for headers
        let cell = row1.insertCell();
        cell = row2.insertCell();
        cell.innerText = "Cycle Enable";

        cell = row3.insertCell();
        cell.innerText = "ID";
        
        // cycle label
        cell = row1.insertCell();
        cell.innerText = 'Cycles';
        cell.colSpan = NCYCLES+1;
        cell.style.textAlign = 'center';

        // valve label
        cell = row1.insertCell();
        cell.innerText = 'Valves';
        cell.colSpan = NVALVES+1;
        cell.style.textAlign = 'center';

        // cycle ids and checkboxes
        for(let cyclei=0; cyclei < NCYCLES; cyclei++){

            // checkbox
            cell = row2.insertCell();
            let div = document.createElement('input');
            div.type = "checkbox";
            div.className = "modbcheckbox";
            div.setAttribute('data-odb-path', `/Equipment/${NAME}/Settings/CyclesEnabled[${cyclei}]`);
            div.setAttribute('data-odb-editable', `1`);
            div.id = `cycle_checkbox_${cyclei}`;
            cell.appendChild(div);
            cell.style.textAlign = 'center';
            cell.style.cursor = 'pointer';
            cell.addEventListener('click', (e) => { if (e.target !== div) div.click(); });
            cell.id = `cycle_checkbox_cell${cyclei}`;

            // id
            cell = row3.insertCell();
            cell.innerText = cyclei;
            cell.style.textAlign = 'center';
            cell.style.fontWeight = 'bold';
            cell.id = `cycle_${cyclei}`;
        }

        // add/remove cycle buttons
        cell = row2.insertCell();
        cell.style.width = '40px';
        cell.rowSpan = 2;
        cell.appendChild(make_button("chevron-left.svg", 
                                     rmcycle, 
                                     NCYCLES<3, 
                                     "Remove cycle",
                                     "cycle_rm_button"));
        cell.appendChild(make_button("chevron-right.svg", 
                                     addcycle, 
                                     false, 
                                     "Add cycle",
                                    "cycle_add_button"));

        // valve ids and names
        for(let valvei=0; valvei < NVALVES; valvei++){
            
            // name
            cell = row2.insertCell();
            let div = document.createElement('div');
            div.className = "modbvalue";
            div.setAttribute('data-odb-path', `/Equipment/${NAME}/Settings/ValveNames[${valvei}]`);
            div.setAttribute('data-odb-editable', `1`);
            div.id = `valve_name_${valvei}`;
            cell.appendChild(div);
            cell.style.textAlign = 'center';
            cell.style.width = '80px';
            cell.rowSpan = 2;
            cell.title = `PPG CH${valvei*2+1}`;
        }

        // add/remove valve buttons
        cell = row2.insertCell();
        cell.style.width = '40px';
        cell.rowSpan = 2;
        cell.appendChild(make_button("chevron-left.svg", 
                                     rmvalve, 
                                     NVALVES < 3, 
                                     "Remove valve",
                                     "valve_rm_button"))
        cell.appendChild(make_button("chevron-right.svg", 
                                     addvalve, 
                                     false, 
                                     "Add valve",
                                     "valve_add_button"))
        
        // populate period durations
        let cellidx_cycle = 0; // cell index in the list
        let cellidx_valve = 0; // cell index in the list
        for(let periodi=0; periodi < NPERIODS; periodi++){

            row = table.insertRow();

            // header column - period labels
            cell = row.insertCell();
            let label = document.createElement('label');
            label.innerText = `Period ${periodi}`;
            
            // period checkbox
            let box = document.createElement('input');
            box.type = 'checkbox';
            box.className = "modbcheckbox";
            box.setAttribute('data-odb-path', 
                `/Equipment/${NAME}/Settings/PeriodsEnabled[${periodi}]`);
            box.setAttribute('data-odb-editable', `1`);
            box.id = `checkbox_period_${periodi}`;
            label.appendChild(box);
            
            // period name
            let pname = document.createElement('span');
            pname.className = "modbvalue";
            pname.setAttribute('data-odb-path',
                `/Equipment/${NAME}/Settings/PeriodNames[${periodi}]`);
            pname.setAttribute('data-odb-editable', '1');
            pname.id = `period_name_${periodi}`;
            
            cell.appendChild(label);
            cell.appendChild(pname);
            cell.style.cursor = 'pointer';
            cell.addEventListener('click', (e) => { if (e.target === cell) box.click(); });
            cell.id = `period_${periodi}`;

            // cell timings
            for(let cyclei=0; cyclei < NCYCLES; cyclei++){
                cell = row.insertCell();
                cell.id = `cell_c${cyclei}_p${periodi}`;
                let div =  document.createElement('div');
                div.className = "modbvalue";
                div.setAttribute('data-odb-path', 
                    `/Equipment/${NAME}/Settings/PeriodDurations[${cellidx_cycle++}]`);
                div.setAttribute('data-odb-editable', `1`);
                div.id = `dur_c${cyclei}_p${periodi}`;
                cell.appendChild(div);
                cell.style.textAlign = 'center';     
            }

            // buttons for period add/remove
            if(periodi == NPERIODS-2) {
                cell = row.insertCell();
                cell.rowSpan = 2;
                cell.appendChild(make_button("chevron-up.svg", 
                                             rmperiod, 
                                             NPERIODS<3, 
                                             "Remove period",
                                             "period_rm_button"))
                cell.appendChild(make_button("chevron-down.svg", 
                                             addperiod, 
                                             false, 
                                             "Add period",
                                             "period_add_button"))
            }

            // blank column between buttons and valves (only needed when buttons don't span all rows)
            if(periodi == 0 && NPERIODS > 2){
                cell = row.insertCell();
                cell.rowSpan = NPERIODS-2;
            }

            // valve states
            for(let valvei=0; valvei < NVALVES; valvei++){
                cell = row.insertCell();
                let div =  document.createElement('input');
                div.type = "checkbox";
                div.className = "modbcheckbox";
                div.setAttribute('data-odb-path',
                    `/Equipment/${NAME}/Settings/ValveStates[${cellidx_valve++}]`);
                div.setAttribute('data-odb-editable', `1`);
                div.id = `checkbox_valve_${valvei}_${periodi}`;
                cell.appendChild(div);
                cell.style.textAlign = 'center';
                cell.style.width = '1px';
                cell.style.cursor = 'pointer';
                cell.addEventListener('click', (e) => { if (e.target !== div) div.click(); });
            }

            // blank column for niceness
            if(periodi == 0){
                cell = row.insertCell();
                cell.rowSpan = NPERIODS;
            }
        }

        // Cycle durations
        row = table.insertRow();
        cell = row.insertCell();
        cell.innerText = 'Total Duration'
        for(let cyclei=0; cyclei < NCYCLES; cyclei++){
            cell = row.insertCell();
            cell.id = `total_duration_${cyclei}`;
            cell.style.textAlign = 'center';
        }

        // blank row for niceness
        cell = row.insertCell();
        cell = row.insertCell();
        cell.colSpan = NVALVES + 1;
        cell.innerText = "Normally closed valves are open for selected periods, otherwise closed";

        // run update
        setTotalDuration();

    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });
}

/** expand cycle list by one */
async function addcycle(){

    let basepath = `/Equipment/${NAME}/Settings`;
    let rpc = await mjsonrpc_db_get_values([basepath]);
    let settings = rpc.result.data[0];
    let dur = settings.perioddurations;
    let ncycles = settings.cyclesenabled.length;
    let nperiods = dur.length / ncycles;

    // copy the period durations, inserting previous duration at the end of each cycle
    let newdur = [];
    let cyclei = 0;
    let old_d = 0;
    for(let d of dur){
        if(cyclei < ncycles){
            newdur.push(d);
            cyclei++;
        } else {
            newdur.push(old_d);     // add new cycle
            newdur.push(d);     // make sure to keep the data for the next one
            cyclei = 1;         // we pushed d in the last step, now cycle is 1
        }
        old_d = d;  // track old durations 
    }
    newdur.push(0);

    NCYCLES = ncycles + 1;
    let paths = [`${basepath}/CyclesEnabled`, `${basepath}/PeriodDurations`];
    let sizes = [NCYCLES, NCYCLES * nperiods];
    await mjsonrpc_db_resize(paths, sizes);
    await mjsonrpc_db_set_value(paths[1], newdur);

    populate_timings({...settings,
        cyclesenabled: [...settings.cyclesenabled, true],
        perioddurations: newdur});
}

/** reduce cycle list by one */
async function rmcycle(){

    let basepath = `/Equipment/${NAME}/Settings`;
    let rpc = await mjsonrpc_db_get_values([basepath]);
    let settings = rpc.result.data[0];
    let dur = settings.perioddurations;
    let ncycles = settings.cyclesenabled.length;
    let nperiods = dur.length / ncycles;

    // copy the period durations, deleting a value at the end of each cycle
    let newdur = [];
    let cyclei = 0;
    for(let d of dur){
        if(cyclei < ncycles-1){
            newdur.push(d);
            cyclei++;
        } else {
            cyclei = 0;
        }
    }

    NCYCLES = ncycles - 1;
    let paths = [`${basepath}/CyclesEnabled`, `${basepath}/PeriodDurations`];
    let sizes = [NCYCLES, NCYCLES * nperiods];

    // Redraw table before resizing ODB — populate_timings synchronously clears the old tbody,
    // removing modbcheckbox elements at stale CyclesEnabled[N] paths. Without this, MIDAS
    // polling can fire between the resize and the repopulate and fail with "index exceeds array length".
    populate_timings({...settings,
        cyclesenabled: settings.cyclesenabled.slice(0, NCYCLES),
        perioddurations: newdur});

    await mjsonrpc_db_resize(paths, sizes);
    await mjsonrpc_db_set_value(paths[1], newdur);
}

/** expand valve list by one */
async function addvalve(){

    let basepath = `/Equipment/${NAME}/Settings`;
    let rpc = await mjsonrpc_db_get_values([basepath]);
    let settings = rpc.result.data[0];
    let dur = settings.valvestates;
    let nvalves = settings.valvenames.length;
    let nperiods = dur.length / nvalves;

    // copy the valve states, inserting false at the end of each period
    let newdur = [];
    let valvei = 0;
    for(let d of dur){
        if(valvei < nvalves){
            newdur.push(d);
            valvei++;
        } else {
            newdur.push(false);     // add new valve
            newdur.push(d);     // make sure to keep the data for the next one
            valvei = 1;         // we pushed d in the last step, now valve is 1
        }
    }
    newdur.push(false);

    NVALVES = nvalves + 1;
    let paths = [`${basepath}/ValveNames`, `${basepath}/ValveStates`];
    let sizes = [NVALVES, NVALVES * nperiods];
    await mjsonrpc_db_resize(paths, sizes);
    await mjsonrpc_db_set_value(paths[1], newdur);

    populate_timings({...settings,
        valvenames: [...settings.valvenames, ''],
        valvestates: newdur});
}

/** reduce valve list by one */
async function rmvalve(){

    let basepath = `/Equipment/${NAME}/Settings`;
    let rpc = await mjsonrpc_db_get_values([basepath]);
    let settings = rpc.result.data[0];
    let dur = settings.valvestates;
    let nvalves = settings.valvenames.length;
    let nperiods = dur.length / nvalves;

    // copy the valve states, deleting a value at the end of each period
    let newdur = [];
    let valvei = 0;
    for(let d of dur){
        if(valvei < nvalves-1){
            newdur.push(d);
            valvei++;
        } else {
            valvei = 0;
        }
    }

    NVALVES = nvalves - 1;
    let paths = [`${basepath}/ValveNames`, `${basepath}/ValveStates`];
    let sizes = [NVALVES, NVALVES * nperiods];

    populate_timings({...settings,
        valvenames: settings.valvenames.slice(0, NVALVES),
        valvestates: newdur});

    await mjsonrpc_db_resize(paths, sizes);
    await mjsonrpc_db_set_value(paths[1], newdur);
}

/** expand period list by one */
async function addperiod(){
    // get paths
    let basepath = `/Equipment/${NAME}/Settings`;
    let paths = [`${basepath}/ValveStates`,
                 `${basepath}/PeriodDurations`,
                 `${basepath}/PeriodsEnabled`,
                 `${basepath}/PeriodNames`];

    // resize
    NPERIODS++;
    let sizes = [NVALVES*NPERIODS, NCYCLES*NPERIODS, NPERIODS, NPERIODS];
    await mjsonrpc_db_resize(paths, sizes);
    
    // redraw the table
    populate_timings();
}

/** reduce period list by one */
async function rmperiod(){
    let basepath = `/Equipment/${NAME}/Settings`;
    let rpc = await mjsonrpc_db_get_values([basepath]);
    let settings = rpc.result.data[0];

    let paths = [`${basepath}/ValveStates`,
                 `${basepath}/PeriodDurations`,
                 `${basepath}/PeriodsEnabled`,
                 `${basepath}/PeriodNames`];

    NPERIODS--;
    let sizes = [NVALVES*NPERIODS, NCYCLES*NPERIODS, NPERIODS, NPERIODS];

    // Redraw before resizing ODB — removes stale modbcheckbox/modbvalue elements so MIDAS
    // polling doesn't try to read truncated array indices after the resize.
    populate_timings({...settings,
        periodsenabled:  settings.periodsenabled.slice(0, NPERIODS),
        perioddurations: settings.perioddurations.slice(0, NCYCLES * NPERIODS),
        valvestates:     settings.valvestates.slice(0, NVALVES * NPERIODS),
        periodnames:     settings.periodnames.slice(0, NPERIODS),
    });

    await mjsonrpc_db_resize(paths, sizes);
}

/** Calculate the total duration of each cycle */
function setTotalDuration(){
    if(NCYCLES === 0) return;  // globals not yet initialized; avoid division by zero

    let paths = [`/Equipment/${NAME}/Settings/PeriodDurations`,
                 `/Equipment/${NAME}/Settings/PeriodsEnabled`,
                 `/Equipment/${BEAMLINE_EPICS}/Settings/Names`,
                 `/Equipment/${BEAMLINE_EPICS}/Variables/Measured`
                ];
    mjsonrpc_db_get_values(paths).then(function(rpc){
        
        // get data from rpc call
        let durations = rpc.result.data[0];
        let enabled = rpc.result.data[1];

        // Beamline EPICS names/values don't exist until that equipment has
        // run at least once; treat their absence as "no beam data" so the
        // pre-run page doesn't throw.
        let names = rpc.result.data[2];
        let measured = rpc.result.data[3];
        let beam = {};
        if(names && measured){
            for(let i=0; i<names.length; i++){
                beam[names[i]] = measured[i];
            }
        }

        let beamon = parseFloat(beam["B1V:KSM:RDBEAMON.VAL1"]) * 0.000888111;
        let beamoff = parseFloat(beam["B1V:KSM:RDBEAMOFF.VAL1"]) * 0.000888111;

        // derive NPERIODS from fetched length to avoid reading stale globals during a resize
        let nperiods = Math.floor(durations.length / NCYCLES);

         // array of zeros
        let totals = [];
        totals.length = NCYCLES;
        totals.fill(0);

        let idx = 0;

        for(let periodi=0; periodi<nperiods; periodi++){

            if(!enabled[periodi]) continue;

            for(let cyclei=0; cyclei<NCYCLES; cyclei++){
                totals[cyclei] += durations[idx++];
            }
        }

        // Set totals
        for(let cyclei=0; cyclei<NCYCLES; cyclei++){
            let cell = document.getElementById(`total_duration_${cyclei}`);
            let checkbox_cell = document.getElementById(`cycle_checkbox_cell${cyclei}`);
            if(cell != null){
                cell.innerText = totals[cyclei];

                 // fade checkbox cell to grey
                if(checkbox_cell.style.backgroundColor === "var(--myellow)"){
                    checkbox_cell.style.setProperty("-webkit-transition", 
                        "background-color 3s", 
                        "");
                    checkbox_cell.style.setProperty("transition", 
                        "background-color 3s", 
                        "");    
                    checkbox_cell.style.backgroundColor = "";
                } else {
                    checkbox_cell.style.removeProperty("-webkit-transition");
                    checkbox_cell.style.removeProperty("transition");
                }

                // highlight bad total duration
                let is_too_long = totals[cyclei] > beamon + beamoff - 10;
                let is_too_short = totals[cyclei] == 0;
                if(is_too_long){
                    cell.classList.add('morange');
                    cell.title ="Total cycle duration must be less than beam on + beam off - 10 seconds";
                } else {
                    cell.classList.remove('morange');
                    cell.title = "";
                }

                if(is_too_long || is_too_short){
                    // flash yellow
                    if(document.getElementById(`cycle_checkbox_${cyclei}`).checked){
                        checkbox_cell.style.backgroundColor = "var(--myellow)";
                    } 
                    
                    // disable checkbox
                    mjsonrpc_db_set_value(`/Equipment/${NAME}/Settings/CyclesEnabled[${cyclei}]`,
                                          false);                    
                } 
            }
        }

     }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}

/** Prevent editing of cycle in progress and highlight cells */
function disable_and_highlight_cycle_in_progress(){
    mjsonrpc_db_get_values([`/Equipment/${NAME}/Settings`,
                            `/Equipment/${NAME}/Variables`,
                            '/Runinfo/State']).then(function(rpc){
        let settings = rpc.result.data[0];
        let variables = rpc.result.data[1];
        let inrun = rpc.result.data[2] !== STATE_STOPPED;
        // SEQC bank only exists once a run has produced a cycle; treat its
        // absence as "not in cycle" so the pre-run page doesn't throw.
        let seqc = variables && variables['seqc'] ? variables['seqc'].map(Number) : null;
        let cycle_start   = seqc ? seqc[0] : 0;
        let incycle       = seqc ? seqc[2] : 0;
        let current_cycle = seqc ? seqc[3] : 0;
        
        // highlight current cycle
        for(let cyclei=0; cyclei<NCYCLES; cyclei++){
            let cell = document.getElementById(`cycle_${cyclei}`);
            if(cell === null) continue;

            if(cyclei == settings.currentcycle && inrun && incycle){
                cell.classList.add("mgreen");
            } else {
                cell.classList.remove("mgreen");
            }
        }

        // get current period
        let ncycles = settings['cyclesenabled'].length
        let period_elapsed = 0;
        let current_period = 0;
        let cycle_elapsed = Date.now()/1000 - cycle_start;
        for(let i=current_cycle; i<settings['perioddurations'].length; i+=ncycles){

            current_period = (i-current_cycle)/ncycles;

            if(settings['periodsenabled'][current_period] == 1){
                period_elapsed += settings['perioddurations'][i];
            }

            if(period_elapsed > cycle_elapsed){
                break;
            }
        }

        // highlight current period
        for(let periodi=0; periodi<NPERIODS; periodi++){
            let cell = document.getElementById(`period_${periodi}`);
            if(cell === null) continue;

            if(periodi == current_period && inrun && incycle){
                cell.classList.add("mgreen");
            } else {
                cell.classList.remove("mgreen");
            }
        }
        
        // highlight current cycle/period duration cell
        for(let cyclei=0; cyclei<NCYCLES; cyclei++){
            for(let periodi=0; periodi<NPERIODS; periodi++){

                // enable all cycles except for current cycle
                let div = document.getElementById(`dur_c${cyclei}_p${periodi}`);
                let cell = document.getElementById(`cell_c${cyclei}_p${periodi}`);
                if(div === null || cell === null) continue;

                if(cyclei == settings.currentcycle && inrun){
                    div.setAttribute('data-odb-editable', `0`);
                } else {
                    div.setAttribute('data-odb-editable', `1`);
                }

                if(cyclei == settings.currentcycle && periodi == current_period && inrun && incycle){
                    cell.classList.add("mgreen");
                } else {
                    cell.classList.remove("mgreen");
                }
            }
        }


        // elements to disable / enable elements when in run
        let to_disable = ['valve_rm_button',
                          'valve_add_button',
                          'period_rm_button',
                          'period_add_button',
                          'cycle_rm_button',
                          'load_button',
                         ];
        let to_uneditable = [];

        for(let periodi=0; periodi<NPERIODS; periodi++){

            // period checkboxes and names
            to_disable.push(`checkbox_period_${periodi}`);
            to_uneditable.push(`period_name_${periodi}`);

            // valve checkboxes
            for(let valvei=0; valvei<NVALVES; valvei++){
                to_disable.push(`checkbox_valve_${valvei}_${periodi}`);
            }
        }

        // valve names
        for(let valvei=0; valvei<NVALVES; valvei++){
            to_uneditable.push(`valve_name_${valvei}`);
        }

        // enable/disable
        for(let name of to_disable){
            let ele = document.getElementById(name);
            if(ele === null) continue;

            // respect minimum-count constraints even when not in run
            let count_disabled = (name === 'valve_rm_button'  && NVALVES  < 3)
                               || (name === 'period_rm_button' && NPERIODS < 3)
                               || (name === 'cycle_rm_button'  && NCYCLES  < 3);

            if(inrun || count_disabled) ele.setAttribute('disabled', '');
            else                        ele.removeAttribute('disabled');
        }

        // editable / uneditable
        for(let name of to_uneditable){
            let ele = document.getElementById(name);
            if(inrun)   ele.setAttribute('data-odb-editable', '0');
            else        ele.setAttribute('data-odb-editable', '1');
        }

     }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}