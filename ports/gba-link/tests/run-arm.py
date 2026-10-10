#!/usr/bin/env python3
"""Two actual tg5040 gpSP binaries in the fixed ARM64 SDK (not a Brick benchmark)."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
LOCK=json.loads((ROOT/'ports/gba-link/versions.json').read_text())
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--netem',action='store_true',help='Use only this disposable container network: 25ms delay + 2 percent loss')
args=parser.parse_args()
( ROOT/'build/gba-link/arm-duel-host-input.txt').write_text('100 120 256\n200 220 256\n')
( ROOT/'build/gba-link/arm-duel-join-input.txt').write_text('150 170 256\n')
script=r"""
set -e
cd /work
${CROSS_COMPILE}gcc -std=c99 -Wall -Wextra -Werror -Iworkspace/all/minarch -Iworkspace/all/minarch/libretro-common/include ports/gba-link/tests/core-runner.c workspace/all/minarch/ma_netplay.c -lcrypto -ldl -o build/gba-link/core-runner-arm64.elf
"""
if args.netem:
    script+=r"""
apt-get update -qq
apt-get install -y -qq iproute2
tc qdisc add dev lo root netem delay 25ms loss 2%
"""
script+=r"""
sdk=/opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc
NP_DUEL=1 NP_SCRIPT=build/gba-link/arm-duel-host-input.txt NP_DYNAREC=1 "$sdk/lib/ld-2.28.so" --library-path "$sdk/lib:$sdk/usr/lib" build/gba-link/core-runner-arm64.elf build/gba-link/brick/gpsp_libretro.so build/gba-link/link-probe.gba host 127.0.0.1 55435 mul_poke 600 >build/gba-link/arm-host.log 2>&1 &
peer=$!
sleep .2
NP_DUEL=1 NP_SCRIPT=build/gba-link/arm-duel-join-input.txt NP_DYNAREC=1 "$sdk/lib/ld-2.28.so" --library-path "$sdk/lib:$sdk/usr/lib" build/gba-link/core-runner-arm64.elf build/gba-link/brick/gpsp_libretro.so build/gba-link/link-probe.gba join 127.0.0.1 55435 mul_poke 600 >build/gba-link/arm-join.log 2>&1
wait "$peer"
"""
if args.netem:
    script+=r"""
tc -s qdisc show dev lo
"""
if args.netem:
    script=script.replace('arm-host.log','netem-host.log').replace('arm-join.log','netem-join.log')
command=['docker','run','--rm','--platform','linux/arm64']
if args.netem:command+=['--cap-add','NET_ADMIN']
command+=['-v',str(ROOT)+':/work',LOCK['toolchain'],'bash','-lc',script]
path=ROOT/'build/gba-link'/('netem-test.log' if args.netem else 'arm-test.log')
with open(path,'w') as output:
    result=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT)
print(path.read_text())
raise SystemExit(result.returncode)
