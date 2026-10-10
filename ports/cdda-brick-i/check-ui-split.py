#!/usr/bin/env python3
"""Prove UI extraction does not carry memory/storage patches into clean I source."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source_archive', type=Path)
parser.add_argument('output', type=Path, help='A new directory; existing output is never replaced')
args = parser.parse_args()
port = Path(__file__).resolve().parent
pins = dict(line.split('=', 1) for line in (port / 'pins.env').read_text().splitlines() if '=' in line)
assert hashlib.sha256(args.source_archive.read_bytes()).hexdigest() == pins['CDDA_SOURCE_SHA256']
args.output.mkdir(parents=True, exist_ok=False)
tree = args.output / 'source'
tree.mkdir()
prefix = 'Cataclysm-DDA-' + pins['CDDA_REV'] + '/'
with tarfile.open(args.source_archive) as tar:
    for entry in tar:
        if not entry.name.startswith(prefix):
            continue
        rel = Path(entry.name[len(prefix):])
        if not rel.parts or (rel.parts[0] != 'src' and str(rel) not in ('Makefile', 'CMakeLists.txt')):
            continue
        assert '..' not in rel.parts and not rel.is_absolute()
        target = tree / rel
        if entry.isdir():
            target.mkdir(parents=True, exist_ok=True)
        elif entry.isfile():
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(tar.extractfile(entry).read())
        else:
            raise RuntimeError('Unexpected non-file source entry: ' + entry.name)

def snapshot():
    return {str(p.relative_to(tree)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(tree.rglob('*')) if p.is_file()}

before = snapshot()
for pass_no in (1, 2):
    subprocess.run(['python3', str(port / 'apply-port.py'), str(tree)], check=True)
    after = snapshot()
    if pass_no == 1:
        first = after
    else:
        assert after == first, 'Repeated UI application changed source'

allowed = {'src/sdltiles.cpp', 'src/font_loader.cpp', 'src/sdl_font.cpp',
           'src/input_context.cpp', 'src/cata_imgui.cpp', 'src/input_popup.cpp',
           'src/uilist.cpp', 'src/third-party/imgui/imgui_draw.cpp',
           'src/third-party/imgui/imgui_impl_sdlrenderer2.cpp', 'src/inventory_ui.cpp',
           'src/crafting_gui.cpp', 'src/overmap_ui.cpp', 'src/advanced_inv.cpp',
           'src/output.cpp', 'src/game.cpp'}
changed = sorted(p for p, value in before.items() if after.get(p) != value)
added = sorted(set(after) - set(before))
assert set(changed) <= allowed, changed
assert set(added) == {'src/brick_input.cpp', 'src/brick_input.h', 'src/brick_gui.h',
                      'src/brick_gui_fallback.h', 'src/brick_ui.h'}, added
assert all(before[p] == after[p] for p in before if p not in allowed)
# game.cpp is a mixed upstream file: permit precisely the sidebar measurement,
# not save/load, memory-pressure or map-lifetime changes.
original_game = None
with tarfile.open(args.source_archive) as tar:
    original_game = tar.extractfile(prefix + 'src/game.cpp').read()
sidebar_probe = args.output / 'sidebar-only'
(sidebar_probe / 'src').mkdir(parents=True)
(sidebar_probe / 'src/game.cpp').write_bytes(original_game)
subprocess.run(['python3', str(port / 'sidebar-layout.py'), str(sidebar_probe)], check=True)
assert (sidebar_probe / 'src/game.cpp').read_bytes() == (tree / 'src/game.cpp').read_bytes()

for p in changed + added:
    text = (tree / p).read_text()
    for marker in ('brick_release_loading_memory', 'brick_memory', 'brick_pressure',
                   'brick_reload', 'brick_queue', 'brick_parsed_pack', 'brick_validation',
                   'brick_map_reclaim', 'brick_placement', 'brick_compression_cache', 'brick_power'):
        assert marker not in text, (p, marker)

result = {'pinned_archive_verified': True, 'ui_source_replay_passes': 2,
          'idempotent': True, 'original_files': len(before),
          'changed_ui_files': changed, 'added_ui_files': added,
          'all_other_upstream_files_byte_equal': True,
          'game_cpp_only_sidebar_measurement': True,
          'steam_deck_build_verified': False, 'native_verified': False}
(args.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
