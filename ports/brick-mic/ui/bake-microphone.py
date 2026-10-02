#!/usr/bin/env python3
"""Bake the user-supplied SVG once; runtime only colors its antialiased mask."""
from pathlib import Path
import subprocess, tempfile, xml.etree.ElementTree as ET
from fontTools.svgLib.path import parse_path
from fontTools.pens.svgPathPen import SVGPathPen

root = Path(__file__).resolve().parents[3]
ui = Path(__file__).resolve().parent
svg = ET.parse(ui / 'assets/microphone.svg').getroot()
# PocketJS's offline baker does not accept SVG arcs. FontTools converts arcs to
# cubic Beziers without altering the supplied path geometry or using a network.
paths = []
for element in svg:
    pen = SVGPathPen(None)
    parse_path(element.attrib['d'], pen)
    paths.append('<path fill="#ffffff" d="' + pen.getCommands() + '"/>')
with tempfile.TemporaryDirectory(prefix='brick-microphone-') as folder:
    source = Path(folder) / 'microphone.svg'
    source.write_text('<svg width="40" height="40" viewBox="0 0 1024 1024">' + ''.join(paths) + '</svg>')
    script = Path(folder) / 'bake.ts'
    script.write_text('''import {bakeSvg} from ''' + repr(str(root / 'build/pocketjs-port/upstream/framework/compiler/bake-svg.ts')) + ''';
const image=bakeSvg(await Bun.file(process.argv[2]).text());
const bytes=image.rgba;
const mask=Array.from({length:image.width*image.height},(_,i)=>bytes[i*4+3].toString(16).padStart(2,'0')).join('');
console.log('// Generated from assets/microphone.svg by bake-microphone.py.');
console.log('export const microphoneAlpha="'+mask+'";');
''')
    result = subprocess.run([str(root / 'build/pocketjs-port/bun/bun-darwin-aarch64/bun'), str(script), str(source)], capture_output=True, text=True, check=True)
    (ui / 'microphone-mask.ts').write_text(result.stdout)
