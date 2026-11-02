"""Fresh, aspect-preserving side-by-side review of the six Compact headers."""
from pathlib import Path
from PIL import Image,ImageDraw
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--tag',required=True);parser.add_argument('--blank',action='store_true');args=parser.parse_args()
if not args.tag.replace('-','').replace('_','').isalnum():raise SystemExit('Invalid review tag')
out=Path(__file__).resolve().parents[1]/'validation'
canvas=Image.new('RGB',(920,6*178),(23,27,33));draw=ImageDraw.Draw(canvas)
for i,name in enumerate(['prime','glass','neon','sport','magazine','dark']):
 ref=Image.open(out/f'reference-{name}.png').convert('RGB')
 native=Image.open(out/f'style-{name}.png').convert('RGBA')
 if args.blank:native=Image.frombytes('RGBA',native.size,(out/f'header-blank-{name}.rgba').read_bytes())
 native=native.crop((15,1,native.width-15,native.height-1))
 back=Image.new('RGBA',native.size,(23,27,33,255));back.alpha_composite(native);native=back.convert('RGB')
 for col,(im,label) in enumerate([(ref,'REFERENCIA'),(native,'SIN TEXTO' if args.blank else 'RECONSTRUIDA')]):
  draw.text((12+col*460,i*178+4),name+' | '+label,fill='white')
  crop=im.crop((0,0,220,min(75,im.height)));crop=crop.resize((440,crop.height*2),Image.Resampling.NEAREST)
  canvas.paste(crop,(12+col*460,i*178+22))
result=out/f'compact-headers-{args.tag}.png';canvas.save(result);print(result)
