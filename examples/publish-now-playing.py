"""One-shot development publisher. Does not detect DJ software or infer playback."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import json
import os
from pathlib import Path
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--state', required=True, choices=('playing', 'paused', 'stopped', 'waiting', 'unavailable'))
parser.add_argument('--artist', default='')
parser.add_argument('--title', default='')
args = parser.parse_args()
if not args.output.is_absolute() or str(args.output).startswith(('\\\\', '//')):
    parser.error('Choose an absolute local-disk output path.')
payload = {'schema': 'nexus.now-playing.v1', 'state': args.state, 'updated_at': time.time()}
if args.state == 'playing':
    if not args.title.strip(): parser.error('--title is required for playing state.')
    for label, value, limit in [('artist', args.artist, 256), ('title', args.title, 512)]:
        if len(value.encode('utf-8')) > limit or any(ord(c) < 32 or ord(c) == 127 for c in value):
            parser.error(f'{label} exceeds its size limit or contains control characters.')
    payload.update(artist=args.artist, title=args.title)
raw = json.dumps(payload, ensure_ascii=False).encode('utf-8')
temporary = None
try:
    with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix=args.output.name+'.', suffix='.tmp', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, args.output)
finally:
    if temporary is not None and temporary.exists(): temporary.unlink()
print(f'Published one {args.state} update to {args.output}. It expires without further heartbeats.')
