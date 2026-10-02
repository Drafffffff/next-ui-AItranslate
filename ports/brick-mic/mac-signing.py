#!/usr/bin/env python3
"""Pin an existing public code-signing identity; never create or trust a certificate."""
import argparse
from dataclasses import dataclass
import fcntl
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import subprocess
import sys
import tempfile
from typing import Optional

BUNDLE_ID = "com.nextui.brickmic"
INSTALLED = Path("/Applications/Brick Mic.app")
PIN_FILE = Path.home() / "Library/Application Support/Brick Mic/signing-identity.json"


class SigningError(RuntimeError):
    pass


@dataclass(frozen=True)
class Signature:
    identifier: str
    adhoc: bool
    requirement: str
    fingerprint: Optional[str]


def command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=15)
    if result.returncode:
        # Do not print identity inventories or request private-key access here.
        raise SigningError(f"Signing check failed: {Path(arguments[0]).name}")
    return result.stdout + result.stderr


def parse_identities(output):
    return {fingerprint.upper(): name for fingerprint, name in
            re.findall(r'^\s*\d+\)\s+([0-9A-Fa-f]{40})\s+"([^"]+)"', output, re.MULTILINE)}


def identities():
    return parse_identities(command(["/usr/bin/security", "find-identity", "-v", "-p", "codesigning"]))


def resolve_identity(requested, available):
    if not requested or requested == "-":
        raise SigningError("A fixed certificate identity is required; adhoc signing is forbidden.")
    requested = requested.strip()
    matches = [fingerprint for fingerprint, name in available.items()
               if requested.upper() == fingerprint or requested == name]
    if len(matches) != 1:
        raise SigningError("The specified existing code-signing identity is unavailable or ambiguous.")
    return matches[0]


def load_pin(path=PIN_FILE):
    try:
        metadata = path.lstat()
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_uid != os.getuid() or metadata.st_nlink != 1:
            raise SigningError("The signing pin must be a regular file owned by the current user.")
        pin = json.loads(path.read_text())
    except FileNotFoundError:
        raise SigningError("Fixed signing identity is not configured. Use mac-signing.py init --identity '<existing certificate>' only after the user specifies it; the installed app was not changed.")
    if (not isinstance(pin, dict) or pin.get("version") != 1 or pin.get("bundle_id") != BUNDLE_ID
            or not isinstance(pin.get("fingerprint"), str)
            or not re.fullmatch(r"[0-9A-F]{40}", pin["fingerprint"])):
        raise SigningError("Invalid fixed signing configuration; it will not be replaced automatically.")
    for field in ("designated_requirement", "initial_adhoc_requirement"):
        if pin.get(field) is not None and (not isinstance(pin[field], str) or not pin[field]):
            raise SigningError("Invalid pinned signing requirement.")
    return pin


def inspect_app(app):
    with (app / "Contents/Info.plist").open("rb") as handle:
        info = plistlib.load(handle)
    if info.get("CFBundleIdentifier") != BUNDLE_ID or info.get("CFBundleExecutable") != "BrickMic":
        raise SigningError("The application is not Brick Mic with the expected Bundle ID and executable.")
    command(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)])
    details = command(["/usr/bin/codesign", "-d", "-r-", "--verbose=2", str(app)])
    identifier = re.search(r"^Identifier=(.+)$", details, re.MULTILINE)
    requirement = re.search(r"^(?:# )?designated => (.+)$", details, re.MULTILINE)
    if not identifier or identifier[1] != BUNDLE_ID or not requirement:
        raise SigningError("The signed Bundle ID or designated requirement is missing or invalid.")
    adhoc = bool(re.search(r"^Signature=adhoc$", details, re.MULTILINE))
    fingerprint = None
    if not adhoc:
        with tempfile.TemporaryDirectory(prefix="brick-mic-public-certificate-") as folder:
            prefix = str(Path(folder) / "certificate")
            # This option has an optional value: an equals sign keeps codesign
            # from treating the public certificate prefix as another bundle.
            command(["/usr/bin/codesign", "--display", "--extract-certificates=" + prefix, str(app)])
            leaf = Path(prefix + "0")
            if not leaf.is_file():
                raise SigningError("The application's public leaf certificate could not be inspected.")
            fingerprint = hashlib.sha1(leaf.read_bytes()).hexdigest().upper()
    return Signature(identifier[1], adhoc, requirement[1], fingerprint)


def check_installed(pin, installed):
    if installed is None:
        return
    if installed.identifier != BUNDLE_ID:
        raise SigningError("The installed app has a different signed Bundle ID.")
    if installed.adhoc:
        if pin.get("designated_requirement") or pin.get("initial_adhoc_requirement") != installed.requirement:
            raise SigningError("The installed app is adhoc; certificate migration requires explicit initialization approval.")
    else:
        if installed.fingerprint != pin["fingerprint"]:
            raise SigningError("The installed app uses a different certificate; refusing to change its identity.")
        if pin.get("designated_requirement") and installed.requirement != pin["designated_requirement"]:
            raise SigningError("The installed app's designated requirement differs from the signing pin.")


