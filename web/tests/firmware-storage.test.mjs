import test from 'node:test';import assert from 'node:assert/strict';import {findProfile,stageProfile,writeBytes} from '../firmware-storage.js';
class Dir{kind='directory';children=new Map();async getDirectoryHandle(name,{create=false}={}){if(!this.children.has(name)){if(!create)throw Error('missing');this.children.set(name,new Dir())}return this.children.get(name)}async getFileHandle(name,{create=false}={}){if(!this.children.has(name)){if(!create)throw Error('missing');this.children.set(name,{data:new Uint8Array()})}const item=this.children.get(name);return {getFile:async()=>new Blob([item.data]),createSyncAccessHandle:async()=>({truncate:n=>{item.data=new Uint8Array(n)},write:(b,{at})=>{const next=new Uint8Array(Math.max(item.data.length,at+b.length));next.set(item.data);next.set(b,at);item.data=next;return b.length},flush(){},close(){}})}}async removeEntry(name){if(!this.children.delete(name))throw Error('missing')}async *entries(){yield*this.children.entries()}}
test('discovery ignores incomplete generations, preserves legacy',async()=>{const root=new Dir();await writeBytes(root,'nand.bin',new Uint8Array([42]));assert.equal(await findProfile(root),null);const p=await root.getDirectoryHandle('firmware-profiles',{create:true});await p.getDirectoryHandle('profile-1-aaaa',{create:true});assert.equal(await findProfile(root),null);assert.equal(root.children.get('nand.bin').data[0],42)});
test('failure removes only its staged generation',async()=>{const root=new Dir();await writeBytes(root,'nand.bin',new Uint8Array([42]));const p=await root.getDirectoryHandle('firmware-profiles',{create:true});await p.getDirectoryHandle('profile-1-aaaa',{create:true});await assert.rejects(stageProfile(root,{},new Uint8Array(),async()=>{throw Error('disk full')}),/disk full/);assert.deepEqual([...p.children.keys()],['profile-1-aaaa']);assert.equal(root.children.get('nand.bin').data[0],42)});
test('stages the two boot files unchanged and relies on selection to publish them',async()=>{
  const root=new Dir();
  const otp=Uint8Array.from({length:8192},(_,i)=>i%256);
  const loader=new Uint8Array([1,2]), bootloader=new Uint8Array([3,4]), entries=[];
  const profile=await stageProfile(root,{loader,bootloader,entries},otp,async(content,handle,options)=>{
    assert.equal(content,entries);assert.equal(options.loader,loader);assert.equal(options.bootloader,bootloader);
    handle.write(new Uint8Array([42]),{at:0});return {pages:1};
  });
  const dir=await(await root.getDirectoryHandle('firmware-profiles')).getDirectoryHandle(profile.name);
  assert.deepEqual([...dir.children.keys()],['nand.bin','otp.bin']);
  assert.deepEqual(dir.children.get('otp.bin').data,otp);
  assert.equal(await findProfile(root),null);
});
