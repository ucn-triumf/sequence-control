// Sequencer functions

function setup(){

    // draw elements in base webpage
    populate_run_control();
    populate_timings();

    // update run control status and buttons every 1 second
    setInterval(update_run_control, 1000)
}

/** Fill in run control table elements from the ODB */
function populate_run_control(){       
    // get all elements from odb
    mjsonrpc_db_ls(["/Experiment/Edit on start"]).then(function(rpc){
        
        // check if odb path exists
        if(!rpc.result.status){
            table.innerText = "\"Edit on start\" not found in ODB";
            return;
        }

        // get table
        let table = document.getElementById("tbl_run_control");

        // get key names
        let values = rpc.result.data[0];
        for(key of Object.keys(values)){
            
            // don't print */key values
            if(key.includes("/key")){
                continue;
            }

            // set title cell
            let row = table.insertRow();
            let cell = row.insertCell();
            cell.innerText = key;

            // ODB editable link
            let odb_value;
            if(typeof(values[key]) == "boolean"){
                odb_value = document.createElement('input');
                odb_value.type = "checkbox";
                odb_value.className = "modbcheckbox";
            } else {
                odb_value = document.createElement('span');
                odb_value.className = "modbvalue";
            }
            
            odb_value.setAttribute("data-odb-editable", "1");
            odb_value.setAttribute("data-odb-path", `/Experiment/Edit on start/${key}`);
            
            // insert ODB value cell
            cell = row.insertCell();
            cell.style.textAlign = "center";
            cell.appendChild(odb_value);
        }

    });
}

