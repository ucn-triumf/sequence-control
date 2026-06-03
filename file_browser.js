/**
 * File browser popup for saving/loading sequencer setting presets.
 *
 * Presets are stored in the ODB under /Equipment/UCNSequencer26/Presets/<name>.
 * Only the configuration keys (timings, valve states) are saved — not run status.
 */

// ODB path where presets are stored
const PRESET_ODB_PATH = `/Equipment/${NAME}/Presets`;

// ODB keys that define the sequencer configuration (lowercase = how MIDAS returns them)
const PRESET_KEYS = ['cyclesenabled', 'perioddurations', 'periodsenabled', 'valvenames', 'valvestates'];

// TID (type ID) constants from midas.js, needed for db_create
const _TID_BOOL   = 8;
const _TID_DOUBLE = 10;
const _TID_STRING = 12;

// ODB type and string_length for each preset key
const PRESET_KEY_TYPES = {
    cyclesenabled:   {tid: _TID_BOOL},
    perioddurations: {tid: _TID_DOUBLE},
    periodsenabled:  {tid: _TID_BOOL},
    valvenames:      {tid: _TID_STRING, string_length: 64},
    valvestates:     {tid: _TID_BOOL},
};


/** Open the save preset dialog */
function show_save_dialog() {
    _show_file_dialog(true);
}

/** Open the load preset dialog */
function show_load_dialog() {
    _show_file_dialog(false);
}


/** Build and display the file browser modal.
 *
 * Args:
 *     is_save: true for save mode, false for load mode
 */
function _show_file_dialog(is_save) {
    document.getElementById('fb_overlay')?.remove();

    // Full-screen translucent backdrop
    let overlay = document.createElement('div');
    overlay.id = 'fb_overlay';
    Object.assign(overlay.style, {
        position: 'fixed', top: '0', left: '0',
        width: '100%', height: '100%',
        background: 'rgba(0,0,0,0.5)', zIndex: '1000',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
    });
    overlay.addEventListener('click', (e) => { if (e.target === overlay) overlay.remove(); });

    // Dialog box
    let dialog = document.createElement('div');
    Object.assign(dialog.style, {
        background: 'white', border: '2px solid #555',
        padding: '20px', minWidth: '360px', maxWidth: '480px',
        boxShadow: '0 4px 16px rgba(0,0,0,0.4)',
    });

    // Title
    let title = document.createElement('h3');
    Object.assign(title.style, { margin: '0 0 12px 0' });
    title.innerText = is_save ? 'Save Preset' : 'Load Preset';
    dialog.appendChild(title);

    // Preset list
    let list_label = document.createElement('div');
    list_label.innerText = 'Saved presets:';
    Object.assign(list_label.style, { fontWeight: 'bold', marginBottom: '4px' });
    dialog.appendChild(list_label);

    let file_list = document.createElement('div');
    file_list.id = 'fb_list';
    Object.assign(file_list.style, {
        border: '1px solid #bbb', height: '160px',
        overflowY: 'auto', background: '#f8f8f8',
        marginBottom: '12px', padding: '2px',
        fontFamily: 'monospace', fontSize: '13px',
    });
    file_list.innerText = 'Loading...';
    dialog.appendChild(file_list);

    // Preset name input
    let input_label = document.createElement('label');
    input_label.innerText = is_save ? 'Save as:' : 'Selected preset:';
    Object.assign(input_label.style, { fontWeight: 'bold', display: 'block', marginBottom: '4px' });
    dialog.appendChild(input_label);

    let name_input = document.createElement('input');
    name_input.id = 'fb_name_input';
    name_input.type = 'text';
    name_input.placeholder = is_save ? 'Enter preset name' : '(select a preset above)';
    name_input.readOnly = !is_save;
    Object.assign(name_input.style, {
        width: '100%', boxSizing: 'border-box',
        padding: '4px', marginBottom: '14px',
        background: is_save ? 'white' : '#f0f0f0',
    });

    // Allow clicking a saved preset to populate the save name field too
    dialog.appendChild(name_input);

    // Action buttons
    let btn_row = document.createElement('div');
    btn_row.style.textAlign = 'right';

    let cancel = document.createElement('button');
    cancel.innerText = 'Cancel';
    cancel.style.marginRight = '10px';
    cancel.onclick = () => overlay.remove();
    btn_row.appendChild(cancel);

    let action = document.createElement('button');
    action.innerText = is_save ? 'Save' : 'Load';
    action.onclick = () => {
        let preset_name = name_input.value.trim();
        if (!preset_name) {
            alert(is_save ? 'Enter a preset name.' : 'Select a preset to load.');
            return;
        }
        let p = is_save ? _save_preset(preset_name) : _load_preset(preset_name);
        p.then(() => overlay.remove()).catch(err => mjsonrpc_error_alert(err));
    };
    btn_row.appendChild(action);
    dialog.appendChild(btn_row);

    overlay.appendChild(dialog);
    document.body.appendChild(overlay);

    _populate_preset_list(file_list, name_input);
}


