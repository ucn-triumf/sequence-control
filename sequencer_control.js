function update_seq_control(){

    mjsonrpc_db_get_values([`/Experiment/Edit on start/run with sequencer`,
                            "/Runinfo/State"]).then(function(rpc) {
        
        // extract data from rpc call
        let seq_status = rpc.result.data[0];
        let inrun = rpc.result.data[1] !== STATE_STOPPED;

        // Set status banner
        let cell = document.getElementById('seq_control_status');
        if(seq_status == 1){
            cell.classList.add("mgreen");
            cell.classList.remove("mred");
            cell.innerText = `Sequencer enabled`;
        } else {
            cell.classList.remove("mgreen");
            cell.classList.add("mred");
            cell.innerText = `Sequencer NOT enabled`;
        }

        // disable / enable trigger state
        if(inrun){
            document.getElementById("use_external_trigger").setAttribute("disabled", "");
        } else {
            document.getElementById("use_external_trigger").removeAttribute("disabled");
        }

    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}