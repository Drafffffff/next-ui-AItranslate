// Test report logic against a tiny DOM facade; this is not a browser or a
// layout/visual check. Execute the actual generated script and its handlers.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const html=fs.readFileSync(process.argv[2],'utf8');
const data=html.match(/<script id="data" type="application\/json">([\s\S]*?)<\/script>/)[1];
const script=html.match(/<script>\n([\s\S]*?)<\/script>/)[1];
class Element {
 constructor(tag){this.tag=tag;this.children=[];this.value='';this.textContent='';this.dataset={};this.flags=new Set();this.classList={add:x=>this.flags.add(x),toggle:(x,on)=>on?this.flags.add(x):this.flags.delete(x)};}
 append(...nodes){this.children.push(...nodes);}
 replaceChildren(...nodes){this.children=[...nodes];}
 setAttribute(name,value){this[name]=value;}
}
const elements=new Map([...html.matchAll(/id="([\w-]+)"/g)].map(m=>[m[1],new Element('div')]));
elements.get('data').textContent=data;elements.get('filter').value='all';
const nav=['overview','cases','compare','environment'].map(page=>Object.assign(new Element('button'),{dataset:{page}}));
const document={getElementById:id=>{assert(elements.has(id),'missing report element '+id);return elements.get(id);},createElement:tag=>new Element(tag),createElementNS:(_,tag)=>new Element(tag),createTextNode:t=>Object.assign(new Element('#text'),{textContent:t}),querySelectorAll:q=>{assert.equal(q,'nav button');return nav;}};
vm.runInNewContext(script,{document,Node:Element,console,URL,Blob,setTimeout},{timeout:3000});
const parsed=JSON.parse(data),list=elements.get('case-list');
assert.equal(list.children.length,parsed.cases.length,'all cases should render');
for(const button of nav){button.onclick();for(const page of nav)assert.equal(elements.get(page.dataset.page).flags.has('hidden'),page!==button);}
elements.get('filter').value='fail';elements.get('filter').onchange();
const failures=parsed.cases.filter(c=>!c.machine_pass).length;
assert.equal(list.children.filter(e=>e.tag==='article').length,failures,'failure filter');
elements.get('filter').value='all';elements.get('search').value=parsed.cases[0].id;elements.get('search').oninput();
assert.equal(list.children.filter(e=>e.tag==='article').length,parsed.cases.filter(c=>c.id===parsed.cases[0].id).length,'case search');
elements.get('search').value='no-such-case-xyz';elements.get('search').oninput();assert.equal(list.children[0].textContent,'没有匹配案例。');
console.log('PASS: generated report script, navigation, failure filter, case search and empty state');