/** Fetch the preset list from ODB and render it in the file browser.
 *
 * Args:
 *     container: DOM element to fill with preset name rows
 *     name_input: input whose value is set when a preset is clicked
 */
function _populate_preset_list(container, name_input) {
    mjsonrpc_db_ls([PRESET_ODB_PATH]).then(function(rpc) {
        container.innerHTML = '';

        // Path missing or empty
        if (!rpc.result.status[0] || !rpc.result.data[0]) {
            container.innerText = '(no presets saved yet)';
            return;
        }

        let data = rpc.result.data[0];

        // ODB ls returns child names plus metadata entries like "name/key" — keep only clean names
        let names = Object.keys(data).filter(k => !k.includes('/')).sort();
        if (names.length === 0) {
            container.innerText = '(no presets saved yet)';
            return;
        }

        for (let preset_name of names) {
            let row = document.createElement('div');
            row.innerText = preset_name;
            Object.assign(row.style, {
                padding: '3px 6px', cursor: 'pointer',
                borderBottom: '1px solid #e0e0e0',
            });
            row.addEventListener('mouseover', () => { row.style.background = '#e8e8ff'; });
            row.addEventListener('mouseout', () => {
                row.style.background = (name_input.value === preset_name) ? '#c0c0ff' : '';
            });
            row.addEventListener('click', () => {
                container.querySelectorAll('div').forEach(el => el.style.background = '');
                row.style.background = '#c0c0ff';
                name_input.value = preset_name;
            });
            container.appendChild(row);
        }

    }).catch(() => {
        container.innerText = '(no presets saved yet)';
    });
}


/** Read current settings from ODB and write them as a named preset.
 *
 * Args:
 *     preset_name: the preset name (used as the ODB subdirectory name)
 */
async function _save_preset(preset_name) {
    let rpc = await mjsonrpc_db_get_values([`/Equipment/${NAME}/Settings`]);
    let all_settings = rpc.result.data[0];

    let preset_base = `${PRESET_ODB_PATH}/${preset_name}`;
    let create_items = [];
    let paste_paths = [];
    let paste_values = [];

    for (let key of PRESET_KEYS) {
        if (!(key in all_settings)) continue;
        let val = all_settings[key];
        let info = PRESET_KEY_TYPES[key];

        // db_create requires {path, type, array_length} objects — not plain strings
        let item = {
            path: `${preset_base}/${key}`,
            type: info.tid,
            array_length: Array.isArray(val) ? val.length : 1,
        };
        if (info.string_length) item.string_length = info.string_length;

        create_items.push(item);
        paste_paths.push(item.path);
        paste_values.push(val);
    }

    // Create ODB keys; status 311 means the key already exists, fine for overwriting
    let create_rpc = await mjsonrpc_db_create(create_items);
    for (let i = 0; i < create_rpc.result.status.length; i++) {
        let s = create_rpc.result.status[i];
        if (s !== 1 && s !== 311)
            throw new Error(`db_create failed for ${create_items[i].path}, status ${s}`);
    }

    await mjsonrpc_db_paste(paste_paths, paste_values);
}


/** Load a named preset from ODB and apply it to the live Settings, resizing arrays as needed.
 *
 * Args:
 *     preset_name: the preset name to restore
 */
async function _load_preset(preset_name) {
    // Fetch each preset key by its individual leaf path (matching how _save_preset stores them)
    let preset_base = `${PRESET_ODB_PATH}/${preset_name}`;
    let rpc = await mjsonrpc_db_get_values(PRESET_KEYS.map(k => `${preset_base}/${k}`));

    let preset = {};
    for (let i = 0; i < PRESET_KEYS.length; i++) {
        if (rpc.result.data[i] !== null) preset[PRESET_KEYS[i]] = rpc.result.data[i];
    }
    if (!preset.cyclesenabled) throw new Error(`Preset "${preset_name}" not found in ODB.`);

    // Compute array dimensions from the preset
    let ncycles = preset.cyclesenabled.length;
    let nperiods = preset.perioddurations.length / ncycles;
    let nvalves = preset.valvenames.length;

    // Resize Settings arrays to match preset before writing (avoids ODB size mismatch errors)
    let base = `/Equipment/${NAME}/Settings`;
    await mjsonrpc_db_resize(
        [`${base}/CyclesEnabled`, `${base}/PeriodsEnabled`, `${base}/ValveNames`,
         `${base}/PeriodDurations`, `${base}/ValveStates`],
        [ncycles, nperiods, nvalves, ncycles * nperiods, nvalves * nperiods]
    );

    // Write preset values into Settings using leaf paths
    let set_paths = PRESET_KEYS.filter(k => k in preset).map(k => `${base}/${k}`);
    let set_values = PRESET_KEYS.filter(k => k in preset).map(k => preset[k]);
    let req = mjsonrpc_make_request('db_paste', {paths: set_paths, values: set_values});
    await mjsonrpc_send_request([req]);

    // Redraw the timing/valve table
    populate_timings();
}
