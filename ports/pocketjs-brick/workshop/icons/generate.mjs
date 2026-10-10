import {Resvg} from '@resvg/resvg-js';
import {readFileSync,writeFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
const directory=fileURLToPath(new URL('.',import.meta.url));
let header='/* Iconify Lucide SVG alpha masks. ISC; see workshop/icons/LICENSE.txt. */\n';
for(const [index,name] of ['gamepad-2','folder-plus'].entries()){
 const rendered=new Resvg(readFileSync(directory+name+'.svg')).render();
 writeFileSync(directory+name+'.png',rendered.asPng());
 const pixels=rendered.pixels,values=[];
 for(let i=3;i<pixels.length;i+=4)values.push(pixels[i]);
 header+=`static const unsigned char workshop_icon_${index}[4096]={\n`;
 for(let i=0;i<values.length;i+=32)header+=values.slice(i,i+32).join(',')+',\n';
 header+='};\n';
}
writeFileSync(directory+'masks.h',header);
