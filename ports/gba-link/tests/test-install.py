#!/usr/bin/env python3
"""Exercise mounted-SD install/rollback boundaries without a handheld."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

MODULE=Path(__file__).resolve().parents[1]/'install.py'
spec=importlib.util.spec_from_file_location('gba_installer',MODULE)
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)


class SDInstall(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.card=self.root/'card';self.package=self.root/'package'
        self.card.mkdir();self.package.mkdir();self.old={}
        hashes={}
        for i,name in enumerate(installer.FILES):
            dst=self.card/name;dst.parent.mkdir(parents=True,exist_ok=True)
            self.old[name]=('old file '+str(i)).encode();dst.write_bytes(self.old[name])
            src=self.package/'payload'/name;src.parent.mkdir(parents=True,exist_ok=True)
            src.write_bytes(('new file '+str(i)).encode());hashes[name]=installer.sha(src)
        (self.package/'manifest.json').write_text(json.dumps({'revision':'12345678'*5,'files':hashes}))
        self.key=self.card/'.userdata/shared/ai-keys.txt';self.key.parent.mkdir(parents=True);self.key.write_text('private-test-key')
        self.rom=self.card/'Roms/GBA/user.gba';self.rom.parent.mkdir(parents=True);self.rom.write_bytes(b'user rom')
        self.patch=patch.object(installer,'PACKAGE',self.package);self.patch.start();self.addCleanup(self.patch.stop)

    def unchanged(self):
        for name,data in self.old.items():self.assertEqual((self.card/name).read_bytes(),data)
        self.assertEqual(self.key.read_text(),'private-test-key');self.assertEqual(self.rom.read_bytes(),b'user rom')

    def test_install_and_rollback(self):
        installer.install(self.card)
        backup=next((self.card/'.gba-link-backups').iterdir())
        for name in installer.FILES:self.assertEqual((self.card/name).read_bytes(),(self.package/'payload'/name).read_bytes())
        installer.rollback(self.card,backup);self.unchanged()

    def test_first_helper_install_and_rollback(self):
        helper=self.card/installer.HELPER;helper.unlink();self.old.pop(installer.HELPER)
        installer.install(self.card)
        self.assertTrue(helper.is_file())
        backup=next((self.card/'.gba-link-backups').iterdir())
        installer.rollback(self.card,backup)
        self.assertFalse(helper.exists());self.unchanged()

    def test_missing_helper_payload_rejected_before_change(self):
        (self.package/'payload'/installer.HELPER).unlink()
        with self.assertRaises(FileNotFoundError):installer.install(self.card)
        self.unchanged()

    def test_legacy_backup_rollback(self):
        backup=self.card/'.gba-link-backups/legacy';backup.mkdir(parents=True)
        old={}
        for name in installer.BASE_FILES:
            target=backup/name;target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(self.old[name]);old[name]=installer.sha(target)
            (self.card/name).write_bytes(b'changed')
        (backup/'journal.json').write_text(json.dumps({'state':'APPLIED','old':old}))
        installer.rollback(self.card,backup);self.unchanged()

    def test_corrupted_payload_never_modifies_card(self):
        (self.package/'payload'/installer.FILES[1]).write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError,'Package checksum mismatch'):installer.install(self.card)
        self.assertFalse((self.card/'.gba-link-backups').exists());self.unchanged()

    def test_mid_update_exception_restores_both_files(self):
        original=installer.replace;count=0
        def fail_once(source,destination):
            nonlocal count
            count+=1
            if count==2:raise OSError('simulated second-file write failure')
            original(source,destination)
        with patch.object(installer,'replace',fail_once):
            with self.assertRaisesRegex(OSError,'simulated'):installer.install(self.card)
        self.unchanged()

    def test_symlink_target_rejected(self):
        target=self.card/installer.FILES[0];data=target.read_bytes();target.unlink()
        outside=self.root/'outside';outside.write_bytes(data);target.symlink_to(outside)
        with self.assertRaisesRegex(ValueError,'symbolic link'):installer.install(self.card)
        self.assertEqual(outside.read_bytes(),data)

    def test_incomplete_preparation_is_recoverable(self):
        original=installer.sha
        def fail_copy(path):
            if '.gba-link-backups' in str(path):raise OSError('simulated backup interruption')
            return original(path)
        with patch.object(installer,'sha',fail_copy):
            with self.assertRaisesRegex(OSError,'backup interruption'):installer.install(self.card)
        backup=next((self.card/'.gba-link-backups').iterdir())
        installer.rollback(self.card,backup);self.unchanged()


if __name__=='__main__':unittest.main(verbosity=2)
