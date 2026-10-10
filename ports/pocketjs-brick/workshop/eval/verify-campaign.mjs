import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const html=fs.readFileSync(process.argv[2],'utf8');
const data=html.match(/<script id="data" type="application\/json">([\s\S]*?)<\/script>/)[1];
const script=html.match(/<script>\n([\s\S]*?)<\/script>/)[1];
class Element{constructor(tag){this.tag=tag;this.children=[];this.value='';this.events={};this.style={};this.textContent='';}append(...x){this.children.push(...x)}replaceChildren(...x){this.children=[...x]}addEventListener(n,f){this.events[n]=f}}
const elements=new Map([...html.matchAll(/id="([\w-]+)"/g)].map(x=>[x[1],new Element('div')]));elements.get('data').textContent=data;
const document={getElementById:id=>elements.get(id),createElement:t=>new Element(t),createTextNode:t=>Object.assign(new Element('#text'),{textContent:t})};
vm.runInNewContext(script,{document},{timeout:3000});
const parsed=JSON.parse(data),root=elements.get('rounds');assert.equal(root.children.length,parsed.rounds.length);
elements.get('round').value=String(parsed.rounds[0].round);elements.get('round').events.change();assert.equal(root.children.length,1);
elements.get('result').value='fail';elements.get('result').events.change();
const cases=root.children[0].children.filter(x=>x.className==='case');assert.equal(cases.length,(parsed.rounds[0].cases??[]).filter(c=>!c.machine_pass).length);
assert(!html.includes('<img src=x onerror='));
console.log('PASS campaign navigation, failure filter and missing/incomplete rounds');
