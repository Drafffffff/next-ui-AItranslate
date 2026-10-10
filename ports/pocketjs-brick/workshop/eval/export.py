#!/usr/bin/env python3
"""Export a complete offline HTML report including audio and downloadable evidence."""
import argparse,base64,json,re
from pathlib import Path
from report import render_report

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('run',type=Path);parser.add_argument('--output',type=Path,required=True)
 args=parser.parse_args();root=Path(__file__).resolve().parents[4]
 source=(args.run if args.run.is_absolute() else root/args.run).resolve()
 output=(args.output if args.output.is_absolute() else root/args.output).resolve()
 data=json.loads((source/'summary.json').read_text())
 if not data.get('complete'):raise ValueError('Export needs a completed run')
 if output.exists():raise ValueError('Export output must be a new file')
 files={};mime={'game.p8':'text/plain;charset=utf-8','game.lua':'text/plain;charset=utf-8','trace.json':'application/json','project.json':'application/json','prompt-1.txt':'text/plain;charset=utf-8','replay/audio.wav':'audio/wav'}
 for case in data['cases']:
  slug=case['slug']
  if not re.fullmatch(r'[a-z0-9-]+',slug):raise ValueError('Invalid case slug')
  for name,content_type in mime.items():
   path=source/'cases'/slug/name
   if path.is_file():files[slug+'/'+name]='data:'+content_type+';base64,'+base64.b64encode(path.read_bytes()).decode()
 output.parent.mkdir(parents=True,exist_ok=True)
 output.write_text(render_report(dict(data,embedded_files=files)),encoding='utf-8')
 print('Standalone report: '+str(output)+' ('+str(output.stat().st_size)+' bytes)')

if __name__=='__main__':main()