/** Set run control buttons start/stop and status banner */
function update_run_control(){

    // check if the run is running
    mjsonrpc_db_get_values(["/runinfo"]).then(function(rpc) {
        let runinfo = rpc.result.data[0];

        // make button and get cell
        let button1 = document.createElement('button');
        let button2 = document.createElement('button');
        let cell = document.getElementById("run_control_buttoncell");

        // clear old contents of the cell
        while(cell.firstChild){
            cell.removeChild(cell.firstChild);
        }

        let banner = document.getElementById("run_control_status");
        
        // set properties depending on the run state
        if (runinfo.state === STATE_RUNNING) {
            
            // button
            button1.innerText = 'Stop Run';
            button1.onclick = () => mhttpd_stop_run('&Return=custom&page=Sequencer26');
            cell.appendChild(button1);
            
            // button2.innerText = 'Pause Run';
            // button1.onclick = () => mhttpd_pause_run('&Return=custom&page=Sequencer26');
            // cell.appendChild(button2);
            
            // banner
            banner.innerHTML = `Run ${runinfo["run number"]} in progress`;
            banner.classList.add("mgreen");

        } else if (runinfo.state === STATE_PAUSED) {
            
            // button
            button1.innerText = 'Stop Run';
            button1.onclick = () => mhttpd_stop_run('&Return=custom&page=Sequencer26');
            cell.appendChild(button1);
            
            banner.innerHTML = `Run ${runinfo["run number"]} paused`;
            banner.classList.add("myellow");

            // button2.innerText = 'Resume Run';
            // button1.onclick = () => mhttpd_resume_run('&Return=custom&page=Sequencer26');
            // cell.appendChild(button2);
            
        } else if (runinfo.state === STATE_STOPPED) {
            
            // button
            button1.innerText = 'Start Run';
            button1.onclick = () => mhttpd_start_run('&Return=custom&page=Sequencer26');
            cell.appendChild(button1);

            banner.innerHTML = `Run ${runinfo["run number"]} finished`;
            banner.classList.add("mred");
        }

    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}

/** Fill in cycle and period and valve settings input table */
function populate_timings(){

    let table = document.getElementById("tbl_timing");

    // get equipment settings and populate
    mjsonrpc_db_get_values(['/Equipment/UCNSequencer26/Settings']).then(function(rpc){

        let settings = rpc.result.data[0];
        console.log(settings);

        // get sizes
        let ncycles = settings.cyclesenabled.length;
        let nperiods = settings.perioddurations.length / ncycles;
        let nvalves = settings.valvestates.length / nperiods;

        // check sizes
        if(nvalves != settings.valvenames.length){
            mjsonrpc_error_alert(Error(`ODB array length mismatch: ncycles = CyclesEnabled.length = ${ncycles}, nperiods = PeriodDurations.length/ncycles = ${nperiods}, nvalves = ValveStates.length/nperiods (${nvalves}) = ValveNames.length (${settings.valvenames.length})`));
        }

        // update header rows span
        let header = document.getElementById("duration_header");
        header.colSpan = ncycles+1;

        header = document.getElementById("valve_header");
        header.colSpan = nvalves+1;

        // write header rows
        let row1 = table.insertRow();   // header labels
        let row2 = table.insertRow();   // checkboxes/names
        let row3 = table.insertRow();   // ids

        // blank column for headers
        let cell = row1.insertCell();
        cell = row2.insertCell();
        cell = row3.insertCell();
        cell.innerText = "";
        
        // cycle label
        cell = row1.insertCell();
        cell.innerText = 'Cycles';
        cell.colSpan = ncycles+1;
        cell.style.textAlign = 'center';

        // valve label
        cell = row1.insertCell();
        cell.innerText = 'Valves';
        cell.colSpan = nvalves+1;
        cell.style.textAlign = 'center';

        // cycle ids and checkboxes
        for(let cyclei=0; cyclei < ncycles; cyclei++){

            // checkbox
            cell = row2.insertCell();
            let div = document.createElement('input');
            div.type = "checkbox";
            div.className = "modbcheckbox";
            div.setAttribute('data-odb-path', `/Equipment/UCNSequencer26/Settings/CyclesEnabled[${cyclei}]`);
            div.setAttribute('data-odb-editable', `1`);
            cell.appendChild(div);
            cell.style.textAlign = 'center';

            // id
            cell = row3.insertCell();
            cell.innerText = cyclei;
            cell.style.textAlign = 'center';
        }

        // add/remove cycle buttons
        cell = row2.insertCell();
        cell.rowSpan = nperiods+2;
        cell.style.width = '40px';
        
        let brm = document.createElement("button");
        brm.innerText = '-';
        brm.onclick = rmcycle;
        brm.style.width = '20px';
        cell.appendChild(brm);
        
        let badd = document.createElement("button");
        badd.innerText = '+';
        badd.onclick = addcycle;
        badd.style.width = '20px';
        cell.appendChild(badd);
        
        // valve ids and names
        for(let valvei=0; valvei < nvalves; valvei++){
            
            // name
            cell = row2.insertCell();
            let div = document.createElement('div');
            div.className = "modbvalue";
            div.setAttribute('data-odb-path', `/Equipment/UCNSequencer26/Settings/ValveNames[${valvei}]`);
            div.setAttribute('data-odb-editable', `1`);
            cell.appendChild(div);
            cell.style.textAlign = 'center';

            // id
            cell = row3.insertCell();
            cell.innerText = valvei;
            cell.style.textAlign = 'center';
        }

        // add/remove valve buttons
        cell = row2.insertCell();
        cell.rowSpan = nperiods+2;
        cell.style.width = '40px';

        brm = document.createElement("button");
        brm.innerText = '-';
        brm.onclick = rmvalve;
        brm.style.width = '20px';
        cell.appendChild(brm);

        badd = document.createElement("button");
        badd.innerText = '+';
        badd.onclick = addvalve;
        badd.style.width = '20px';
        cell.appendChild(badd);
        
        // populate durations
        let cellidx_cycle = 0; // cell index in the list
        let cellidx_valve = 0; // cell index in the list
        for(let periodi=0; periodi < nperiods; periodi++){

            row = table.insertRow();

            // header column
            cell = row.insertCell();
            cell.innerText = `Period ${periodi}`;

            // cell timings
            for(let cyclei=0; cyclei < ncycles; cyclei++){
                cell = row.insertCell();
                let div =  document.createElement('div');
                div.className = "modbvalue";
                div.setAttribute('data-odb-path', 
                    `/Equipment/UCNSequencer26/Settings/PeriodDurations[${cellidx_cycle++}]`);
                div.setAttribute('data-odb-editable', `1`);
                cell.appendChild(div);
                cell.style.textAlign = 'center';     
            }

            // valve states
            for(let valvei=0; valvei < nvalves; valvei++){
                cell = row.insertCell();
                let div =  document.createElement('input');
                div.type = "checkbox";
                div.className = "modbcheckbox";
                div.setAttribute('data-odb-path', 
                    `/Equipment/UCNSequencer26/Settings/ValveStates[${cellidx_valve++}]`);
                div.setAttribute('data-odb-editable', `1`);
                cell.appendChild(div);
                cell.style.textAlign = 'center';     
            }
        }

        // Cycle durations
        row = table.insertRow();
        cell = row.insertCell();
        cell.innerText = 'Total Duration'
        for(let cyclei=0; cyclei < ncycles; cyclei++){
            cell = row.insertCell();
            cell.id = `total_duration_${cyclei}`;
        }

        // add/remove periods
        cell = row.insertCell();

        brm = document.createElement("button");
        brm.innerText = '-';
        brm.onclick = rmperiod;
        brm.style.width = '20px';
        cell.appendChild(brm);

        badd = document.createElement("button");
        badd.innerText = '+';
        badd.onclick = addperiod;
        badd.style.width = '20px';
        cell.appendChild(badd);

    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });



}

/** expand cycle list by one */
function addcycle(){}

/** reduce cycle list by one */
function rmcycle(){}

/** expand valve list by one */
function addvalve(){}

/** reduce valve list by one */
function rmvalve(){}

/** expand period list by one */
function addperiod(){}

/** reduce period list by one */
function rmperiod(){}

/** Calculate the total duration of each cycle */
function setTotalDuration(){}