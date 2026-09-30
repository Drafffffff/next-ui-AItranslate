export const PALETTE = ['#191d28','#f1e8ce','#e8b54a','#d86e3b','#b94749','#8f5377','#5b526f','#3c4059','#496e85','#639caa','#9bc6b6','#6a9367','#385e48','#a9b77a','#c7ad87','#857461'];
export type Project = {version: 1; size: number; fps: number; frames: string[]};
export function decodeProject(raw: string): Project {
  const p = JSON.parse(raw);
  if(p.version !== 1 || ![16,32,64].includes(p.size) || !Number.isInteger(p.fps) || p.fps < 2 || p.fps > 24 || !Array.isArray(p.frames) || p.frames.length<1 || p.frames.length>8 || p.frames.some((f:unknown)=>typeof f!=='string'||f.length!==p.size*p.size||!/^[0-9a-f]+$/.test(f))) throw new Error('作品文件格式无效');
  return p;
}
export function fill(frame: string,size: number,x:number,y:number,color:number):string {
  if(x<0||y<0||x>=size||y>=size||color<0||color>15)return frame;
  const a=frame.split(''),start=y*size+x,old=a[start],value=color.toString(16);
  if(old===value)return frame;
  const queue=[start];a[start]=value;
  for(let i=0;i<queue.length;i++){
    const j=queue[i],cx=j%size,cy=Math.floor(j/size);
    const next=[cx?j-1:-1,cx+1<size?j+1:-1,cy?j-size:-1,cy+1<size?j+size:-1];
    for(const k of next)if(k>=0&&a[k]===old){a[k]=value;queue.push(k);}
  }
  return a.join('');
}
export function paint(frame:string,size:number,x:number,y:number,color:number):string {
  const i=y*size+x;if(i<0||i>=frame.length||x<0||x>=size||y<0||y>=size)return frame;
  return frame.slice(0,i)+color.toString(16)+frame.slice(i+1);
}
export function demo():Project {
  const size=32,frames:string[]=[];
  for(let f=0;f<4;f++){
    let p='0'.repeat(size*size);
    const rect=(x:number,y:number,w:number,h:number,c:number)=>{for(let yy=y;yy<y+h;yy++)for(let xx=x;xx<x+w;xx++)p=paint(p,size,xx,yy,c);};
    const dy=f===1?-1:f===3?1:0;
    rect(8,7+dy,16,15,9);rect(10,9+dy,12,10,10);rect(12,11+dy,2,f===2?1:3,0);rect(18,11+dy,2,f===2?1:3,0);rect(14,16+dy,4,1,4);
    rect(15,4+dy,2,3,14);rect(14,3+dy,4,2,2);rect(5,13+dy,3,6,8);rect(24,12+dy,3,6,8);rect(10,22+dy,4,5,8);rect(18,22+dy,4,5,8);
    frames.push(p);
  }
  return {version:1,size,fps:8,frames};
}
export function svg(project:Project,index:number):string {
  const n=project.size,frame=project.frames[index],paths=PALETTE.map(()=> '');
  for(let y=0;y<n;y++){let x=0;while(x<n){const c=frame[y*n+x];let end=x+1;while(end<n&&frame[y*n+end]===c)end++;paths[parseInt(c,16)]+= `M${x} ${y}h${end-x}v1h-${end-x}z`;x=end;}}
  const body=paths.map((d,i)=>d?`<path fill="${PALETTE[i]}" d="${d}"/>`:'').join('');
  return `<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 ${n} ${n}" shape-rendering="crispEdges">${body}</svg>`;
}
