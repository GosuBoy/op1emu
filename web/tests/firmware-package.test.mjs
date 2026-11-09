import test from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {crc32,parseTar,extractFirmware,extractFirmware246} from '../firmware-package.js';
if (!globalThis.crypto) globalThis.crypto = webcrypto;
const cat=(...chunks)=>Uint8Array.from(chunks.flatMap(a=>Array.from(a)));
function hdr(flags,count=0) {
 const b=new Uint8Array(16), d=new DataView(b.buffer);
 d.setUint16(0,flags,true);b[3]=0xad;d.setUint32(4,0xff90021c,true);d.setUint32(8,count,true);
 b[2]=b.reduce((a,b)=>a^b,0);return b;
}
function tar(members) {
 const out=[];
 for(const [name,data,type=48] of members) {
  const h=new Uint8Array(512),text=(offset,s)=>h.set(new TextEncoder().encode(s),offset);
  text(0,name);text(100,'0000644\0');text(124,data.length.toString(8).padStart(11,'0')+'\0');text(136,'00000000001\0');
  h[156]=type;h.fill(32,148,156);text(148,h.reduce((a,b)=>a+b,0).toString(8).padStart(6,'0')+'\0 ');
  out.push(h,data,new Uint8Array((512-data.length%512)%512));
 }
 return cat(...out,new Uint8Array(1024));
}
function pkg(length) {
 const c=new Uint8Array(14),d=new DataView(c.buffer);c[0]=93;d.setUint32(1,4096,true);d.setBigUint64(5,BigInt(length),true);
 const prefix=new Uint8Array(4);new DataView(prefix.buffer).setUint32(0,crc32(c),true);return cat(prefix,c);
}
test('CRC32 standard vector',()=>assert.equal(crc32(new TextEncoder().encode('123456789')),0xcbf43926));
test('TAR names, metadata and bounds',()=>{
 const archive=tar([['./OP1_vdk.ldr',hdr(0x8006)],['content/',new Uint8Array(),53],['content/a',[1,2]]]);
 const entries=parseTar(archive);assert.equal(entries[0].name,'OP1_vdk.ldr');assert.equal(entries[0].mode,0o644);assert.equal(entries[0].mtime,1);assert.equal(entries[1].type,'directory');
 for(const bad of [tar([['../x',[]]]),tar([['/x',[]]]),tar([['a',[]],['./a',[]]]),tar([['a',[],50]]),archive.subarray(0,archive.length-1)])assert.throws(()=>parseTar(bad));
 const corrupt=archive.slice();corrupt[0]^=1;assert.throws(()=>parseTar(corrupt),/checksum/);
 const trailing=archive.slice();trailing[trailing.length-1]=1;assert.throws(()=>parseTar(trailing),/end marker/);
});
test('package injection contract, CRC first, lengths and decoder limits',async()=>{
 const archive=tar([['OP1_vdk.ldr',hdr(0x8006)]]),input=pkg(archive.length);
 const decode=async(compressed,limits)=>{assert.equal(limits.maxOutputBytes,256*1024*1024);return {bytes:archive,bytesRead:compressed.length};};
 const result=await extractFirmware(input,{decompressLzma:decode});assert.equal(result.packageCrc32,crc32(input.subarray(4)));assert.deepEqual(result.loader,hdr(0x8006));
 const corrupt=input.slice();corrupt[0]^=1;await assert.rejects(extractFirmware(corrupt,{decompressLzma:()=>{throw new Error('must not run');}}),/CRC32/);
 await assert.rejects(extractFirmware(input,{decompressLzma:async()=>({bytes:archive,bytesRead:0})}),/after LZMA/);
 await assert.rejects(extractFirmware(pkg(1),{decompressLzma:decode}),/length mismatch/);
 await assert.rejects(extractFirmware(hdr(0x8006)),/package header/);
});
test('production wrapper rejects unknown package before decoder invocation',async()=>{
 await assert.rejects(extractFirmware246(pkg(0),{decompressLzma:()=>{throw new Error('decoder must not run');}}),/Unsupported firmware/);
});