def check_candidate(pin, candidate, installed):
    check_installed(pin, installed)
    if candidate.identifier != BUNDLE_ID or candidate.adhoc or candidate.fingerprint != pin["fingerprint"]:
        raise SigningError("Refusing an adhoc app or a certificate different from the fixed signing identity.")
    if not re.search(r'\bidentifier\s+"' + re.escape(BUNDLE_ID) + r'"', candidate.requirement):
        raise SigningError("The candidate's designated requirement does not bind the Brick Mic identifier.")
    expected = pin.get("designated_requirement")
    if installed is not None and not installed.adhoc:
        expected = installed.requirement
    if expected and candidate.requirement != expected:
        raise SigningError("The candidate's designated requirement changed; the installed app was not replaced.")


def signing_identity(pin, available, requested=None):
    fingerprint = resolve_identity(requested or pin["fingerprint"], available)
    if fingerprint != pin["fingerprint"]:
        raise SigningError("The requested certificate differs from the existing pin; the pin will not be updated.")
    return fingerprint


def initialize(identity, allow_initial_migration, path=PIN_FILE, installed_path=INSTALLED):
    if path.exists() or path.is_symlink():
        raise SigningError("A signing identity is already pinned; initialization cannot replace it.")
    available = identities()
    fingerprint = resolve_identity(identity, available)
    installed = inspect_app(installed_path) if installed_path.exists() else None
    requirement = None
    initial_adhoc = None
    if installed is not None:
        if installed.adhoc:
            if not allow_initial_migration:
                raise SigningError("Current app is adhoc. Initial certificate migration must be explicitly authorized with --allow-initial-migration.")
            initial_adhoc = installed.requirement
        elif installed.fingerprint != fingerprint:
            raise SigningError("The specified certificate differs from the installed app; refusing to pin a replacement.")
        else:
            requirement = installed.requirement
    pin = {"version": 1, "bundle_id": BUNDLE_ID, "fingerprint": fingerprint,
           "name": available[fingerprint], "designated_requirement": requirement,
           "initial_adhoc_requirement": initial_adhoc}
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "w") as handle:
        json.dump(pin, handle, indent=2)
        handle.write("\n")
    print(f"Pinned existing public signing identity: {available[fingerprint]} ({fingerprint}). No app was signed or installed.")


def seal(path=PIN_FILE, installed_path=INSTALLED):
    """Explicitly finish the first migration without changing the certificate."""
    load_pin(path)  # A missing/unsafe pin must not create a directory or lock.
    descriptor = os.open(path.with_suffix(".lock"), os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    temporary = None
    try:
        lock_metadata = os.fstat(descriptor)
        if (not stat.S_ISREG(lock_metadata.st_mode) or lock_metadata.st_uid != os.getuid()
                or lock_metadata.st_nlink != 1):
            raise SigningError("The signing configuration lock is not a regular file owned by the current user.")
        os.fchmod(descriptor, 0o600)
        fcntl.flock(descriptor, fcntl.LOCK_EX)
        pin = load_pin(path)
        original = path.read_bytes()
        metadata = path.lstat()
        signing_identity(pin, identities())
        installed = inspect_app(installed_path)
        check_candidate(pin, installed, installed)
        if pin.get("designated_requirement"):
            print("Fixed signing requirement is already sealed and matches the installed app; configuration was not changed.")
            return
        sealed = {**pin, "designated_requirement": installed.requirement,
                  "initial_adhoc_requirement": None}
        temp_descriptor, temp_name = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
        temporary = Path(temp_name)
        with os.fdopen(temp_descriptor, "w") as handle:
            os.fchmod(handle.fileno(), 0o600)
            json.dump(sealed, handle, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        current = load_pin(path)
        current_metadata = path.lstat()
        if (current != pin or path.read_bytes() != original
                or (current_metadata.st_dev, current_metadata.st_ino) != (metadata.st_dev, metadata.st_ino)):
            raise SigningError("Signing configuration changed during sealing; it was not overwritten.")
        os.replace(temporary, path)
        temporary = None
        print("Sealed the installed fixed certificate's designated requirement; the initial adhoc migration permission was removed.")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        os.close(descriptor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="operation", required=True)
    initialize_parser = commands.add_parser("init", help="Explicitly pin an already-existing certificate; never create one.")
    initialize_parser.add_argument("--identity", required=True)
    initialize_parser.add_argument("--allow-initial-migration", action="store_true", help="Explicitly authorize migration of the current exact adhoc app; does not replace a previously fixed certificate.")
    commands.add_parser("seal", help="Explicitly finish first migration: pin the installed certificate's requirement and remove adhoc migration permission.")
    commands.add_parser("identity", help="Validate configuration and print the pinned fingerprint for codesign.")
    verify_parser = commands.add_parser("verify", help="Verify a bundle against the pin and installed signing requirement.")
    verify_parser.add_argument("app", type=Path)
    args = parser.parse_args()
    if args.operation == "init":
        initialize(args.identity, args.allow_initial_migration)
        return
    if args.operation == "seal":
        seal()
        return
    pin = load_pin()
    fingerprint = signing_identity(pin, identities(), os.environ.get("BRICK_MIC_SIGNING_IDENTITY"))
    installed = inspect_app(INSTALLED) if INSTALLED.exists() else None
    check_installed(pin, installed)
    if args.operation == "identity":
        print(fingerprint)
    else:
        check_candidate(pin, inspect_app(args.app), installed)


if __name__ == "__main__":
    try:
        main()
    except (SigningError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"Brick Mic signing blocked: {error}", file=sys.stderr)
        sys.exit(2)
