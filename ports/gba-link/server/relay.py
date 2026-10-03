#!/usr/bin/env python3
"""NextUI two-player packet relay. Both consoles connect outbound.
Own protocol; no ROMs, saves, or RetroArch relay interoperability.
Run on a reachable host: python3 relay.py --bind 0.0.0.0 --port 55435
A room code is a pairing hint, not an authentication or encryption mechanism.
"""
import argparse
import asyncio
import contextlib
import time


class Relay:
    def __init__(self, delay_ms=0, chunks=65536):
        self.rooms = {}
        self.delay = delay_ms / 1000
        self.chunks = chunks
        self.connections = 0

    async def client(self, reader, writer):
        self.connections += 1
        entry = None
        room = None
        try:
            if self.connections > 128:
                return
            prefix = await asyncio.wait_for(reader.readexactly(16), 5)
            if prefix[:8] != b'NXRELAY1' or prefix[-1:] != b'\n' or prefix[14:15] not in (b'H', b'J'):
                return
            room = prefix[8:14]
            if len(room) != 6 or not room.isdigit():
                return
            role = prefix[14:15]
            entries = self.rooms.setdefault(room, {})
            if role in entries:
                return
            entry = (reader, writer, asyncio.Future())
            entries[role] = entry
            other = entries.get(b'J' if role == b'H' else b'H')
            if other:
                # Keep a room reserved until both peers close; prevent a third
                # participant with the same code from joining an ongoing match.
                entry[2].set_result(other)
                other[2].set_result(entry)
            other = await asyncio.wait_for(asyncio.shield(entry[2]), 120)
            destination = other[1]
            while data := await reader.read(self.chunks):
                if self.delay:
                    await asyncio.sleep(self.delay)
                destination.write(data)
                await destination.drain()
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, ConnectionError, OSError):
            pass
        finally:
            self.connections -= 1
            if entry is not None:
                entries = self.rooms.get(room, {})
                for role, candidate in list(entries.items()):
                    if candidate is entry:
                        del entries[role]
                if not entries:
                    self.rooms.pop(room, None)
                if entry[2].done() and not entry[2].cancelled():
                    with contextlib.suppress(Exception):
                        entry[2].result()[1].close()
                elif not entry[2].done():
                    entry[2].cancel()
            writer.close()
            with contextlib.suppress(ConnectionError, OSError):
                await writer.wait_closed()


async def main(args):
    relay = Relay(args.delay_ms, args.chunk_bytes)
    server = await asyncio.start_server(relay.client, args.bind, args.port, limit=65536)
    print(f'NextUI relay listening on {args.bind}:{args.port}', flush=True)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=55435)
    parser.add_argument('--delay-ms', type=float, default=0, help='Test-only per-chunk forwarding delay')
    parser.add_argument('--chunk-bytes', type=int, default=65536, help='Test-only TCP fragmentation')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or args.delay_ms < 0 or not 1 <= args.chunk_bytes <= 65536:
        parser.error('invalid port, delay, or chunk size')
    try:
        asyncio.run(main(args))
    except KeyboardInterrupt:
        pass
