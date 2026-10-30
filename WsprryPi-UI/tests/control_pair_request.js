// SPDX-License-Identifier: MIT
// Current request factory for synthetic pair commands. Actual UI producers must
// emit their own contract; this helper asserts it rather than repairing them.
'use strict';
const assert=require('node:assert/strict');
module.exports=command=>{
 const mutation=command.method && command.method!=='GET';
 if(!mutation || command.omit_contract || !command.body || !(command.path?.startsWith('/api/control/v1/') || command.raw_resource))return command;
 let body;try{body=JSON.parse(command.body);}catch{return command;}
 if(!body || typeof body!=='object' || Array.isArray(body))return command;
 if(command.headers)assert.equal(body.contract,'Wsprry-Control/1','Actual shipped UI producer must emit the current control contract');
 else if(!Object.hasOwn(body,'contract'))return {...command,body:JSON.stringify({...body,contract:'Wsprry-Control/1'})};
 return command;
};
