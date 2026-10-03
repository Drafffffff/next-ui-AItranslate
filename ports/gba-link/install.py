#!/usr/bin/env python3
"""Install/roll back this experimental update on a mounted SD card; no SSH."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import time

PACKAGE = Path(__file__).resolve().parent
FILES = ('.system/tg5040/bin/minarch.elf', '.system/tg5040/cores/gpsp_libretro.so')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe(root, relative):
    if relative.startswith('/') or '..' in Path(relative).parts:
        raise ValueError('Invalid relative path')
    path = root
    for part in Path(relative).parts:
        path = path / part
        if path.is_symlink():
            raise ValueError('Refusing symbolic link: ' + str(path))
    return path


def atomic_json(path, value):
    temporary=path.with_name(path.name+'.tmp')
    with open(temporary,'w') as output:
        json.dump(value,output,ensure_ascii=False,indent=2);output.write('\n')
        output.flush();os.fsync(output.fileno())
    os.replace(temporary,path)


def replace(source,destination):
    temporary=destination.with_name(destination.name+'.gba-link-new')
    with open(source,'rb') as src, open(temporary,'wb') as dst:
        shutil.copyfileobj(src,dst);dst.flush();os.fsync(dst.fileno())
    os.chmod(temporary,0o755)
    if sha(source)!=sha(temporary):
        temporary.unlink();raise ValueError('Copy checksum mismatch')
    os.replace(temporary,destination)


def rollback(root,backup):
    journal_path=backup/'journal.json'
    journal=json.loads(journal_path.read_text())
    if set(journal['old'])!=set(FILES):raise ValueError('Invalid backup journal')
    if journal['state']=='PREPARING':
        if any(sha(safe(root,name))!=checksum for name,checksum in journal['old'].items()):
            raise ValueError('Current files changed during incomplete preparation; inspect before recovery')
        journal['state']='ROLLED_BACK';atomic_json(journal_path,journal)
        print('Preparation cancelled; installed files were never changed.');return
    for name,checksum in journal['old'].items():
        if sha(safe(backup,name))!=checksum:raise ValueError('Backup checksum mismatch')
        safe(root,name)
    journal['state']='ROLLING_BACK';atomic_json(journal_path,journal)
    for name in FILES:replace(safe(backup,name),safe(root,name))
    journal['state']='ROLLED_BACK';atomic_json(journal_path,journal)
    print('Rolled back both files. ROMs, saves and settings were untouched.')


def install(root):
    manifest=json.loads((PACKAGE/'manifest.json').read_text())
    if set(manifest['files'])!=set(FILES):raise ValueError('Unexpected update contents')
    for name,checksum in manifest['files'].items():
        if sha(safe(PACKAGE/'payload',name))!=checksum:raise ValueError('Package checksum mismatch')
        if not safe(root,name).is_file():raise ValueError('Expected existing NextUI file: '+name)
    backups=safe(root,'.gba-link-backups');backups.mkdir(exist_ok=True)
    for journal_path in backups.glob('*/journal.json'):
        if json.loads(journal_path.read_text())['state'] not in ('APPLIED','ROLLED_BACK'):
            raise ValueError('Interrupted update: run rollback --backup '+journal_path.parent.name)
    backup=backups/(time.strftime('%Y%m%d-%H%M%S')+'-'+manifest['revision'][:8])
    backup.mkdir()  # Refuse an existing backup; never overwrite it.
    journal={'schema':1,'state':'PREPARING','revision':manifest['revision'],'old':{name:sha(safe(root,name)) for name in FILES}}
    atomic_json(backup/'journal.json',journal)
    for name in FILES:
        source=safe(root,name);destination=safe(backup,name)
        destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
        if sha(source)!=sha(destination):raise ValueError('Backup checksum mismatch')
        journal['old'][name]=sha(destination)
    journal['state']='APPLYING';atomic_json(backup/'journal.json',journal)
    try:
        for name in FILES:replace(safe(PACKAGE/'payload',name),safe(root,name))
        journal['state']='APPLIED';atomic_json(backup/'journal.json',journal)
    except Exception:
        rollback(root,backup);raise
    print('Installed experimental GBA Link. Backup: '+backup.name)
    print('Rollback: python3 install.py '+str(root)+' rollback --backup '+backup.name)
    print('Eject the SD card before reinserting it. Two Bricks need the same package.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdcard',type=Path)
    parser.add_argument('action',choices=['install','rollback'])
    parser.add_argument('--backup',help='Exact backup directory printed at installation')
    args=parser.parse_args();root=args.sdcard.resolve(strict=True)
    if root==Path('/') or not root.is_dir():parser.error('Specify the mounted NextUI SD card')
    if args.action=='rollback':
        if not args.backup or Path(args.backup).name!=args.backup:parser.error('Exact --backup name required')
        rollback(root,safe(root,'.gba-link-backups/'+args.backup))
    else:install(root)


if __name__=='__main__':
    main()
