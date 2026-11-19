// SPDX-License-Identifier: MIT
(function(root){
    'use strict';
    const node=id=>root.document.getElementById('controllerRetirement'+id);
    if(!node(''))return;
    const client=root.ConfigurationRecoveryClient.create({base:root.WSPRRYPI_PATHS?.sharedApiPath||'/api/control/v1'});
    let target=null,name='',busy=false,epoch=0,closed=false,confirmed=false;
    async function read(){
        if(!target||busy||closed)return;
        const selected=target,serial=epoch;busy=true;node('Refresh').disabled=true;
        node('Feedback').textContent='Reading the saved retirement receipt…';
        try{
            const response=await client.retirements();
            if(response.etag!==`"${response.data.revision}"`||!root.WsprryControl.validControllerDocument(response.data,'controller-retirements'))throw Error('Retirement history could not be verified.');
            if(serial!==epoch||selected!==target)return;
            const row=response.data.assignments.find(value=>value.target_id===selected);
            confirmed=row?.state==='retired';
            node('Feedback').textContent=confirmed?`${name}: retirement is confirmed. This controller cannot resume that saved schedule.`:row?`${name}: ${row.state}. Use WTP-capable software to finish outstanding retirement and confirm inactive, unowned output. A missing or pending receipt is not cleanup evidence.`:'No matching retirement receipt is available. Preserve the original controller history and reconcile with WTP-capable software.';
        }catch(error){if(serial===epoch){confirmed=false;node('Feedback').textContent=error.message+' No retirement is inferred.';}}
        finally{busy=false;node('Refresh').disabled=closed;if(serial!==epoch&&target&&!closed)void read();}
    }
    node('Refresh').addEventListener('click',()=>void read());
    root.addEventListener('pagehide',()=>{closed=true;++epoch;node('Refresh').disabled=true;});
    root.addEventListener('pageshow',()=>{closed=false;if(target)void read();});
    root.ControllerRetirement=Object.freeze({get fenced(){return !confirmed;},select(id,label=''){target=/^[0-9a-f]{32}$/.test(id)?id:null;name=String(label);++epoch;confirmed=false;node('').hidden=!target;if(target)void read();}});
})(window);
