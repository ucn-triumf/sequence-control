function update_seq_status(){

    // request info from ODB
    let req = [mjsonrpc_make_request('cm_exist', {"name":"fe_ucnsequencer"}),
               mjsonrpc_make_request('db_get_values', {"paths":[`/Equipment/${NAME}/Settings`,
                                                                `/Equipment/${NAME}/Variables`,
                                                                '/Runinfo/State']}),
              ];

    // batch rpc request 
    mjsonrpc_send_request(req).then(function(rpc) {
        
        // extract data from rpc call
        let seq_status = rpc[0].result.status;
        let settings = rpc[1].result.data[0];
        let variables = rpc[1].result.data[1];
        let run_state = rpc[1].result.data[2];

        let seqc = variables['seqc'].map(Number);
        let cycle_start = seqc[0];
        let incycle = seqc[2];
        let current_cycle = seqc[3];

        // Set status banner
        let cell = document.getElementById('seq_status');
        if(seq_status == 1){
            cell.classList.add("mgreen");
            cell.classList.remove("mred");
            cell.innerText = `${NAME} frontend is running`;
        } else {
            cell.classList.remove("mgreen");
            cell.classList.add("mred");
            cell.innerText = `${NAME} frontend is NOT running`;
        }

        // not in cycle: clear some stuff
        if(run_state != 3 || incycle != 1){
            document.getElementById('incycle').innerText = 'No';
            document.getElementById('timeleftincycle').innerText = '';    
            document.getElementById('timeleftinsupercycle').innerText = '';    
            return;
        } 
        
        // we are in-cycle... set the real values
        document.getElementById('incycle').innerText = 'Yes';

        // set time until cycle end
        let cycle_dur = document.getElementById(`total_duration_${current_cycle}`).innerText;
        let now = Date.now()/1000;
        let cycle_elapsed = now - cycle_start;
        document.getElementById('timeleftincycle').innerText = `${Math.round(cycle_dur - cycle_elapsed)} sec`;
        
        // update period number
        let ncycles = settings['cyclesenabled'].length
        let period_elapsed = 0;
        for(let i=current_cycle; i<settings['perioddurations'].length; i+=ncycles){
            
            // TODO: finish implmeenting
            if(period_elapsed > cycle_elapsed){
                console.log(`period  ${(i-current_cycle)/ncycles}`)
                break;
            }
            
            period_elapsed += settings['perioddurations'][i];
        }



        
        // set time until supercycle end
        
        // set supercycle duration
        let super_dur = 0;
        for(let cyclei=0; cyclei < NCYCLES; cyclei++){
            let cell = document.getElementById(`total_duration_${cyclei}`);
            super_dur += parseInt(cell.innerText);
        }
        document.getElementById('supercycledur').innerText = super_dur;

    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}