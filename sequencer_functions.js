
// Function to make a sh script version of what is in ODB
function create_script(){

  // Start by resetting all variables...
  var script = "# Script for setting complicated cycle\n"
  script +=    "# Note, when setting ODB variables you need to remember that indexes start with zero.\n"
  script +=    "# Reset all the durations and valves states\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod1[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod2[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod3[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod4[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod5[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod6[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod7[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod8[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod9[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod10[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve1State[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve2State[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve3State[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve4State[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve5State[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve6State[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve7State[*] 0'\n"
  script +=    "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve8State[*] 0'\n"

  // Now make XHR request and fill in the non-zero values
  mjsonrpc_send_request([
    mjsonrpc_make_request("db_get_values",{"paths":["/Equipment/UCNSequencer2018/Settings"]})
  ]).then(function (rpc) {         
         
    var seq_var = rpc[0].result.data[0];
    console.log(seq_var.numberperiodsincycle);

    script += "#Set new variables\n"
    script += "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/numberPeriodsInCycle "+seq_var.numberperiodsincycle+"'\n";
    script += "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/numCyclesInSuper "+seq_var.numcyclesinsuper+"'\n";

    var i,j;
    for(i = 0; i < 10; i++){
      var period = i + 1;
      for(j = 0; j < 20; j++){        
        var value = seq_var["durationtimeperiod"+period][j];
        if(value != 0) script += "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/DurationTimePeriod"+period+"["+j+"] "+value+"'\n"; 
      }
      for(j = 0; j < 8; j++){
        var valve = j + 1;
        var value = seq_var["valve"+valve+"state"][i];
        if(value != 0) script += "odbedit -c 'set /Equipment/UCNSequencer2018/Settings/Valve"+valve+"State["+i+"] "+value+"'\n"; 
      }
    }
    
    // Make blob and make link to it...
    var blob = new Blob([script], {type: "text/plain;charset=utf-8"});
    var blobUrl = URL.createObjectURL(blob);
    var link = document.getElementById("downloadlink"); // Or maybe get it from the current document
    link.style.display = "block";
    link.href = blobUrl;
    link.download = "sequencer.sh";
    link.innerHTML = "Click here to download the file";

  }).catch(function(error) {
    if (error.request) {
      var s = mjsonrpc_decode_error(error);
      console.log("mjsonrpc_error_alert: " + s);
    } else {
      console.log("mjsonroc_error_alert: " + error);
    }
  });


}
   
