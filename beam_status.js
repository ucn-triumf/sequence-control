function update_beam_status(){

    let cell = document.getElementById('beamline_status');

    // path to epics logging equipment
    mjsonrpc_db_get_values([`/Equipment/${BEAMLINE_EPICS}/Settings/Names`,
                            `/Equipment/${BEAMLINE_EPICS}/Variables/Measured`]).then(function(rpc) {
        
        // get data, indexed by name
        let data = rpc.result.data
        let beam = {};
        for(let i=0; i<data[0].length; i++){
            beam[data[0][i]] = data[1][i];
        }

        // Set status banner
        let cell = document.getElementById('beamline_status');
        if(beam['B1U:SEPT:STATON'] && beam['B1U:B0:STATON']){
            cell.classList.add("mgreen");
            cell.classList.remove("mred");
            cell.innerText = 'UCN beamline enabled (septum \& B0 are on)';
        } else {
            cell.classList.add("mred");
            cell.classList.remove("mgreen");
            cell.innerText = 'UCN beamline disabled (septum or B0 are off)';
        }

        // Set values

        // BL1A current
        cell = document.getElementById('1vextractcur');
        cell.innerText = `${parseFloat(beam["B1:FOIL:ADJCUR"]).toFixed(2)} uA`;
        
        // BL1U current
        cell = document.getElementById('predictedbeamcur');
        cell.innerText = `${parseFloat(beam["B1V:KSM:PREDCUR"]).toFixed(2)} uA`;
        
        // beam on time
        cell = document.getElementById('beamontime');
        let ontime = parseFloat(beam["B1V:KSM:RDBEAMON.VAL1"]) * 0.000888111;
        cell.innerText = `${ontime.toFixed(2)} sec`;
        
        // beam off time
        cell = document.getElementById('beamofftime');
        let offtime = parseFloat(beam["B1V:KSM:RDBEAMOFF.VAL1"]) * 0.000888111;
        cell.innerText = `${offtime.toFixed(2)} sec`;

        // ksm status
        cell = document.getElementById('ksm_status');
        if(beam['B1V:KSM:INSEQ'] == 1){
            cell.innerText = "In ON/OFF sequence";
        } else {
            cell.innerText = "Not in ON/OFF sequence";
        }
        
        // beam on status
        cell = document.getElementById('beam_on_status');
        if(beam['B1V:KSM:BONPRD']){
            cell.innerText = "Being irradiated";
            cell.style.color = 'blue';
            cell.className = 'blink';
        } else {
            cell.innerText = "Not being irradiated";
            cell.style.color = 'black';
            cell.className = '';
        }
    }).catch(function(error) {
        mjsonrpc_error_alert(error);
    });

}