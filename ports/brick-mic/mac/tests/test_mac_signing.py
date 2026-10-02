"""Offline signing policy fixtures: no real certificate, key, TCC, app or process access."""
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch

HELPER = Path(__file__).resolve().parents[2] / "mac-signing.py"
spec = importlib.util.spec_from_file_location("brick_mic_signing_fixture", HELPER)
signing = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = signing
spec.loader.exec_module(signing)

LEAF = b"Synthetic public leaf certificate fixture"
FIRST = hashlib.sha1(LEAF).hexdigest().upper()
SECOND = "B" * 40
REQUIREMENT = 'identifier "com.nextui.brickmic" and anchor = H"' + FIRST + '"'
ADHOC_REQUIREMENT = 'cdhash H"' + "C" * 40 + '"'


def certificate(fingerprint=FIRST, requirement=REQUIREMENT):
    return signing.Signature(signing.BUNDLE_ID, False, requirement, fingerprint)


def adhoc(requirement=ADHOC_REQUIREMENT):
    return signing.Signature(signing.BUNDLE_ID, True, requirement, None)


def pin(requirement=REQUIREMENT, migration=None):
    return {"version": 1, "bundle_id": signing.BUNDLE_ID, "fingerprint": FIRST,
            "designated_requirement": requirement, "initial_adhoc_requirement": migration}


