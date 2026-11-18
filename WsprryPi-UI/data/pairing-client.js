// SPDX-License-Identifier: MIT
(function(root){
    'use strict';
    const control=root.WsprryControl||(typeof require==='function'?require('./control-contract.js'):null);
    const documents=root.ConfigurationRecoveryClient||(typeof require==='function'?require('./configuration-recovery-client.js'):null);
    const identity=value=>typeof value==='string'&&/^[0-9a-f]{32}$/.test(value);
    function create({fetch=root.fetch.bind(root),crypto=root.crypto,base='/api/control/v1',storage}={}){
        if(!/^\/(?:[a-zA-Z0-9_.~-]+\/)*api\/control\/v1$/.test(base)||base.length>96||base.split('/').some(value=>value==='.'||value==='..'))throw Error('invalid_base');
        const path=base+'/host/pairing',key='wsprrypi.pairing.pending:'+base;
        let token='',sending=false,pending=null;
        if(storage===undefined)try{storage=root.localStorage;}catch(_){}
        const valid=value=>value&&typeof value==='object'&&Object.keys(value).sort().join(',')==='device,kind,operation'&&
            identity(value.device)&&identity(value.operation)&&['connect','remove'].includes(value.kind);
        try{const text=storage?.getItem(key)||'null';const value=text.length<=512?JSON.parse(text):null;if(valid(value))pending=value;else storage?.removeItem(key);}catch(_){}
        const remember=value=>{pending=value;try{if(value)storage?.setItem(key,JSON.stringify(value));else storage?.removeItem(key);}catch(_){}};
        const request=(url,options={})=>fetch(url,{credentials:'same-origin',cache:'no-store',redirect:'error',signal:AbortSignal.timeout(30000),...options});
        const document=(response,family)=>documents.readControllerDocument(response,family);
        const id=()=>Array.from(crypto.getRandomValues(new Uint8Array(16)),value=>value.toString(16).padStart(2,'0')).join('');
        async function session(){
            const response=await request(base+'/host/management-session',{method:'POST',headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1'},
                body:JSON.stringify({contract:control.contract,schema:'controller-browser-session'})});
            await document(response,'controller-browser-session');
            const next=response.headers.get('X-WsprryPi-CSRF');if(!/^[0-9a-f]{64}$/.test(next||''))throw Error('browser_session_unavailable');
            token=next;return token;
        }
        async function post(kind,body,family,device){
            if(sending)throw Error('operation_pending');sending=true;
            try{
                const csrf=await session();const operation=id();
                if(kind!=='window')remember({kind,device,operation});
                const response=await request(path+'/'+kind,{method:'POST',headers:{'Content-Type':'application/json','X-WsprryPico-Request':'1','X-WsprryPi-CSRF':csrf},
                    body:JSON.stringify({contract:control.contract,...body,operation_id:operation})});
                const result=await document(response,family);
                if(family==='target-operation'){
                    if(result.operation_id!==operation||result.binding.device_id!==device)throw Error('invalid_response');
                    if(['committed','rejected'].includes(result.state))remember(null);
                }
                return result;
            }catch(error){if(error.code==='csrf_failed')token='';throw error;}
            finally{sending=false;}
        }
        return Object.freeze({
            state:async()=>document(await request(path),'pairing-state'),
            discovery:async()=>document(await request(path+'/discovery'),'pair-discovery'),
            pending:()=>pending?Object.freeze({...pending}):null,
            window(state){
                if(!control.validControllerDocument(state,'pairing-state')||state.state==='unavailable')throw Error('authority_pending');
                const action=state.state==='paired'?'replace':state.state==='access_recovery'?'recover':'pair';
                return post('window',{schema:'pair-window',action,expected_authorization_generation:state.authorization_generation},'pair-window-response',state.device_id);
            },
            remove(state){
                if(!control.validControllerDocument(state,'pairing-state')||state.state!=='paired')throw Error('authority_pending');
                return post('remove',{schema:'pair-remove',expected_authorization_generation:state.authorization_generation},'target-operation',state.device_id);
            },
            connect(discovery,code){
                if(!identity(discovery?.discovery_id)||!identity(discovery?.device_id))throw Error('target_context_unavailable');
                const normalized=String(code).replace(/[ -]/g,'').replace(/[a-z]/g,value=>value.toUpperCase());
                if(!/^[0123456789ABCDEFGHJKMNPQRSTVWXYZ]{8}$/.test(normalized))throw Error('invalid_code');
                return post('connect',{schema:'pair-connect',discovery_id:discovery.discovery_id,expected_device_id:discovery.device_id,code:normalized},'target-operation',discovery.device_id);
            },
            async check(){
                if(!pending)throw Error('operation_unknown');const captured={...pending};
                const result=await document(await request(path+'/operations/'+captured.operation),'target-operation');
                if(result.operation_id!==captured.operation||result.binding.device_id!==captured.device)throw Error('invalid_response');
                if(['committed','rejected'].includes(result.state)&&pending?.operation===captured.operation)remember(null);
                return result;
            }
        });
    }
    const api=Object.freeze({create});root.PairingClient=api;if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof window==='undefined'?globalThis:window);
