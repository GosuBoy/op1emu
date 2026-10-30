#!/usr/bin/env python3
"""Reproducible owned INIT stream; GNU Binutils 2.44 bfin-elf. No vendor inputs."""
import argparse, hashlib, json, struct, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent

def sha(data): return hashlib.sha256(data).hexdigest()

def elf(data):
    assert data[:7] == b'\x7fELF\x01\x01\x01'
    assert struct.unpack_from('<H', data, 18)[0] == 106
    shoff = struct.unpack_from('<I', data, 32)[0]
    size, count, names = struct.unpack_from('<HHH', data, 46)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff+i*size) for i in range(count)]
    contents = lambda s: data[s[4]:s[4]+s[5]]
    string = lambda t, n: t[n:t.index(b'\0', n)].decode()
    strings = contents(sections[names]); result = {}; symbols = {}
    for s in sections:
        name = string(strings, s[0])
        if s[2] & 2 and s[5]:
            assert name in ('.bootstrap', '.init', '.main'), name
            result[name] = (s[3], contents(s))
        if s[1] == 2:
            strings2 = contents(sections[s[6]])
            for offset in range(s[4], s[4]+s[5], s[9]):
                n, value, _, info, _, index = struct.unpack_from('<IIIBBH', data, offset)
                if info >> 4 == 1 and index: symbols[string(strings2,n)] = value
    return result, symbols

def block(target, payload=b'', flags=6, count=None, argument=0):
    h = bytearray(struct.pack('<IIII', 0xad000000|flags, target, len(payload) if count is None else count, argument))
    checksum = 0
    for b in h: checksum ^= b
    h[2] = checksum
    return bytes(h)+payload

def build(output, assembler, linker):
    output.mkdir(parents=True,exist_ok=True); identities = {}
    for name, tool in [('assembler',assembler),('linker',linker)]:
        version = subprocess.check_output([str(tool),'--version'],text=True).splitlines()[0]
        assert '2.44' in version, version
        identities[name] = {'path':str(tool.resolve()),'version':version,'sha256':sha(tool.read_bytes())}
    manifest = {'schema':1,'toolchain':'GNU Binutils 2.44 bfin-elf; -mcpu=bf524; no C compiler/runtime',
                'sources':{n:sha((ROOT/n).read_bytes()) for n in ['guest.s','link.ld','build.py']},'images':{}}
    commands=[]
    for name, case, bootdata in [('normal',0,0),('bad-context',1,0),('bad-init',2,0),('no-redirect',3,0),('null-data',0,1),('misaligned-data',0,2),('malformed-data',0,3),('missing-init',0,0),('missing-fill',0,0),('direct',0,0)]:
        obj=output/(name+'.o'); image=output/(name+'.elf')
        cmd=[str(assembler),'-mcpu=bf524','--fatal-warnings','--defsym',f'CASE={case}','--defsym',f'BOOT_DATA={bootdata}','-o',str(obj),str(ROOT/'guest.s')]
        subprocess.run(cmd,check=True);commands.append(cmd)
        cmd=[str(linker),'-T',str(ROOT/'link.ld'),'-o',str(image),str(obj)]
        subprocess.run(cmd,check=True);commands.append(cmd)
        sections,symbols=elf(image.read_bytes());init_addr,init=sections['.init'];main_addr,main=sections['.main'];entry,bootstrap=sections['.bootstrap']
        # ROM stream starts with a FIRST/IGNORE marker. INIT code is loaded by
        # ROM, not preinstalled by the host in the positive case.
        stream=block(init_addr,flags=0x5006)+block(init_addr,init)
        if name != 'missing-fill':stream+=block(0xff800000,flags=0x106,count=0x100)
        if name != 'missing-init':stream+=block(init_addr,flags=0x806)
        poison_offset=len(stream)
        stream+=block(symbols['poison_entry'],flags=0xd006)
        stream+=block(main_addr,flags=0x5006)+block(main_addr,main,flags=0x8406)
        if name == 'direct':
            packaged=block(main_addr,main,flags=0xc006)
        else:
            packaged=block(entry,bootstrap,flags=0x4006)
            if bootdata:packaged+=block(init_addr,init)
            packaged+=block(0xff900000,stream,flags=0x8006)
        assert len(stream)<0x4000
        (output/(name+'.ldr')).write_bytes(packaged)
        manifest['images'][name]={'image_sha256':sha(packaged),'image_bytes':len(packaged),'main_sha256':sha(main),'stream_sha256':sha(stream),'stream_bytes':len(stream),'poison_offset':poison_offset,'symbols':symbols}
        if name=='normal':(output/'stream.ldr').write_bytes(stream)
    assert len({v['main_sha256'] for v in manifest['images'].values()})==1
    # A deliberately synthetic attached backing store, not NoFlash and not a
    # physical boot-area reconstruction. MemBoot never consumes its contents.
    nand=bytes((i*13+7)&255 for i in range(4096));(output/'synthetic.nand').write_bytes(nand)
    manifest['nand']={'bytes':len(nand),'sha256':sha(nand),'role':'same attached synthetic snapshot backing; not a NAND boot stream'}
    manifest['stream_sha256']=sha((output/'stream.ldr').read_bytes())
    (output/'manifest.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
    (output/'build-receipt.json').write_text(json.dumps({'tools':identities,'commands':commands},sort_keys=True,indent=2)+'\n')
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--as',dest='assembler',required=True,type=Path);p.add_argument('--ld',dest='linker',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--check',action='store_true');p.add_argument('--update',action='store_true');a=p.parse_args()
    m=build(a.output,a.assembler,a.linker)
    if a.check:
        assert m==json.loads((ROOT/'manifest.json').read_text()),'manifest differs'
        for n in list(m['images'])+['stream']:
            assert (a.output/(n+'.ldr')).read_bytes()==(ROOT/(n+'.ldr')).read_bytes(),n
        assert sha((a.output/'synthetic.nand').read_bytes())==m['nand']['sha256']
    if a.update:
        for n in list(m['images'])+['stream']:(ROOT/(n+'.ldr')).write_bytes((a.output/(n+'.ldr')).read_bytes())
        for n in ['manifest.json']:(ROOT/n).write_bytes((a.output/n).read_bytes())
    print(json.dumps({n:v['image_sha256'] for n,v in m['images'].items()},indent=2))
if __name__=='__main__':main()