class SigningTests(unittest.TestCase):
    def test_missing_pin_blocks_without_command_or_file_changes(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(signing, "command") as command:
            path = Path(folder) / "signing-identity.json"
            with self.assertRaisesRegex(signing.SigningError, "not configured"):
                signing.load_pin(path)
            self.assertFalse(path.exists())
            command.assert_not_called()

    def test_only_one_valid_existing_identity_can_be_selected(self):
        output = f'  1) {FIRST} "Fixture Brick Mic"\n  2) {SECOND} "Other fixture"\n  2 valid identities found\n'
        available = signing.parse_identities(output)
        self.assertEqual(signing.resolve_identity("Fixture Brick Mic", available), FIRST)
        self.assertEqual(signing.resolve_identity(FIRST.lower(), available), FIRST)
        for requested in ("-", "missing fixture"):
            with self.assertRaises(signing.SigningError):
                signing.resolve_identity(requested, available)
        with self.assertRaises(signing.SigningError):
            signing.resolve_identity("Ambiguous", {FIRST: "Ambiguous", SECOND: "Ambiguous"})

    def test_pinned_certificate_and_requirement_accept_rebuild(self):
        signing.check_candidate(pin(), certificate(), certificate())
        self.assertEqual(signing.signing_identity(pin(), {FIRST: "Fixture"}), FIRST)

    def test_adhoc_candidate_is_always_rejected(self):
        for installed, config in ((certificate(), pin()), (adhoc(), pin(None, ADHOC_REQUIREMENT))):
            with self.assertRaisesRegex(signing.SigningError, "adhoc"):
                signing.check_candidate(config, adhoc(), installed)

    def test_certificate_or_requirement_change_is_rejected(self):
        with self.assertRaises(signing.SigningError):
            signing.check_candidate(pin(), certificate(SECOND), certificate())
        with self.assertRaises(signing.SigningError):
            signing.check_candidate(pin(), certificate(), certificate(SECOND))
        with self.assertRaises(signing.SigningError):
            signing.check_candidate(pin(), certificate(requirement=REQUIREMENT + " or true"), certificate())
        with self.assertRaises(signing.SigningError):
            signing.signing_identity(pin(), {FIRST: "Fixture", SECOND: "Other"}, SECOND)

    def test_no_implicit_adhoc_migration_or_changed_baseline(self):
        with self.assertRaisesRegex(signing.SigningError, "explicit"):
            signing.check_candidate(pin(None), certificate(), adhoc())
        signing.check_candidate(pin(None, ADHOC_REQUIREMENT), certificate(), adhoc())
        with self.assertRaises(signing.SigningError):
            signing.check_candidate(pin(None, ADHOC_REQUIREMENT), certificate(), adhoc("another exact cdhash"))
        # Once installed with the certificate, subsequent candidates must also
        # preserve that designated requirement even for an initial migration pin.
        with self.assertRaises(signing.SigningError):
            signing.check_candidate(pin(None, ADHOC_REQUIREMENT), certificate(requirement="changed"), certificate())
        with self.assertRaisesRegex(signing.SigningError, "identifier"):
            signing.check_candidate(pin(None, ADHOC_REQUIREMENT), certificate(requirement="true"), adhoc())

    def test_initialization_requires_explicit_migration_and_cannot_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "support/signing-identity.json"
            installed = Path(folder) / "Brick Mic.app"
            installed.mkdir()
            with patch.object(signing, "identities", return_value={FIRST: "Fixture Brick Mic"}), \
                 patch.object(signing, "inspect_app", return_value=adhoc()), \
                 patch.object(signing, "command") as command:
                with self.assertRaisesRegex(signing.SigningError, "explicitly"):
                    signing.initialize(FIRST, False, path, installed)
                self.assertFalse(path.exists())
                with patch("builtins.print"):
                    signing.initialize(FIRST, True, path, installed)
                value = signing.load_pin(path)
                self.assertEqual(value["fingerprint"], FIRST)
                self.assertEqual(value["initial_adhoc_requirement"], ADHOC_REQUIREMENT)
                original = path.read_bytes()
                with self.assertRaisesRegex(signing.SigningError, "already pinned"):
                    signing.initialize(SECOND, True, path, installed)
                self.assertEqual(path.read_bytes(), original)
                command.assert_not_called()

    def test_initialization_will_not_replace_an_installed_fixed_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            path, installed = Path(folder) / "pin.json", Path(folder) / "app"
            installed.mkdir()
            with patch.object(signing, "identities", return_value={SECOND: "Other fixture"}), \
                 patch.object(signing, "inspect_app", return_value=certificate()):
                with self.assertRaisesRegex(signing.SigningError, "differs"):
                    signing.initialize(SECOND, True, path, installed)
            self.assertFalse(path.exists())

    def test_initialization_preserves_an_existing_certificate_requirement(self):
        with tempfile.TemporaryDirectory() as folder:
            path, installed = Path(folder) / "pin.json", Path(folder) / "app"
            installed.mkdir()
            with patch.object(signing, "identities", return_value={FIRST: "Fixture Brick Mic"}), \
                 patch.object(signing, "inspect_app", return_value=certificate()), patch("builtins.print"):
                signing.initialize(FIRST, False, path, installed)
            saved = signing.load_pin(path)
            self.assertEqual(saved["designated_requirement"], REQUIREMENT)
            self.assertIsNone(saved["initial_adhoc_requirement"])

    def test_public_leaf_and_commented_requirement_are_inspected_without_signing(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / "Fixture.app"
            (app / "Contents").mkdir(parents=True)
            (app / "Contents/Info.plist").write_bytes(plistlib.dumps({
                "CFBundleIdentifier": signing.BUNDLE_ID, "CFBundleExecutable": "BrickMic"}))
            calls = []

            def fake_command(arguments):
                calls.append(arguments)
                extraction = [value for value in arguments if value.startswith("--extract-certificates=")]
                if extraction:
                    self.assertEqual(len(arguments), 4, "A standalone prefix is mistaken for an application path")
                    self.assertEqual(arguments[-1], str(app))
                    prefix = extraction[0].split("=", 1)[1]
                    Path(prefix + "0").write_bytes(LEAF)
                    return ""
                if "-d" in arguments:
                    return f"Identifier={signing.BUNDLE_ID}\n# designated => {REQUIREMENT}\n"
                return ""

            with patch.object(signing, "command", side_effect=fake_command):
                result = signing.inspect_app(app)
            self.assertEqual(result, certificate())
            self.assertIn("--strict", calls[0])
            self.assertEqual(sum(any(value.startswith("--extract-certificates=") for value in call) for call in calls), 1)
            self.assertTrue(all("--sign" not in call and "security" not in call for call in calls))

    def test_bad_or_linked_pin_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "pin.json"
            path.write_text(json.dumps({**pin(), "fingerprint": "-"}))
            with self.assertRaises(signing.SigningError):
                signing.load_pin(path)
            link = Path(folder) / "linked.json"
            link.symlink_to(path)
            with self.assertRaises(signing.SigningError):
                signing.load_pin(link)

    def test_seal_first_migration_pins_requirement_and_revokes_adhoc(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "pin.json"
            path.write_text(json.dumps(pin(None, ADHOC_REQUIREMENT)))
            with patch.object(signing, "identities", return_value={FIRST: "Fixture"}), \
                 patch.object(signing, "inspect_app", return_value=certificate()), patch("builtins.print"):
                signing.seal(path, Path(folder) / "app")
            saved = signing.load_pin(path)
            self.assertEqual(saved["fingerprint"], FIRST)
            self.assertEqual(saved["designated_requirement"], REQUIREMENT)
            self.assertIsNone(saved["initial_adhoc_requirement"])
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            self.assertEqual(path.stat().st_uid, signing.os.getuid())
            signing.check_candidate(saved, certificate(), certificate())
            with self.assertRaises(signing.SigningError):
                signing.check_installed(saved, adhoc())
            with self.assertRaises(signing.SigningError):
                signing.check_installed(pin(REQUIREMENT, ADHOC_REQUIREMENT), adhoc())
            self.assertEqual(list(path.parent.glob(path.name + ".*")), [])

    def test_seal_already_pinned_requirement_does_not_rewrite(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "pin.json"
            path.write_text(json.dumps(pin()) + "\n")
            original, inode = path.read_bytes(), path.stat().st_ino
            with patch.object(signing, "identities", return_value={FIRST: "Fixture"}), \
                 patch.object(signing, "inspect_app", return_value=certificate()), patch("builtins.print"):
                signing.seal(path, Path(folder) / "app")
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(path.stat().st_ino, inode)

    def test_seal_refuses_different_certificate_or_changed_existing_requirement(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "pin.json"
            for config, installed in ((pin(None, ADHOC_REQUIREMENT), certificate(SECOND)),
                                      (pin(), certificate(requirement=REQUIREMENT + " and false")),
                                      (pin(None, ADHOC_REQUIREMENT), adhoc())):
                path.write_text(json.dumps(config))
                original = path.read_bytes()
                with patch.object(signing, "identities", return_value={FIRST: "Fixture"}), \
                     patch.object(signing, "inspect_app", return_value=installed):
                    with self.assertRaises(signing.SigningError):
                        signing.seal(path, Path(folder) / "app")
                self.assertEqual(path.read_bytes(), original)

    def test_seal_will_not_overwrite_configuration_changed_during_verification(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "pin.json"
            path.write_text(json.dumps(pin(None, ADHOC_REQUIREMENT)))
            replacement = {**pin(), "fingerprint": SECOND}

            def change_configuration(_):
                path.write_text(json.dumps(replacement))
                return certificate()

            with patch.object(signing, "identities", return_value={FIRST: "Fixture"}), \
                 patch.object(signing, "inspect_app", side_effect=change_configuration):
                with self.assertRaisesRegex(signing.SigningError, "changed during sealing"):
                    signing.seal(path, Path(folder) / "app")
            self.assertEqual(signing.load_pin(path), replacement)
            self.assertEqual(list(path.parent.glob(path.name + ".*")), [])

    def test_seal_missing_pin_does_not_create_files(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(signing, "command") as command:
            path = Path(folder) / "missing/pin.json"
            with self.assertRaises(signing.SigningError):
                signing.seal(path, Path(folder) / "app")
            self.assertEqual(list(Path(folder).iterdir()), [])
            command.assert_not_called()


if __name__ == "__main__":
    unittest.main()
