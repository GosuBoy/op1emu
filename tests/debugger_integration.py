#!/usr/bin/env python3
"""Public read-only debugger: real CPU/audio versus original capture, no private inputs."""
import importlib.util,json,struct,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('stream',ROOT/'tests/stream_guest_integration.py');stream=importlib.util.module_from_spec(spec);spec.loader.exec_module(stream)
def dump(path,commands):
 with path.open('a') as f:
  for command in commands:f.write(json.dumps(command)+'\n')
class Control:
 def __init__(self,commands,log,proc):self.commands=commands;self.log=log;self.proc=proc;self.sequence=0;self.offset=0
 def receive(self,predicate):
  end=time.monotonic()+10
  while time.monotonic()<end:
   lines=self.log.read_text().splitlines() if self.log.exists() else []
   while self.offset<len(lines):
    line=json.loads(lines[self.offset]);self.offset+=1
    if predicate(line):return line
   if self.proc.poll() is not None:raise AssertionError('emulator exited before debugger response')
   time.sleep(.002)
  raise AssertionError('host timeout awaiting debugger response')
 def command(self,cmd,**args):
  self.sequence+=1;dump(self.commands,[dict(cmd=cmd,id=self.sequence,**args)])
  return self.receive(lambda r:r.get('request',{}).get('id')==self.sequence)
 def stop(self):
  self.command('wait-stop');return self.receive(lambda r:r.get('kind')=='stop')['status']
 def snapshot(self):
  r=self.command('snapshot');assert r['ok'];return r['snapshot']
 def stable(self):
  a=self.snapshot();time.sleep(.05);b=self.snapshot();assert a==b,'stopped guest or device state changed with wall time';return a
 def resume(self):assert self.command('continue')['ok']
def native_command(native,out,mode,debug=False,commands=None):
 image='work-44100' if mode=='work' else 'normal-44100'
 args=[native,ROOT/'tests/fixtures/stream_guest'/(image+'.ldr'),out,mode,'--rate','44100']
 if debug:args+=['--chain','--debug-enable']
 if commands:args+=['--debug-commands',commands,'--debug-log',out.with_suffix('.debug.jsonl')]
 return list(map(str,args)),image
def run(native,out,mode,debug=False):
 args,image=native_command(native,out,mode,debug)
 with tempfile.TemporaryDirectory(prefix='debug-public-') as d:
  r=subprocess.run(args,cwd=d,capture_output=True,text=True,timeout=40);assert r.returncode==0,r.stdout+r.stderr
 return image

def compare(a,b):
 for ext in ['.pcm','.csv','.json']:assert a.with_suffix(ext).read_bytes()==b.with_suffix(ext).read_bytes(),str(b)+ext

