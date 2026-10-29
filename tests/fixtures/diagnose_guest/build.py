#!/usr/bin/env python3
"""Three source builds for a public diagnose/fix exercise; GNU Binutils 2.44."""
import argparse,hashlib,importlib.util,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parent
BASE=ROOT.parent/'stream_guest'
SPEC=importlib.util.spec_from_file_location('stream_build',BASE/'build.py')
stream=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(stream)
GOOD='    R2 = 0x40;\n'
BAD='    R2 = 0x20;\n'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def sources():
 a=(BASE/'stream.s').read_text();assert a.count(GOOD)==1
 b=a.replace(GOOD,BAD);assert b.count(BAD)==1
 c=b.replace(BAD,GOOD);assert c==a
 return {'reference':a,'defective':b,'corrected':c}
def build(output,assembler,linker):
 output=Path(output);output.mkdir(parents=True,exist_ok=True)
 tools={}
 for name,path in [('assembler',assembler),('linker',linker)]:
  path=Path(path).resolve();version=subprocess.check_output([path,'--version'],text=True).splitlines()[0]
  if '2.44' not in version:raise ValueError('requires GNU Binutils 2.44: '+version)
  tools[name]={'path':str(path),'sha256':sha(path),'version':version}
 manifest={'schema':1,'toolchain':'GNU Binutils 2.44, bfin-elf, -mcpu=bf524; no C compiler/runtime','sources':{str(p.relative_to(ROOT.parent)):sha(p) for p in [ROOT/'build.py']+[BASE/n for n in ['stream.s','codec.inc','codec-macros.inc','link.ld','build.py']]},'images':{}};commands=[]
 for name,source in sources().items():
  d=output/name;d.mkdir(exist_ok=True);(d/'guest.s').write_text(source)
  cmd=[str(assembler),'-mcpu=bf524','--fatal-warnings','-I',str(BASE),'--defsym','STREAM_CASE=4','--defsym','STREAM_RATE=44100','-o',str(d/'guest.o'),str(d/'guest.s')]
  subprocess.run(cmd,check=True);commands.append(cmd)
  cmd=[str(linker),'-T',str(BASE/'link.ld'),'-o',str(d/'guest.elf'),str(d/'guest.o')];subprocess.run(cmd,check=True);commands.append(cmd)
  image,symbols=stream.extract((d/'guest.elf').read_bytes());(d/'guest.ldr').write_bytes(image)
  manifest['images'][name]={'source_sha256':sha(d/'guest.s'),'image_sha256':sha(d/'guest.ldr'),'image_bytes':len(image),'symbols':symbols}
 (output/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
 (output/'build-receipt.json').write_text(json.dumps({'tools':tools,'commands':commands},indent=2,sort_keys=True)+'\n')
 return manifest

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--as',dest='assembler',required=True,type=Path);p.add_argument('--ld',dest='linker',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--check',action='store_true');p.add_argument('--update',action='store_true');a=p.parse_args()
 m=build(a.output,a.assembler,a.linker)
 if a.check:
  assert m==json.loads((ROOT/'manifest.json').read_text()),'manifest mismatch'
  for name in m['images']:assert (a.output/name/'guest.ldr').read_bytes()==(ROOT/(name+'.ldr')).read_bytes(),name
 if a.update:
  for name in m['images']:(ROOT/(name+'.ldr')).write_bytes((a.output/name/'guest.ldr').read_bytes())
  (ROOT/'manifest.json').write_text(json.dumps(m,indent=2,sort_keys=True)+'\n')
 print(json.dumps({name:v['image_sha256'] for name,v in m['images'].items()},indent=2))
if __name__=='__main__':main()
