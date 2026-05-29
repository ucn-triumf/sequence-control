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

        // run update
        update_run_control();
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
