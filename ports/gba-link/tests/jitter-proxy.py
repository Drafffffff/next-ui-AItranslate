#!/usr/bin/env python3
"""Local-only TCP burst fixture; arm by writing milliseconds to --control.

Keeps every byte in order. Once armed, holds each direction for that duration
every two seconds, letting TCP accumulate a burst as a congested AP can do.
Never binds a public address or touches handheld configuration.
"""
import argparse
import asyncio
from pathlib import Path
import time


async def serve(args):
    stats = {'holds': 0, 'bytes': 0, 'largest_read': 0}
    origin = time.monotonic()

    async def copy(reader, writer):
        while data := await reader.read(65536):
            try:
                delay = max(0, min(500, int(args.control.read_text().strip()))) / 1000
            except (OSError, ValueError):
                delay = 0
            phase = (time.monotonic() - origin) % 2
            if phase < delay:
                stats['holds'] += 1
                await asyncio.sleep(delay - phase)
            stats['bytes'] += len(data)
            stats['largest_read'] = max(stats['largest_read'], len(data))
            writer.write(data)
            await writer.drain()

    async def client(reader, writer):
        target_writer = None
        try:
            target_reader, target_writer = await asyncio.open_connection('127.0.0.1', args.target_port)
            tasks = [asyncio.create_task(copy(reader, target_writer)),
                     asyncio.create_task(copy(target_reader, writer))]
            done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in pending:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
        finally:
            writer.close()
            if target_writer:
                target_writer.close()
            print(stats, flush=True)

    server = await asyncio.start_server(client, '127.0.0.1', args.port)
    print(f'Local jitter proxy: {args.port} -> {args.target_port}', flush=True)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--target-port', type=int, required=True)
    parser.add_argument('--control', type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.target_port <= 65535 or args.port == args.target_port:
        parser.error('Use two distinct valid ports')
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass
