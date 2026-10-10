#!/usr/bin/env python3
"""Verify controller UX and voice ownership against native PocketJS, without paid calls."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
from http.server import ThreadingHTTPServer
from PIL import Image

spec = importlib.util.spec_from_file_location('workshop_verify', Path(__file__).with_name('verify.py'))
verify = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = verify
spec.loader.exec_module(verify)


class VoiceFixture(socketserver.ThreadingUnixStreamServer):
    daemon_threads = True
    def __init__(self, path, late=False):
        self.state = dict(state='idle', session=0, rms=0, seconds=0, text='', error='')
        self.lock = threading.Lock()
        self.operations = []
        self.late = late
        super().__init__(path, VoiceHandler)

    def command(self, op):
        with self.lock:
            self.operations.append(op)
            if op == 'start':
                self.state.update(state='recording', session=self.state['session']+1, text='', seconds=.5, rms=3500)
            elif op == 'stop':
                self.state.update(state='result', text='做一个用方向键移动圆点的游戏。')
            elif op == 'cancel':
                old = self.state['session']
                self.state.update(state='idle', session=old+1, text='')
                if self.late:
                    def stale():
                        with self.lock:
                            self.state.update(state='result', session=old, text='这条已取消的需求不应该被发送')
                    timer = threading.Timer(.15, stale)
                    timer.daemon = True
                    timer.start()
            return json.dumps(self.state, ensure_ascii=False).encode()+b'\n'


class VoiceHandler(socketserver.StreamRequestHandler):
    def handle(self):
        self.wfile.write(self.server.command(self.rfile.readline(64).decode().strip()))


def run(host, mode, dump, env, output, label):
    p = subprocess.run([str(host), mode, '--dump', str(dump)], cwd=host.parent, env=env,
                       capture_output=True, text=True, timeout=100)
    (output/(label+'.stdout.log')).write_text(p.stdout)
    (output/(label+'.stderr.log')).write_text(p.stderr)
    verify.require(p.returncode == 0, f'{label}: {p.stdout}\n{p.stderr}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=Path)
    parser.add_argument('--project-fixture', required=True, type=Path, help='A passed verify.py output containing its fixture SD')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    host, output = args.host.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = next(args.project_fixture.glob('fixture-*/sd'))
    sd = output/'sd'
    verify.require(not sd.exists(), 'Use a fresh output directory')
    shutil.copytree(source, sd)
    shared = sd/'.userdata/shared'
    data = shared/'ai-game-workshop'
    roms = sd/'Roms/AI生成游戏 (P8)'
    project_path = next((data/'projects').glob('*.json'))
    project = verify.json_file(project_path)
    project_id = project['id']
    for i in range(3):
        draft=dict(project,id=f'game-ui-draft-{i}',revision=0,versions=[],messages=[],title='很长的项目名称用来检查列表文字重叠问题',updatedAt=project['updatedAt']-i-1)
        (data/'projects'/(draft['id']+'.json')).write_text(json.dumps(draft,ensure_ascii=False))
    # Different solid colours make the native priority observable in rendered pixels.
    for folder, color in [('covers', (224,80,70)), ('previews',(40,164,132))]:
        (data/folder).mkdir(exist_ok=True)
        Image.new('RGB',(128,128),color).save(data/folder/(project_id+'.png'))
    env = dict(os.environ, POCKETJS_WORKSHOP='1', POCKETJS_HARDWARE='0', POCKETJS_DATA=str(data),
               POCKETJS_ROMS=str(roms), POCKETJS_FONT=str(verify.REPOSITORY/'fonts/font1.ttf'),
               SHARED_USERDATA_PATH=str(shared))
    env.pop('POCKETJS_WORKSHOP_RESUME',None)
    env.pop('POCKETJS_VOICE',None)
    env.pop('POCKETJS_TEST',None)
    with tempfile.TemporaryDirectory(prefix='wg-asr-',dir='/tmp') as ipc:
        path=ipc+'/asr.sock'
        voice=VoiceFixture(path,late=True)
        threading.Thread(target=voice.serve_forever,daemon=True).start()
        env['POCKETJS_VOICE_SOCKET']=path
        try:
            for label, color in [('cover',(224,80,70)),('first-frame',(40,164,132))]:
                if label=='first-frame': (data/'covers'/(project_id+'.png')).unlink()
                # Recovered publication can normalize saved draft metadata; user turns must remain intact.
                old_messages=verify.json_file(project_path)['messages']
                dump=output/label
                run(host,'--workshop-ui-test',dump,env,output,label)
                verify.require((roms/'.media'/(project_id+'.png')).read_bytes()==(data/('covers' if label=='cover' else 'previews')/(project_id+'.png')).read_bytes(), 'NextUI menu cover missing/stale')
                image=Image.open(str(dump)+'.list.ppm')
                verify.require(image.getpixel((110,207))==color, f'{label}: wrong thumbnail priority/pixels')
                verify.require(verify.json_file(project_path)['messages']==old_messages, 'Cancelled recording submitted a user turn')
                voice_image=Image.open(str(dump)+'.voice.ppm')
                verify.require(voice_image.crop((56,520,968,546)).getextrema()==tuple((c,c) for c in verify.BACKGROUND),'Recording panel overlaps conversation content')
                for suffix in ['list','chat','voice','scrolled']:
                    Image.open(str(dump)+'.'+suffix+'.ppm').save(output/(label+'.'+suffix+'.png'))
            verify.require('start' in voice.operations and 'cancel' in voice.operations, 'Controller never reached voice bridge')
        finally:
            voice.shutdown();voice.server_close()
    # Fresh project: A press/release -> final recognition -> actual Pi tools -> unique publication.
    dictation=shared/'dictation-fixture'
    dictation.mkdir()
    dictation_roms=sd/'Roms/dictation-fixture'
    dictation_roms.mkdir()
    model=verify.MockState()
    server=ThreadingHTTPServer(('127.0.0.1',0),verify.make_handler(model))
    server.daemon_threads=True
    threading.Thread(target=server.serve_forever,daemon=True).start()
    (dictation/'config.json').write_text(json.dumps(dict(key=verify.FAKE_KEY, endpoint=f'http://127.0.0.1:{server.server_port}/v1',model='deepseek-flash',timeoutMs=10000)))
    env.update(POCKETJS_DATA=str(dictation),POCKETJS_ROMS=str(dictation_roms),NO_PROXY='127.0.0.1,localhost',no_proxy='127.0.0.1,localhost')
    with tempfile.TemporaryDirectory(prefix='wg-asr-',dir='/tmp') as ipc:
        path=ipc+'/asr.sock'
        voice=VoiceFixture(path)
        threading.Thread(target=voice.serve_forever,daemon=True).start()
        env['POCKETJS_VOICE_SOCKET']=path
        try:
            run(host,'--workshop-dictation-test',output/'dictation.ppm',env,output,'dictation')
            p=verify.json_file(next((dictation/'projects').glob('*.json')))
            users=[m for m in p['messages'] if m['role']=='user']
            results=[m for m in p['messages'] if m['role']=='toolResult']
            verify.require(p['revision']==1 and len(users)==1 and len(results)==5,'Voice did not create one full project/tool turn')
            verify.require(len(list(dictation_roms.glob('*.p8')))==1,'Voice created extra ROMs')
            verify.require(not model.errors and len(model.requests)==6,'Model fixture failed')
            verify.require(verify.FAKE_KEY not in json.dumps(p,ensure_ascii=False),'Key leaked into project')
            verify.require('stop' in voice.operations,'A release did not end recognition')
            image=Image.open(output/'dictation.ppm')
            verify.require(image.getpixel((100,546))==verify.BACKGROUND,'Completed dictation left its recording overlay visible')
            image.save(output/'dictation.png')
        finally:
            voice.shutdown();voice.server_close();server.shutdown();server.server_close()
    # Controllers and deletion run in an isolated copy; ordinary projects and keys are untouched.
    control_sd=output/'controls-sd'
    shutil.copytree(sd,control_sd)
    control_shared=control_sd/'.userdata/shared'
    control_data=control_shared/'ai-game-workshop'
    control_roms=control_sd/'Roms/AI生成游戏 (P8)'
    keeper='game-ui-draft-0'
    protected_project=(control_data/'projects'/(keeper+'.json')).read_bytes()
    protected_rom=b'KEEP THIS OTHER GAME'
    (control_roms/(keeper+'.p8')).write_bytes(protected_rom)
    with (control_roms/'map.txt').open('a') as f:f.write(keeper+'.p8\t保留游戏\n')
    (control_data/'prompt.txt').write_text('THIS REMOVED R INPUT MUST NEVER BE SENT')
    (control_data/'covers'/(project_id+'.png')).write_bytes((data/'previews'/(project_id+'.png')).read_bytes())
    runner=output/'fake-p8-launch.sh';runner.write_text('#!/bin/sh\nexit 0\n');runner.chmod(0o700)
    env.update(POCKETJS_DATA=str(control_data),POCKETJS_ROMS=str(control_roms),SHARED_USERDATA_PATH=str(control_shared),POCKETJS_P8_LAUNCH=str(runner))
    env.pop('POCKETJS_VOICE_SOCKET',None)
    run(host,'--workshop-controls-test',output/'controls.ppm',env,output,'controls')
    for directory,suffix in [('projects','.json'),('covers','.png'),('previews','.png')]:
        verify.require(not (control_data/directory/(project_id+suffix)).exists(),'Deleted project/cache survived: '+directory)
    verify.require(not (control_roms/(project_id+'.p8')).exists(),'Deleted game survived')
    verify.require(not (control_roms/'.media'/(project_id+'.png')).exists(),'Deleted menu cover survived')
    verify.require((control_data/'projects'/(keeper+'.json')).read_bytes()==protected_project,'Deletion changed another project')
    verify.require((control_roms/(keeper+'.p8')).read_bytes()==protected_rom,'Deletion changed another game')
    verify.require((control_roms/'map.txt').read_text()==keeper+'.p8\t保留游戏\n','Deletion damaged other menu mapping')
    verify.require(not (control_data/'last-project.txt').exists() and not (control_data/'play-request.txt').exists(),'Deleted game still selected/queued')
    verify.require(not list((control_data/'deletions').iterdir()),'Completed deletion left its intent')
    Image.open(output/'controls.ppm').save(output/'controls.png')
    Image.open(output/'controls.ppm.progress.ppm').save(output/'delete-progress.png')
    # Simulate interruption after durable intent and before any files are removed.
    (control_data/'deletions'/keeper).write_text('delete\n')
    (control_data/'last-project.txt').write_text(keeper)
    config_before=(control_data/'config.json').read_bytes()
    untouched=(control_data/'projects'/'game-ui-draft-1.json').read_bytes()
    run(host,'--workshop-ready',output/'delete-recovery.ppm',env,output,'delete-recovery')
    verify.require(not (control_data/'projects'/(keeper+'.json')).exists() and not (control_roms/(keeper+'.p8')).exists(),'Interrupted deletion did not recover')
    verify.require((control_data/'config.json').read_bytes()==config_before and (control_data/'projects'/'game-ui-draft-1.json').read_bytes()==untouched,'Recovery changed configuration/other project')
    verify.require(not list((control_data/'deletions').iterdir()),'Recovered deletion left its intent')
    (output/'report.json').write_text(json.dumps(dict(passed=True, list_first=True, cover_priority=True,
        frame_fallback=True, chat_tools=True, page_scroll=True, details=True, cancelled_voice_never_submits=True,
        voice_final_to_same_project=True, dictation_tool_results=4, start_plays_both_pages=True, r_removed=True, idle_stop_hidden=True, y_progress_outline=True, short_delete_cancelled=True, deletion_scoped=True, deletion_recovered=True, real_asr_called=False, real_model_called=False),indent=2)+'\n')
    print('PASS: list-first; cover/frame priority; chat and tools; page scroll; details; late cancelled transcript ignored; A release submits final voice to real Pi tools')
    print('Artifacts: '+str(output))


if __name__=='__main__':
    main()
