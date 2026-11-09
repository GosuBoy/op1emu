// New OPFS generations commit last; never overwrite the user's legacy inputs.
import {NAND_SIZE} from './firmware-nand.js';
export const PROFILES = 'firmware-profiles';
const LEGACY_LOADER_HASH = '1e806672dc440d495987c6b1f33b300876e6a3bb1d58254aac5fe60c960e906a';
export async function writeBytes(dir, name, bytes, {signal} = {}) {
  const handle = await (await dir.getFileHandle(name, {create:true})).createSyncAccessHandle();
  try {
    signal?.throwIfAborted();
    handle.truncate(0);
    for (let at=0; at<bytes.length;) {
      const count=handle.write(bytes.subarray(at),{at});
      if (!Number.isInteger(count) || count<=0 || count>bytes.length-at) throw new Error('Storage write made no progress');
      at+=count;
    }
    handle.flush();
  } finally {handle.close();}
}
export async function findProfile(root) {
  let profiles;
  try {profiles=await root.getDirectoryHandle(PROFILES);} catch{return null;}
  const valid=[];
  for await(const [name,dir] of profiles.entries()) {
    if(dir.kind!=='directory'||!/^profile-[0-9]+-[a-f0-9-]+$/.test(name))continue;
    try {
      const commit=await(await dir.getFileHandle('complete.json')).getFile();
      if(commit.size>4096)continue;
      const info=JSON.parse(await commit.text());
      if(info.schema!==1||info.loaderHash!==LEGACY_LOADER_HASH||!Number.isSafeInteger(info.created))continue;
      const nand=await(await dir.getFileHandle('nand.bin')).getFile();
      const ldr=await(await dir.getFileHandle('plaintext.ldr')).getFile();
      if(nand.size!==NAND_SIZE||ldr.size!==2171624)continue;
      valid.push({name,info,legacy:true});
    }catch{/* incomplete, corrupt, or locked generation */}
  }
  return valid.sort((a,b)=>b.info.created-a.info.created||b.name.localeCompare(a.name))[0]??null;
}
export async function stageProfile(root, {loader, bootloader, entries}, otp, provisionNand, {signal, onProgress} = {}) {
  const profiles = await root.getDirectoryHandle(PROFILES, {create: true});
  const name = `profile-${Date.now()}-${crypto.randomUUID()}`;
  const dir = await profiles.getDirectoryHandle(name, {create: true});
  let staged = false;
  try {
    const nand = await (await dir.getFileHandle('nand.bin', {create: true})).createSyncAccessHandle();
    let result;
    try {
      result = await provisionNand(entries, nand, {loader, bootloader, signal, onProgress});
    } finally {
      nand.close();
    }
    await writeBytes(dir, 'otp.bin', otp, {signal});
    staged = true;
    return {name, info: {pages: result.pages}};
  } finally {
    if (!staged) await profiles.removeEntry(name, {recursive: true}).catch(() => {});
  }
}
