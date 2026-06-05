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
            if(key.includes("/key") || key == "end_of_run_comment"){
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
            odb_value.id = `edit_on_start_${key}`;
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
            button1.onclick = () => stop_run();
            cell.appendChild(button1);
            
            // button2.innerText = 'Pause Run';
            // button1.onclick = () => mhttpd_pause_run('&Return=custom&page=Sequencer26');
            // cell.appendChild(button2);
            
            // banner
            banner.innerHTML = `Run ${runinfo["run number"]} in progress`;
            banner.classList.add("mgreen");
            banner.classList.remove("mred");
            banner.classList.remove("myellow");

            // disable write / run with seq
            let check = document.getElementById('edit_on_start_write data');
            check.setAttribute('disabled', '');
            check = document.getElementById('edit_on_start_run with sequencer');
            check.setAttribute('disabled', '');

        } else if (runinfo.state === STATE_PAUSED) {
            
            // button
            button1.innerText = 'Stop Run';
            button1.onclick = () => stop_run();
            cell.appendChild(button1);
            
            banner.innerHTML = `Run ${runinfo["run number"]} paused`;
            banner.classList.add("myellow");
            banner.classList.remove("mred");
            banner.classList.remove("mgreen");

            // button2.innerText = 'Resume Run';
            // button1.onclick = () => mhttpd_resume_run('&Return=custom&page=Sequencer26');
            // cell.appendChild(button2);

            // disable write / run with seq
            let check = document.getElementById('edit_on_start_write data');
            check.setAttribute('disabled', '');
            check = document.getElementById('edit_on_start_run with sequencer');
            check.setAttribute('disabled', '');
            
        } else if (runinfo.state === STATE_STOPPED) {
            
            // button
            button1.innerText = 'Start Run';
            button1.onclick = () => start_run(true);
            cell.appendChild(button1);

            banner.innerHTML = `Run ${runinfo["run number"]} finished`;
            banner.classList.add("mred");
            banner.classList.remove("mgreen");
            banner.classList.remove("myellow");

            // enable write / run with seq
            let check = document.getElementById('edit_on_start_write data');
            check.removeAttribute('disabled');
            check = document.getElementById('edit_on_start_run with sequencer');
            check.removeAttribute('disabled');
        }

    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}

/** Start run, confirming run title and experiment number */
async function start_run(isok, param){

    // get run parameters
    if(param === undefined){
        let rpc = await mjsonrpc_db_get_values(['/Experiment/Edit on start',
                                                '/Runinfo']);
        edit_on_start = rpc.result.data[0];
        edit_on_start["run number"] = rpc.result.data[1]["run number"];
        var id = 0;
    } else {
        var edit_on_start = param[0];
        var id = param[1];
    }

    if(isok){
        switch(id){
            // confirm title
            case 0:
                dlgConfirm(`Confirm RUN TITLE: "${edit_on_start['run title']}"`,
                    start_run,
                    [edit_on_start, 1]);
                break;
            
            // confirm exp
            case 1:

                // check if write data false
                let next_id = edit_on_start["write data"] ? 3 : 2;

                dlgConfirm(`Confirm EXPERIMENT NUMBER: "${edit_on_start['experiment number']}"`,
                    start_run,
                    [edit_on_start, next_id]);
                break;

            // confirm write data
            case 2: 
                dlgConfirm(`Continue with NO WRITTEN DATA?`,
                    start_run,
                    [edit_on_start, 3]);
                break;

            // start the run
            // logic borrowed from start.html
            case 3:

                // transition parameters
                let tr_params = {};
                tr_params.transition = "TR_START";
                if(edit_on_start['run number'])
                    tr_params.run_number = parseInt(edit_on_start['run number'])+1;
                
                // initiate transition to run start
                mjsonrpc_call("cm_transition", tr_params).then(function (rpc){
                    if (rpc.result.status != 1) {
                        throw new Error("Cannot start run, cm_transition() status " + rpc.result.status + ", see MIDAS messages");
                    }
                    mhttpd_goto_page("Transition", '&Return=custom&page=Sequencer26');
                    
                }).catch(function (error) {
                    document.getElementById("dlgErrorText").innerHTML = mjsonrpc_decode_error(error);
                    dlgShow('dlgError');
                });
        }
    }
}

/** stop run with end-of-run comment prompt */
async function stop_run(){
    dlgQuery("Enter end-of-run comment:", "", 
        async (val)=>{
            if(val != false || val == ""){
                await mjsonrpc_db_paste(["/Experiment/Edit on start/end_of_run_comment"], [val]);
                mhttpd_stop_run('&Return=custom&page=Sequencer26');
            }
        });
}
