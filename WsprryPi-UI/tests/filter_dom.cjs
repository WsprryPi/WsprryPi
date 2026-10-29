'use strict';
// Minimal DOM adapter: production editor owns all validation and transitions.
class Node {
 constructor(doc,tag){this.ownerDocument=doc;this.tagName=tag;this.children=[];this.value='';this.textContent='';this.disabled=false;this.hidden=false;this.attributes={};}
 append(...children){for(const c of children){this.children.push(c);c.parent=this;}}
 replaceChildren(...children){this.children=[];this.append(...children);}
 setAttribute(k,v){this.attributes[k]=String(v);}
 getAttribute(k){return this.attributes[k]??null;}
 remove(){if(this.parent)this.parent.children=this.parent.children.filter(c=>c!==this);}
 focus(){this.ownerDocument.activeElement=this;}
}
module.exports=()=>{const doc={createElement(tag){return new Node(doc,tag);}},host=new Node(doc,'div');host.id='filter-selector-controls';
 const all=(n=host)=>[n,...n.children.flatMap(all)];
 const button=text=>all().find(n=>n.tagName==='button'&&n.textContent===text);
 const inputs=(label)=>all().filter(n=>n.tagName==='label'&&n.textContent===label).map(n=>n.children[0]);
 return {doc,host,all,button,inputs,change(label,value,index=0){const input=inputs(label)[index];if(!input)throw Error('Missing '+label);input.value=String(value);input.oninput();return input;},status:()=>all().find(n=>n.attributes.role==='status').textContent};};
