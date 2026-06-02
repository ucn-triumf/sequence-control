function update_seq_status(){

    // request info from ODB
    let req = [mjsonrpc_make_request('cm_exist', {"name":NAME}),
               mjsonrpc_make_request('db_get_values', {"paths":[`/Equipment/${NAME}/Settings`]}),
              ];

    // batch rpc request 
    mjsonrpc_send_request(req).then(function(rpc) {
        
        // extract data from rpc call
        let seq_status = rpc[0].result.status;
        let settings = rpc[1].result.data[0];

        // Set status banner
        let cell = document.getElementById('seq_status');
        if(seq_status){
            cell.classList.add("mgreen");
            cell.innerText = `${NAME} frontend is running`;
        } else {
            cell.classList.add("mred");
            cell.innerText = `${NAME} frontend is NOT running`;
        }

        /* TODO: Most of these should be set from useq bank */

        // set in-cycle

        // set cycle #

        // set supercycle #

        // set time until cycle end
        
        // set time until supercycle end

        // set supercycle duration



    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}