
/** Helper function for making add/rm cycle/period/valve buttons */
function make_button(img, onclick, disable){
    button = document.createElement("button");
    button.className = "image-button";
    button.innerHTML = `<img src="icons/${img}" width="15px">`;
    button.onclick = onclick;
    button.style.padding = '4px 0px 0px 1px';
    button.style.width = '20px';
    button.disabled = disable;
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
            cell.appendChild(div);
            cell.style.textAlign = 'center';
            cell.style.cursor = 'pointer';
            cell.addEventListener('click', (e) => { if (e.target !== div) div.click(); });

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
        cell.appendChild(make_button("chevron-left.svg", rmcycle, NCYCLES<3))
        cell.appendChild(make_button("chevron-right.svg", addcycle, false))

        // valve ids and names
        for(let valvei=0; valvei < NVALVES; valvei++){
            
            // name
            cell = row2.insertCell();
            let div = document.createElement('div');
            div.className = "modbvalue";
            div.setAttribute('data-odb-path', `/Equipment/${NAME}/Settings/ValveNames[${valvei}]`);
            div.setAttribute('data-odb-editable', `1`);
            cell.appendChild(div);
            cell.style.textAlign = 'center';
            cell.style.width = '80px';
            cell.rowSpan = 2;
        }

        // add/remove valve buttons
        cell = row2.insertCell();
        cell.style.width = '40px';
        cell.rowSpan = 2;
        cell.appendChild(make_button("chevron-left.svg", rmvalve, NVALVES < 3))
        cell.appendChild(make_button("chevron-right.svg", addvalve, false))
        
        // populate period durations
        let cellidx_cycle = 0; // cell index in the list
        let cellidx_valve = 0; // cell index in the list
        for(let periodi=0; periodi < NPERIODS; periodi++){

            row = table.insertRow();

            // header column
            cell = row.insertCell();
            let label = document.createElement('label');
            label.innerText = `Period ${periodi}`;
            let box = document.createElement('input');
            box.type = 'checkbox';
            box.className = "modbcheckbox";
            box.setAttribute('data-odb-path', 
                `/Equipment/${NAME}/Settings/PeriodsEnabled[${periodi}]`);
            box.setAttribute('data-odb-editable', `1`);
            label.appendChild(box);
            cell.appendChild(label);
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
                cell.appendChild(make_button("chevron-up.svg", rmperiod, NPERIODS<3))
                cell.appendChild(make_button("chevron-down.svg", addperiod, false))
            }

            // blank column for niceness
            if(periodi == 0){
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

    // copy the period durations, inserting zero at the end of each cycle
    let newdur = [];
    let cyclei = 0;
    for(let d of dur){
        if(cyclei < NCYCLES){
            newdur.push(d);
            cyclei++;
        } else {
            newdur.push(0);     // add new period
            newdur.push(d);     // make sure to keep the data for the next one
            cyclei = 1;         // we pushed d in the last step, now cycle is 1
        }
    }
    newdur.push(0);

    NCYCLES++;
    let paths = [`${basepath}/CyclesEnabled`, `${basepath}/PeriodDurations`];
    let sizes = [NCYCLES, NCYCLES*NPERIODS];
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

    // copy the period durations, deleting a value at the end of each cycle
    let newdur = [];
    let cyclei = 0;
    for(let d of dur){
        if(cyclei < NCYCLES-1){
            newdur.push(d);
            cyclei++;
        } else {
            cyclei = 0;
        }
    }

    NCYCLES--;
    let paths = [`${basepath}/CyclesEnabled`, `${basepath}/PeriodDurations`];
    let sizes = [NCYCLES, NCYCLES*NPERIODS];
    await mjsonrpc_db_resize(paths, sizes);
    await mjsonrpc_db_set_value(paths[1], newdur);

    populate_timings({...settings,
        cyclesenabled: settings.cyclesenabled.slice(0, NCYCLES),
        perioddurations: newdur});
}

/** expand valve list by one */
async function addvalve(){

    let basepath = `/Equipment/${NAME}/Settings`;
    let rpc = await mjsonrpc_db_get_values([basepath]);
    let settings = rpc.result.data[0];
    let dur = settings.valvestates;

    // copy the valve states, inserting false at the end of each period
    let newdur = [];
    let cyclei = 0;
    for(let d of dur){
        if(cyclei < NVALVES){
            newdur.push(d);
            cyclei++;
        } else {
            newdur.push(false);     // add new period
            newdur.push(d);     // make sure to keep the data for the next one
            cyclei = 1;         // we pushed d in the last step, now cycle is 1
        }
    }
    newdur.push(false);

    NVALVES++;
    let paths = [`${basepath}/ValveNames`, `${basepath}/ValveStates`];
    let sizes = [NVALVES, NVALVES*NPERIODS];
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

    // copy the valve states, deleting a value at the end of each period
    let newdur = [];
    let cyclei = 0;
    for(let d of dur){
        if(cyclei < NVALVES-1){
            newdur.push(d);
            cyclei++;
        } else {
            cyclei = 0;
        }
    }

    NVALVES--;
    let paths = [`${basepath}/ValveNames`, `${basepath}/ValveStates`];
    let sizes = [NVALVES, NVALVES*NPERIODS];
    await mjsonrpc_db_resize(paths, sizes);
    await mjsonrpc_db_set_value(paths[1], newdur);

    populate_timings({...settings,
        valvenames: settings.valvenames.slice(0, NVALVES),
        valvestates: newdur});
}

/** expand period list by one */
async function addperiod(){
    // get paths
    let basepath = `/Equipment/${NAME}/Settings`;
    let paths = [`${basepath}/ValveStates`,
                 `${basepath}/PeriodDurations`];
    
    // resize
    NPERIODS++;
    let sizes = [NVALVES*NPERIODS, NCYCLES*NPERIODS];
    await mjsonrpc_db_resize(paths, sizes);
    
    // redraw the table
    populate_timings();
}

/** reduce period list by one */
async function rmperiod(){
    // get paths
    let basepath = `/Equipment/${NAME}/Settings`;
    let paths = [`${basepath}/ValveStates`,
                 `${basepath}/PeriodDurations`];
    
    // resize
    NPERIODS--;
    let sizes = [NVALVES*NPERIODS, NCYCLES*NPERIODS];
    await mjsonrpc_db_resize(paths, sizes);
    
    // redraw the table
    populate_timings();
}

/** Calculate the total duration of each cycle */
function setTotalDuration(){
    let paths = [`/Equipment/${NAME}/Settings/PeriodDurations`];
     mjsonrpc_db_get_values(paths).then(function(rpc){

         // period durations
         let durations = rpc.result.data[0];

         // array of zeros
        let totals = [];
        totals.length = NCYCLES;
        totals.fill(0);

        let idx = 0;

        for(let periodi=0; periodi<NPERIODS; periodi++){
            for(let cyclei=0; cyclei<NCYCLES; cyclei++){
                totals[cyclei] += durations[idx++];
            }
        }
        
        // Set totals
        for(let cyclei=0; cyclei<NCYCLES; cyclei++){
            let cell = document.getElementById(`total_duration_${cyclei}`);
            if(cell != null){
                cell.innerText = totals[cyclei];
            }
        }

     }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}

/** Prevent editing of cycle in progress and highlight cells */
function disable_and_highlight_cycle_in_progress(){
    mjsonrpc_db_get_values([`/Equipment/${NAME}/Settings`]).then(function(rpc){
        let settings = rpc.result.data[0];
        
        // TODO: replace with check for incycle flag
        let incycle = true;

        for(let cyclei=0; cyclei<NCYCLES; cyclei++){
            for(let periodi=0; periodi<NPERIODS; periodi++){
                
                // enable all cycles except for current cycle
                let div = document.getElementById(`dur_c${cyclei}_p${periodi}`);
                let cell = document.getElementById(`cell_c${cyclei}_p${periodi}`);
                
                if(cyclei == settings.currentcycle && incycle){
                    div.setAttribute('data-odb-editable', `0`);
                } else {
                    div.setAttribute('data-odb-editable', `1`);
                }
            
                if(cyclei == settings.currentcycle && periodi == settings.currentperiod && incycle){
                    cell.classList.add("mgreen");
                } else {
                    cell.classList.remove("mgreen");
                }



            }
        }
        
        // highlight current cycle
        for(let cyclei=0; cyclei<NCYCLES; cyclei++){
            let cell = document.getElementById(`cycle_${cyclei}`);
            
            if(cyclei == settings.currentcycle && incycle){
                cell.classList.add("mgreen");
            } else {
                cell.classList.remove("mgreen");
            }
        }
        
        // highlight current period
        for(let periodi=0; periodi<NPERIODS; periodi++){
            let cell = document.getElementById(`period_${periodi}`);
            
            if(periodi == settings.currentperiod && incycle){
                cell.classList.add("mgreen");
            } else {
                cell.classList.remove("mgreen");
            }
        }

     }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}