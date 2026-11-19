// SPDX-License-Identifier: MIT
(function(root){
    'use strict';
    const node=id=>root.document.getElementById(id);
    let current=null,visible=true,handoff=null;
    try{handoff=JSON.parse(root.sessionStorage.getItem('wsprrypi.fleet.editor.handoff')||'null');}catch(_){/* A malformed navigation hint grants no authority. */}
    function render(state){
        current=state;
        if(!node('fleet-output-list'))return;
        const list=node('fleet-output-list');list.setAttribute('aria-busy',String(state.busy));
        node('fleet-schedule-refresh').disabled=state.busy;
        node('fleet-schedule-feedback').textContent=!state.valid?'Saved state is unavailable or stale. No cleanup or retirement is inferred.':state.error||'';
        list.replaceChildren();
        if(!state.data)return;
        if(handoff && state.data.assignments.some(row=>row.device_id===handoff.device_id)){
            const row=state.data.assignments.find(row=>row.device_id===handoff.device_id);
            root.ControllerRetirement?.select(row.device_id,row.name);handoff=null;
        }
        if(!state.data.assignments.length){const text=root.document.createElement('p');text.className='form-text';text.textContent='No saved legacy controller schedules.';list.append(text);return;}
        for(const row of state.data.assignments){
            const section=root.document.createElement('section');section.className='mt-3';
            const title=root.document.createElement('h5');title.className='cw-control-section__title';title.textContent=row.name;
            const identity=root.document.createElement('p');identity.className='form-text text-break';identity.textContent=`${row.product} · ${row.device_id}`;
            const status=root.document.createElement('p');status.className='form-text';
            const observed=state.data.outputs[row.device_id];status.textContent=observed?.state==='retired'?'Controller retirement confirmed.':observed?.message||'Migration required. Output and ownership remain unconfirmed.';
            const saved=root.document.createElement('p');saved.className='form-text';saved.textContent=`Saved legacy enable: ${row.enabled?'yes':'no'}; accepted work recorded: ${row.in_flight?'yes':'no'}. Entries remain held and do not describe current device output.`;
            const receipt=root.document.createElement('button');receipt.type='button';receipt.className='btn btn-outline-secondary';receipt.textContent='View retirement';receipt.addEventListener('click',()=>root.ControllerRetirement?.select(row.device_id,row.name));
            section.append(title,identity,status,saved,receipt);list.append(section);
        }
    }
    async function openAssignedEditor(id){
        const row=current?.data?.assignments.find(value=>value.device_id===id);
        if(!row)throw Error('Read the saved legacy state before reviewing this target.');
        root.ControllerRetirement?.select(id,row.name);node('fleet-schedules').scrollIntoView({block:'start'});
    }
    function setVisible(value){visible=!!value;if(node('fleet-schedules')){node('fleet-schedules').hidden=!visible;node('fleet-schedules').inert=!visible;}}
    function init(){if(!node('fleet-schedules'))return;node('fleet-schedule-refresh').addEventListener('click',()=>root.FleetClient.refresh());root.FleetClient.subscribe(render);}
    root.FleetSchedules={setVisible,openAssignedEditor};
    if(root.document.readyState==='loading')root.document.addEventListener('DOMContentLoaded',init);else init();
})(window);