def main():
 executable=Path(sys.argv[1]).resolve();terminal=Path(sys.argv[2]).resolve();out=Path(sys.argv[3]).resolve();out.mkdir(exist_ok=True,parents=True)
 manifest=json.loads((ROOT/'tests/fixtures/stream_guest/manifest.json').read_text())
 for mode in ['work','pause']:
  a=out/(mode+'-reference');b=out/(mode+'-enabled');c=out/(mode+'-resumed')
  image=run(executable,a,mode);run(executable,b,mode,True);compare(a,b)
  symbols=manifest['images'][image]['symbols'];commands=c.with_suffix('.commands.jsonl');commands.write_text('')
  c.with_suffix('.debug.jsonl').write_text('') # never consume a prior session's responses
  first='work_wait' if mode=='work' else 'idle_dma_resume'
  dump(commands,[{'cmd':'break','pc':symbols[first]},{'cmd':'wait-stop'}]);args,_=native_command(executable,c,mode,True,commands)
  with tempfile.TemporaryDirectory(prefix='debug-public-') as d,c.with_suffix('.log').open('w') as log:
   p=subprocess.Popen(args,cwd=d,stdout=log,stderr=subprocess.STDOUT);control=Control(commands,c.with_suffix('.debug.jsonl'),p)
   try:
    control.receive(lambda r:r.get('kind')=='stop');s=control.stable();assert s['reason']=='pc' and s['pc']==symbols[first]
    assert s['sport'][0]['tcr1']&1 and s['codec']['frame_hz']==44100
    if mode=='work':assert s['sport'][0]['shifting'] is not None
    assert not control.command('ram',address=0xffc00810,bytes=4)['ok']
    assert not control.command('ram',address=0xfffffff0,bytes=64)['ok']
    assert not control.command('break',pc=symbols[first]+1)['ok']
    assert not control.command('invalid')['ok']
    assert control.command('ram',address=0x2000,bytes=256)['ok']
    if mode=='work':
     control.resume();again=control.stop();assert again['pc']==symbols[first]
     assert again['targets'][0]['hits']==2 and again['packets']>s['packets']
     control.command('remove',pc=symbols[first]);control.command('break',pc=symbols['dma_isr']);control.resume();control.stop()
     irq=control.stable();assert irq['interrupts']['ipend']&0x200 and irq['dma4']['done']
     control.command('remove',pc=symbols['dma_isr']);control.command('break',pc=symbols['start_dma']);control.resume();control.stop()
     call=control.stable();assert call['pc']==symbols['start_dma'] and call['cpu']['rets']
     control.command('remove',pc=symbols['start_dma'])
    else:
     assert s['phase']=='before-idle-wait' and s['cpu']['idle']
     # IDLE wakes for SPORT progress too. Select the main restart before
     # work33, then its next IDLE, rather than counting unrelated wakes.
     control.command('remove',pc=symbols[first]);control.command('break',pc=symbols['start_dma'])
     for i in range(40):
      control.resume();control.stop();s=control.snapshot()
      if s['dma4']['work']==32:break
     else:raise AssertionError('restart before queued clock stimulus not reached')
     control.command('remove',pc=symbols['start_dma']);control.command('break',pc=symbols[first])
     control.resume();control.stop();frozen=control.stable()
     assert frozen['phase']=='before-idle-wait' and frozen['idle_wait_pending']
     assert frozen['timer']['expiry_cycles']>frozen['cycles'] and frozen['events']['count']==4
     assert frozen['sport'][0]['shifting'] is not None
     control.command('remove',pc=symbols[first])
    control.resume();assert p.wait(timeout=20)==0
   finally:
    if p.poll() is None:p.kill();p.wait()
  compare(a,c);stream.check_capture(c,mode,json.loads(c.with_suffix('.json').read_text()),symbols,44100)
  print(mode,'original == debug enabled == stopped/resumed PCM, CSV and JSON',flush=True)
 # Standalone local command executable: explicit cancellation, invalid modes and wall timeout.
 with tempfile.TemporaryDirectory(prefix='debug-terminal-') as d:
  d=Path(d);cmd=d/'commands';log=d/'debug.jsonl';cmd.write_text('')
  image=ROOT/'tests/fixtures/stream_guest/normal-44100.ldr'
  args=list(map(str,[terminal,'--ldr',image,'--commands',cmd,'--log',log,'--host-timeout-ms',1000]))
  p=subprocess.Popen(args,cwd=d,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  ctl=Control(cmd,log,p);assert ctl.command('status')['status']['stopped'];ctl.stable();assert ctl.command('quit')['status']['stopped'];assert p.wait(timeout=5)==0
  cmd.write_text('');r=subprocess.run(args[:-1]+['50'],cwd=d,capture_output=True,timeout=5);assert r.returncode==124
  assert json.loads(log.read_text().splitlines()[-1])['reason']=='host-timeout'
  r=subprocess.run(args+['--realtime','true'],cwd=d,capture_output=True,timeout=5);assert r.returncode==2
  # The terminal wrapper itself preserves native capture, and does not call
  # a valid but unreached RAM address a successful breakpoint.
  cmd.write_text('');dump(cmd,[{'cmd':'break','pc':'0x07000000'},{'cmd':'continue'}])
  cap=d/'terminal';r=subprocess.run(args+['--codec-mclk','11289600','--capture',str(cap)],cwd=d,capture_output=True,timeout=5);assert r.returncode==0
  end=json.loads(log.read_text().splitlines()[-1]);assert end['reason']=='ldr-return' and not end['status']['targets'][0]['verified'] and end['status']['targets'][0]['hits']==0
  # normal and work use the same value/cadence oracle, but have different
  # guest startup times; compare this wrapper against the normal image.
  normal=out/'terminal-reference';run(executable,normal,'normal')
  for ext in ['.pcm','.csv']:assert cap.with_suffix(ext).read_bytes()==normal.with_suffix(ext).read_bytes()
  cmd.write_text('{bad json}\n'+json.dumps({'cmd':'ram','address':0xffc00810,'bytes':4})+'\n'+json.dumps({'cmd':'quit'})+'\n')
  r=subprocess.run(args,cwd=d,capture_output=True,timeout=5);assert r.returncode==0
  bad=[json.loads(x) for x in log.read_text().splitlines() if json.loads(x).get('ok') is False];assert len(bad)==2
  cmd.write_text('x'*4097+'\n');r=subprocess.run(args,cwd=d,capture_output=True,timeout=5);assert r.returncode==2 and b'4096' in r.stderr

 # Pause and quit must remain usable while waiting for an unreached target.
 with tempfile.TemporaryDirectory(prefix='debug-cancel-') as d:
  d=Path(d);cmd=d/'commands';log=d/'debug.jsonl';loop=d/'loop.ldr';cmd.write_text('')
  header=bytearray(struct.pack('<IIII',0xad00c000,0xffa00100,2,0));check=0
  for b in header:check^=b
  header[2]=check;loop.write_bytes(header+struct.pack('<H',0x2000)) # JUMP.S to self
  args=list(map(str,[terminal,'--ldr',loop,'--commands',cmd,'--log',log,'--host-timeout-ms',5000]))
  p=subprocess.Popen(args,cwd=d,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  ctl=Control(cmd,log,p);ctl.command('break',pc='0xffa00102');ctl.resume();ctl.command('wait-stop')
  time.sleep(.025);assert ctl.command('pause')['ok'];ctl.receive(lambda r:r.get('kind')=='stop')
  paused=ctl.stable();assert paused['reason']=='pause' and paused['packets']>0 and not paused['targets'][0]['verified']
  ctl.resume();ctl.command('wait-stop');assert ctl.command('quit')['ok'];assert p.wait(timeout=5)==0
 print('terminal pause/cancel behind an unreached target, wall timeout and unsupported-mode checks passed')
if __name__=='__main__':main()
