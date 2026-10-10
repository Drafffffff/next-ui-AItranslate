import {build} from 'esbuild';
import {mkdir,cp,writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
const out=new URL('../../../build/workshop-native-share/',import.meta.url);
await mkdir(out,{recursive:true});
await build({entryPoints:[fileURLToPath(new URL('./server.mjs',import.meta.url))],outfile:fileURLToPath(new URL('server.mjs',out)),
  bundle:true,platform:'node',target:'node24',format:'esm',treeShaking:true,minify:false});
await cp(new URL('../public/',import.meta.url),new URL('public/',out),{recursive:true});
for(const file of ['Dockerfile','README.md'])await cp(new URL(file,import.meta.url),new URL(file,out));
await writeFile(new URL('.dockerignore',out),'node_modules\n.git\n.env*\n*.key\n*.log\n');
console.log('Built build/workshop-native-share; contains runtime/assets/server only, no project data or credentials');
