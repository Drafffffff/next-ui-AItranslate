/** Native PocketJS text, without a DOM or HTML execution. Offsets refer to
 * display text, so formatting survives both directions of viewport paging. */
export type MarkdownBlock='paragraph'|'heading'|'list'|'quote'|'code'|'table'|'rule';
export interface MarkdownStyle {block:MarkdownBlock;depth:number;flags:number}
export interface MarkdownRange extends MarkdownStyle {start:number;end:number}
export interface MarkdownDocument {text:string;ranges:MarkdownRange[]}
export const STRONG=1,EMPHASIS=2,CODE=4,LINK=8,STRIKE=16;
const normal:MarkdownStyle={block:'paragraph',depth:0,flags:0};
export function displayText(raw:string){return raw.replace(/\r\n?/g,'\n').replace(/\t/g,'  ').replace(/[\u0000-\u0009\u000b-\u001f\uD800-\uDFFF\uFE00-\uFE0F\uFFFD-\uFFFF]|[\u{10000}-\u{10FFFF}]/gu,'?');}
export function markdown(raw:string,enabled=true):MarkdownDocument {
 const source=displayText(raw);
 if(!enabled)return {text:source,ranges:[]};
 const parts:string[]=[],ranges:MarkdownRange[]=[];let length=0;
 function add(text:string,style:MarkdownStyle){
  if(!text)return;parts.push(text);const end=length+text.length,last=ranges[ranges.length-1];
  if(last&&last.end===length&&last.block===style.block&&last.depth===style.depth&&last.flags===style.flags)last.end=end;
  else ranges.push({...style,start:length,end});length=end;
 }
 function inline(text:string,style:MarkdownStyle,nesting=0){
  if(nesting>=8){add(text,style);return;}
  let i=0,start=0;
  const flush=()=>{add(text.slice(start,i),style);};
  while(i<text.length){
   if(text[i]==='\\'&&i+1<text.length&&/[!"#$%&'()*+,\-./:;<=>?@[\]\\^_`{|}~]/.test(text[i+1])){flush();add(text[i+1],style);i+=2;start=i;continue;}
   if(text[i]==='`'){
    const ticks=/^`+/.exec(text.slice(i))![0],end=text.indexOf(ticks,i+ticks.length);
    if(end>=0){flush();let code=text.slice(i+ticks.length,end);if(code.startsWith(' ')&&code.endsWith(' ')&&code.trim())code=code.slice(1,-1);add(code,{...style,flags:style.flags|CODE});i=end+ticks.length;start=i;continue;}
   }
   if(text[i]==='['||text[i]==='!'&&text[i+1]==='['){
    const link=/^(!?)\[([^\]\n]*)\]\(([^()\n]*(?:\([^()\n]*\)[^()\n]*)?)\)/.exec(text.slice(i));
    if(link){flush();if(link[1])add('图片 · ',style);inline(link[2],{...style,flags:style.flags|LINK},nesting+1);const target=link[3].trim().replace(/\s+["'][^"']*["']$/,'');if(target)add(' ('+target+')',{...style,flags:style.flags|LINK});i+=link[0].length;start=i;continue;}
   }
   if(text[i]==='<'&&/^<(?:https?:\/\/|mailto:)/.test(text.slice(i))){const end=text.indexOf('>',i+1);if(end>=0){flush();add(text.slice(i+1,end),{...style,flags:style.flags|LINK});i=end+1;start=i;continue;}}
   const marker='*_~'.includes(text[i])?/^(\*\*\*|___|\*\*|__|~~|\*|_)/.exec(text.slice(i))?.[0]:undefined;
   if(marker&&!(marker[0]==='_'&&i>0&&/[\w]/.test(text[i-1]))&&text[i+marker.length]&&!/\s/.test(text[i+marker.length])){
    const end=text.indexOf(marker,i+marker.length);
    if(end>i+marker.length&&!/\s/.test(text[end-1])&&!(marker[0]==='_'&&/[\w]/.test(text[end+marker.length]||''))){
     flush();const flag=marker==='~~'?STRIKE:marker.length===3?STRONG|EMPHASIS:marker.length===2?STRONG:EMPHASIS;
     inline(text.slice(i+marker.length,end),{...style,flags:style.flags|flag},nesting+1);i=end+marker.length;start=i;continue;
    }
   }
   i++;
  }
  flush();
 }
 const lines=source.split('\n');let fence='',fenceSize=0,blank=false,table=false;
 const separator=(s:string)=>/^\s*\|?\s*:?-{3,}:?\s*(?:\|\s*:?-{3,}:?\s*)+\|?\s*$/.test(s);
 for(let n=0;n<lines.length;n++){
  const line=lines[n],f=/^ {0,3}(`{3,}|~{3,})(.*)$/.exec(line);
  if(fence){
   if(f&&f[1][0]===fence&&f[1].length>=fenceSize&&!f[2].trim()){fence='';blank=false;continue;}
   add(line+(n<lines.length-1?'\n':''),{block:'code',depth:0,flags:CODE});continue;
  }
  if(f){fence=f[1][0];fenceSize=f[1].length;table=false;blank=false;const language=f[2].trim();if(language)add(language+'\n',{block:'code',depth:0,flags:CODE|STRONG});continue;}
  if(!line.trim()){if(!blank)add('\n',normal);blank=true;table=false;continue;}
  blank=false;
  if(/^ {0,3}(?:\*\s*){3,}$|^ {0,3}(?:-\s*){3,}$|^ {0,3}(?:_\s*){3,}$/.test(line)){add('─\n',{block:'rule',depth:0,flags:0});table=false;continue;}
  const heading=/^ {0,3}(#{1,6})\s+(.+?)\s*#*\s*$/.exec(line);
  if(heading){inline(heading[2],{block:'heading',depth:heading[1].length,flags:STRONG});add('\n',{block:'heading',depth:heading[1].length,flags:STRONG});table=false;continue;}
  if(n+1<lines.length&&/^\s*(?:={3,}|-{3,})\s*$/.test(lines[n+1])){const depth=lines[n+1].trim()[0]==='='?1:2;inline(line,{block:'heading',depth,flags:STRONG});add('\n',{block:'heading',depth,flags:STRONG});n++;table=false;continue;}
  if(line.includes('|')&&n+1<lines.length&&separator(lines[n+1])){table=true;const style:MarkdownStyle={block:'table',depth:0,flags:STRONG};inline(line.trim().replace(/^\||\|$/g,'').trim(),style);add('\n',style);n++;continue;}
  if(table&&line.includes('|')){const style:MarkdownStyle={block:'table',depth:0,flags:0};inline(line.trim().replace(/^\||\|$/g,'').trim(),style);add('\n',style);continue;}table=false;
  const quote=/^\s*((?:>\s*)+)(.*)$/.exec(line);
  if(quote){const style:MarkdownStyle={block:'quote',depth:Math.min(3,(quote[1].match(/>/g)||[]).length),flags:0};inline(quote[2],style);add('\n',style);continue;}
  const list=/^( *)([-+*]|\d+[.)])\s+(.*)$/.exec(line);
  if(list){const style:MarkdownStyle={block:'list',depth:Math.min(3,Math.floor(list[1].length/2)),flags:0};let body=list[3],prefix=/\d/.test(list[2])?list[2]+' ':'• ';const task=/^\[([ xX])\]\s+(.*)$/.exec(body);if(task){prefix=task[1]===' '?'[ ] ':'[x] ';body=task[2];}add(prefix,style);inline(body,style);add('\n',style);continue;}
  if(/^ {4}/.test(line)){add(line.slice(4)+'\n',{block:'code',depth:0,flags:CODE});continue;}
  inline(line,normal);if(n<lines.length-1)add('\n',normal);
 }
 return {text:parts.join(''),ranges};
}
export function rangeAt(doc:MarkdownDocument,offset:number):MarkdownRange|undefined {
 let lo=0,hi=doc.ranges.length-1;
 while(lo<=hi){const mid=(lo+hi)>>1,r=doc.ranges[mid];if(offset<r.start)hi=mid-1;else if(offset>=r.end)lo=mid+1;else return r;}
 return undefined;
}
export function blockWindow(doc:MarkdownDocument,offset:number,reverse:boolean,size:number){
 const r=rangeAt(doc,reverse?offset-1:offset);let start=reverse?Math.max(0,offset-size):offset,end=reverse?offset:Math.min(doc.text.length,offset+size);
 if(r){let at=reverse?r.start:r.end;
  while(reverse?at>start:at<end){const next=rangeAt(doc,reverse?at-1:at);if(!next||next.block!==r.block||next.depth!==r.depth){if(reverse)start=at;else end=at;break;}at=reverse?next.start:next.end;}
 }
 return {start,end,style:r||normal};
}
