#!/usr/bin/env python3
"""Actual built-in ROM, owned synthetic guests, exact bounds and direct control."""
import hashlib,importlib.util,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
SPEC=importlib.util.spec_from_file_location('supervise',ROOT/'tools/run_init_handoff.py')
supervise=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(supervise)
FIX=ROOT/'tests/fixtures/init_handoff'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    binary=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
    if out.exists():shutil.rmtree(out)
    out.mkdir(parents=True);(out/'gui').mkdir();(out/'gui/ui.json').symlink_to(ROOT/'gui/ui.json')
    import os
    os.chdir(out)
    manifest=json.loads((FIX/'manifest.json').read_text())
    for n,v in manifest['images'].items():assert sha(FIX/(n+'.ldr'))==v['image_sha256'],n
    nand=out/'synthetic.nand';nand.write_bytes(bytes((i*13+7)&255 for i in range(4096)))
    assert sha(nand)==manifest['nand']['sha256']
    original=sha(nand);results={}
    tampered=out/'tampered';tampered.mkdir()
    shutil.copyfile(FIX/'manifest.json',tampered/'manifest.json')
    payload=bytearray((FIX/'normal.ldr').read_bytes());payload[-1]^=1
    (tampered/'normal.ldr').write_bytes(payload)
    try:
        supervise.run(binary,tampered/'normal.ldr',nand,out/'bad-identity',wall_ms=30000)
    except ValueError as error:
        assert 'fixture manifest' in str(error)
    else:raise AssertionError('unidentified image silently accepted')
    assert not (out/'bad-identity.work').exists(),'identity rejection must precede worker creation'
    def run(name,image='normal',path='rom',cycles=200000,runs=10000,cap=4096,wall=30000):
        code,r=supervise.run(binary,FIX/(image+'.ldr'),nand,out/name,path,cycles,runs,wall,cap)
        assert sha(nand)==original
        if 'cycles' in r:
            assert r['cycles']<=cycles and r['run_calls']<=runs
            assert r['nand_commands']==0,'MEM/direct must retain, not consume/replace NAND context'
            for source,count in json.loads((out/(name+'.state.json')).read_text())['category_events'].items():assert count<=cap,(source,count)
            assert len(r['phases'])<=cap
            history=json.loads((out/(name+'.state.json')).read_text())['portg_hysteresis_history']
            assert history['recorded']<=cap
            events=[json.loads(s) for s in (out/(name+'.events.jsonl')).read_text().splitlines()]
            assert sum(e['kind']=='pc_first' for e in events)<=cap
        results[name]={'status':r['status'],'returncode':code}
        return code,r
    code,positive=run('normal-a')
    assert code==0 and positive['status']=='completed' and positive['observation_complete'],positive
    assert positive['rom_run_calls']>0
    phases={p['name']:p['state'] for p in positive['phases']}
    assert all(k in phases for k in ['init_entry','init_valid','init_return','callback_entry','callback_valid','callback_return','main_entry'])
    entry=phases['init_entry'];done=phases['init_return'];main=phases['main_entry'];final=positive['final']
    boot=entry['dpregs'][0];assert 0xffb00000<=boot<=0xffb00fa0 and boot%4==0
    assert entry['r0_boot_data']['source']==0xff900000+manifest['images']['normal']['poison_offset']
    assert done['mailbox'][8]-done['mailbox'][7]==16
    assert done['context'][1]==boot and done['context'][3]==1
    assert main['context'][2:]==[1,1]
    assert 0xef000000<=entry['rets']<0xef008000 and 0xef000000<=done['rets']<0xef008000
    assert main['dpregs'][14]==0xffb00ffc
    for state in (entry,done,main):
        assert [state['mmr'][k] for k in ['imask','ipend','ilat']]==[0x1f,0x12,0]
    assert phases['callback_entry']['dpregs'][0]==boot and phases['callback_entry']['dpregs'][2]==13
    assert final['mailbox'][0:3]==[0x48414e44,0x55aa11ee,0x32]
    assert final['mmr']['sdbctl']==1 and final['mmr']['sdrrc']==0x300
    assert final['mmr']['sic_iwr0']==1 and final['mmr']['sic_iwr1']==0
    assert final['mmr']['evt1']==manifest['images']['normal']['symbols']['main_entry']
    for key in ['pll_ctl','pll_div','pll_stat']:
        assert phases['constructed']['mmr'][key]==entry['mmr'][key]==done['mmr'][key]==final['mmr'][key]
    code,repeat=run('normal-b');assert code==0
    # Relevant native state and event bytes, independent of timing/PID/tool paths.
    assert positive==repeat
    captures={}
    for suffix in ['.result.json','.events.jsonl','.state.json','.portg-hysteresis.jsonl']:
        assert (out/('normal-a'+suffix)).read_bytes()==(out/('normal-b'+suffix)).read_bytes(),suffix
        captures[suffix]=sha(out/('normal-a'+suffix))
    code,direct=run('direct','direct','direct');assert code==0 and direct['rom_run_calls']==0
    assert direct['final']['mailbox'][0:3]==[0x48414e44,0xa1b2c3d4,0x30]
    assert direct['final']['dpregs'][14]==0x07000000
    assert direct['final']['context']==[0,0,0,0] and direct['final']['mmr']['evt1']==0
    assert direct['final']['mmr']['sdbctl']==0 and direct['final']['mmr']['sdrrc']==0x81a
    assert direct['final']['mmr']['sic_iwr0']==direct['final']['mmr']['sic_iwr1']==0xffffffff
    for name,error in [('null-data',0xe001),('misaligned-data',0xe001),('malformed-data',0xe001),
                       ('bad-context',0xe002),('bad-init',0xe003),('no-redirect',0xe004),('missing-init',0xe004)]:
        code,r=run(name,name);assert code==3 and r['status']=='guest_rejected',(name,r)
        assert r['final']['mailbox'][0]==error,(name,r)
    code,r=run('direct-stream','stream','direct-stream')
    assert code==3 and r['final']['mailbox'][0]==0xe001 and r['rom_run_calls']==0
    code,r=run('wrong-path','direct','rom');assert code==1 and r['status']=='contract_failure'
    code,r=run('cycle-bound',cycles=19);assert code==124 and r['status']=='cycle_limit' and r['cycles']==19
    code,r=run('run-bound',runs=7);assert code==124 and r['status']=='run_limit' and r['run_calls']==7
    code,r=run('trace-bound',cap=1);assert code==1 and r['status']=='incomplete_observation' and not r['observation_complete']
    assert r['handoff_events_dropped']>0 and r['category_omissions']['pc_first']>0
    # Parent deadline is independent of the native between-Run checks.
    code,r=run('wall-bound',wall=1);assert code==124 and r['status']=='host_timeout' and not r['observation_complete']
    for flag,value in [('--max-cycles','1000000001'),('--max-runs','5000001'),('--wall-ms','120001'),('--trace-cap','4097'),('--path','nand')]:
        p=subprocess.run([str(binary),'--image',str(FIX/'normal.ldr'),'--nand',str(nand),'--output',str(out/'invalid'),flag,value],capture_output=True,text=True,timeout=3)
        assert p.returncode==2,(flag,p.stderr)
    summary={'schema':1,'cases':results,'normal_native_identities':captures,'positive_cycles':positive['cycles'],
             'positive_run_calls':positive['run_calls'],'rom_run_calls':positive['rom_run_calls'],
             'image_sha256':manifest['images']['normal']['image_sha256'],'main_sha256':manifest['images']['normal']['main_sha256'],
             'nand_sha256':original,'input_unchanged':sha(nand)==original,
             'qualification':'public synthetic memory-boot ABI; not NAND boot, physical TE-BOOT or board equivalence'}
    (out/'validation.json').write_text(json.dumps(summary,sort_keys=True,indent=2)+'\n')
    print(json.dumps(summary,sort_keys=True,indent=2))
if __name__=='__main__':main()
