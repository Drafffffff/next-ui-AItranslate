#!/usr/bin/env python3
"""Send native SDL virtual joystick events to virtual-controller.cpp's FIFO."""
import argparse
import os
import time

BUTTONS = dict(A=1, B=0, X=3, Y=2, L=4, R=5, SELECT=6, START=7, MENU=8, FN1=9, FN2=10)
HATS = dict(UP=1, RIGHT=2, DOWN=4, LEFT=8, UPRIGHT=3, DOWNRIGHT=6, DOWNLEFT=12, UPLEFT=9)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('fifo')
parser.add_argument('action', choices=['tap', 'chord', 'hat', 'capture'])
parser.add_argument('name')
args = parser.parse_args()
name = args.name.upper()
mapping = HATS if args.action == 'hat' else BUTTONS
if args.action == 'capture' and not all(c.isalnum() or c in '_-' for c in args.name):
    parser.error('Capture name must contain only letters, digits, underscore or dash')
if args.action != 'capture' and name not in mapping:
    parser.error(f'Unknown input {name}; choose from {", ".join(mapping)}')
fd = os.open(args.fifo, os.O_WRONLY | os.O_NONBLOCK)


def send(command):
    os.write(fd, (command + '\n').encode())
    time.sleep(0.08)


try:
    if args.action == 'capture':
        send(f'C {args.name}')
    elif args.action == 'hat':
        send(f'H {mapping[name]}')
        send('H 0')
    else:
        if args.action == 'chord':
            send('B 8 1')
        send(f'B {mapping[name]} 1')
        send(f'B {mapping[name]} 0')
        if args.action == 'chord':
            send('B 8 0')
finally:
    os.close(fd)
