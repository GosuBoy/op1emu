#!/usr/bin/env python3
"""Supervise the public synthetic INIT diagnostic, with an independent wall deadline.

A killed worker cannot provide a final machine snapshot. Report that explicitly;
never relabel partial logs as a completed observation. No device/network access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def identity(path):
    path = Path(path).resolve(); digest=hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda:stream.read(1024*1024),b''):digest.update(data)
    return {'path':str(path),'bytes':path.stat().st_size,'sha256':digest.hexdigest()}


def run(binary, image, nand, output, path='rom', cycles=1_000_000_000,
        runs=5_000_000, wall_ms=120_000, trace_cap=4096):
    for value, cap in [(cycles,1_000_000_000),(runs,5_000_000),(wall_ms,120_000),(trace_cap,4096)]:
        if not 0 < value <= cap: raise ValueError('limit outside approved ceiling')
    if path not in ('rom','direct','direct-stream'): raise ValueError('unsupported_path: NAND/reset boot is unqualified')
    binary,image,nand,output=map(lambda p:Path(p).resolve(),(binary,image,nand,output))
    ui=Path(__file__).resolve().parent.parent/'gui/ui.json'
    inputs=[identity(p) for p in (binary,image,nand,image.parent/'manifest.json',ui)]
    manifest=json.loads((image.parent/'manifest.json').read_text())
    expected=(manifest['stream_sha256'] if image.stem=='stream'
              else manifest['images'].get(image.stem,{}).get('image_sha256'))
    if inputs[1]['sha256']!=expected:
        raise ValueError('image does not match the synthetic fixture manifest')
    if inputs[2]['sha256']!=manifest['nand']['sha256'] or inputs[2]['bytes']!=manifest['nand']['bytes']:
        raise ValueError('backing does not match the intended synthetic NAND context')
    suffixes=['.result.json','.events.jsonl','.state.json','.portg-hysteresis.jsonl',
              '.stdout.log','.stderr.log','.supervision.json']
    for suffix in suffixes:
        target=Path(str(output)+suffix)
        if target in (binary,image,nand,image.parent/'manifest.json') or target.exists():
            raise ValueError('output exists or overlaps an input: '+str(target))
    command=[str(binary),'--image',str(image),'--nand',str(nand),'--output',str(output),
             '--path',path,'--max-cycles',str(cycles),'--max-runs',str(runs),
             '--wall-ms',str(wall_ms),'--trace-cap',str(trace_cap)]
    # An isolated caller directory prevents accidental use of a private otp.bin.
    work=Path(str(output)+'.work');work.mkdir()
    (work/'gui').mkdir();(work/'gui/ui.json').symlink_to(ui)
    # Budget includes worker startup/JIT, not only time between CPU Run calls.
    deadline=time.monotonic()+wall_ms/1000
    timed_out=False; killed=False
    with open(str(output)+'.stdout.log','xb') as stdout, open(str(output)+'.stderr.log','xb') as stderr:
        worker=subprocess.Popen(command,stdout=stdout,stderr=stderr,cwd=work)
        try:
            returncode=worker.wait(timeout=max(0,deadline-time.monotonic()))
        except subprocess.TimeoutExpired:
            timed_out=True; killed=True
            worker.kill()  # entire native worker, including compiler threads
            worker.wait()
            returncode=124
    if time.monotonic()>deadline:
        timed_out=True;returncode=124
    unchanged=all(identity(p['path'])==p for p in inputs)
    result_path=Path(str(output)+'.result.json')
    if timed_out:
        result={'schema':1,'status':'host_timeout','observation_complete':False,
                'reason':'independent worker deadline expired; final CPU observation not accepted',
                'dropped_counts_available':False,'final_state_available':False,
                'limits':{'cycles':cycles,'run_calls':runs,'wall_ms':wall_ms,'events_per_source':trace_cap}}
        result_path.write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')
    elif result_path.exists():
        result=json.loads(result_path.read_text())
    else:
        result={'schema':1,'status':'worker_failure','observation_complete':False,'returncode':returncode}
        result_path.write_text(json.dumps(result,sort_keys=True,indent=2)+'\n')
    receipt={'schema':1,'inputs':inputs,'inputs_unchanged':unchanged,'command':command,
             'returncode':returncode,'worker_killed_at_deadline':killed,'deadline_expired':timed_out,
             'implicit_boot_inputs':'isolated worker directory; no OTP supplied; ROM memory-boot skips preboot',
             'wall_enforcement':'independent process deadline; cleanup/reporting can follow termination',
             'result_status':result['status'],'supervisor_source':identity(Path(__file__))}
    Path(str(output)+'.supervision.json').write_text(json.dumps(receipt,sort_keys=True,indent=2)+'\n')
    if not unchanged: raise RuntimeError('an input changed during execution')
    return returncode,result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('binary','image','nand','output'):p.add_argument('--'+n,required=True,type=Path)
    p.add_argument('--path',default='rom');p.add_argument('--max-cycles',type=int,default=1_000_000_000)
    p.add_argument('--max-runs',type=int,default=5_000_000);p.add_argument('--wall-ms',type=int,default=120_000)
    p.add_argument('--trace-cap',type=int,default=4096);a=p.parse_args()
    code,result=run(a.binary,a.image,a.nand,a.output,a.path,a.max_cycles,a.max_runs,a.wall_ms,a.trace_cap)
    print(json.dumps({'status':result['status'],'observation_complete':result['observation_complete']}))
    raise SystemExit(code)
if __name__=='__main__':main()
