"""Package actual libobs GPU captures as a timed animated preview."""
from pathlib import Path
from statistics import median
import argparse
from PIL import Image

root = Path(__file__).resolve().parents[1]
validation = root / 'validation'
parser = argparse.ArgumentParser()
parser.add_argument('--kind', choices=('scroll', 'blocks', 'alternating', 'clock', 'broadcast'), default='scroll')
kind = parser.parse_args().kind
stem = kind + '-preview'
width, height = (1370,73) if kind=='broadcast' else (960,92)
stride = width * height * 4
raw = (validation / (stem + '.rgba')).read_bytes()
times = [int(line) for line in (validation / (stem + '-timing.txt')).read_text().splitlines()]
if len(times) < 2 or len(raw) != len(times) * stride:
    raise SystemExit('Capture dimensions, frame count or timing do not match')
frames = [Image.frombytes('RGBA', (width, height), raw[i*stride:(i+1)*stride]) for i in range(len(times))]
still = validation / 'broadcast-still.rgba'
if kind=='broadcast' and still.exists():
    Image.frombytes('RGBA',(width,height),still.read_bytes()).save(validation/(stem+'.png'))
else:
    frames[0].save(validation / (stem + '.png'))
durations = [max(10, round((b-a)/1e7)*10) for a, b in zip(times, times[1:])]
durations.append(round(median(durations)))
palette = frames[0].convert('RGB').quantize(colors=256)
indexed = [palette] + [frame.convert('RGB').quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames[1:]]
indexed[0].save(validation / (stem + '.gif'), save_all=True, append_images=indexed[1:],
                duration=durations, loop=0, disposal=2, optimize=False)
print(f'Saved {len(frames)} real GPU frames, {sum(durations)/1000:.2f} seconds')
