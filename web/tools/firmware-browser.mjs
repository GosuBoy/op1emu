// Source-only browser preparation test. Deliberately stubs the emulator build
// manifest. Exercises the real UI and OPFS, with optional local stock-package
// preparation. Native boot is a separate test.
import path from 'node:path';
import fs from 'node:fs';
import os from 'node:os';
import {fileURLToPath} from 'node:url';
import {setTimeout as delay} from 'node:timers/promises';
import {build} from 'vite';
import {chromium} from 'playwright-core';
import {serveDist} from './serve-dist.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=fs.mkdtempSync(path.join(os.tmpdir(),'op1-web-ui-'));
await build({configFile:false,root,base:'./',logLevel:'warn',worker:{format:'es'},build:{outDir:out,emptyOutDir:true},plugins:[{
  name:'test-only-emulator-manifest',resolveId:id=>id==='virtual:op1emu'?'\0test-manifest':null,
  load:id=>id==='\0test-manifest'?`export default ${JSON.stringify({js:'not-used.mjs',wasm:{file:'not-used.wasm',size:0},bootRom:{url:'https://invalid.test/not-used',sha256:''}})}`:null,
}]});
const server=await serveDist(out);
let browser, page;
try {
browser=await chromium.launch({headless:true,...(process.env.CHROME?{executablePath:process.env.CHROME}:{channel:'chrome'})});
const context=await browser.newContext();page=await context.newPage();
const errors=[];page.on('pageerror',e=>errors.push(e.message));
let externalRequests=0;
await context.route('**/*',async route=>{
  const url=new URL(route.request().url());
  if(/^https?:$/.test(url.protocol)&&url.origin!==new URL(server.url).origin){
    externalRequests++;await route.abort();
  }else await route.continue();
});
const waitStatus = text => page.waitForFunction(value => document.getElementById('status').textContent.includes(value), text);
const waitReady = ready => page.waitForFunction(value => document.getElementById('start').disabled !== value, ready);
const setup = page.locator('#firmware-setup');
const otp = {name:'otp.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(8192,0xa5)};
const rejected = {name:'bad.op1',mimeType:'application/octet-stream',buffer:Buffer.from([1,2,3])};
const assertOpen = async expected => {
  if (await setup.evaluate(node=>node.open) !== expected) throw Error('Unexpected firmware disclosure state');
};
  await page.goto(server.url);
  await page.waitForFunction(()=>document.getElementById('nand-state').textContent!=='…');
  await waitReady(false);
  await assertOpen(true);
  await page.locator('#firmware-file').setInputFiles(rejected);
  await page.locator('#prepare-firmware').click();
  await waitStatus('8 KiB otp.bin');
  await page.locator('#firmware-otp').setInputFiles(otp);
  await page.locator('#prepare-firmware').click();
  await waitStatus('Unsupported firmware');
  if(externalRequests)throw Error('Invalid input triggered an external network request');

  const nand = path.join(out,'synthetic-nand.bin');
  fs.closeSync(fs.openSync(nand,'w'));
  fs.truncateSync(nand,528*1024*1024);
  await page.locator('#pick').setInputFiles(otp);
  await page.waitForFunction(()=>!document.getElementById('export-otp').disabled);
  await assertOpen(true);
  const nandFiles = () => page.evaluate(async () => {
    const root = await navigator.storage.getDirectory();
    const dir = await root.getDirectoryHandle('manual-inputs');
    const names = [];
    for await (const name of dir.keys()) if (name.startsWith('nand-')) names.push(name);
    return names;
  });
  let kept;
  for (let i=0;i<3;i++) {
    await page.locator('#pick').setInputFiles(nand);
    const deadline = Date.now() + 30000;
    for (;;) {
      const names = await nandFiles();
      if (names.length === 1 && names[0] !== kept &&
          !await page.locator('#start').isDisabled() && !await page.locator('#status').textContent()) {
        [kept] = names;
        break;
      }
      if (Date.now() >= deadline) throw Error('Expected exactly one committed NAND after import');
      await delay(50);
    }
  }
  await assertOpen(false);
  await page.reload();
  await waitReady(true);
  await assertOpen(false);

  if(process.env.OP1_FIRMWARE){
    await setup.locator('summary').click();
    await page.locator('#firmware-file').setInputFiles(process.env.OP1_FIRMWARE);
    await page.locator('#firmware-otp').setInputFiles(otp);
    await page.locator('#prepare-firmware').click();
    await page.waitForFunction(()=>document.getElementById('firmware-state').textContent.includes('11810'),null,{timeout:180000});
    if(externalRequests)throw Error('Preparation unexpectedly made an external request');
    await waitReady(true);
    if((await nandFiles()).length)throw Error('Old manual NAND was not reclaimed');
    for (const name of ['nand','otp']) {
      if(await page.locator(`#export-${name}`).isDisabled())throw Error(`Prepared ${name} not exportable`);
    }
    const downloadEvent = page.waitForEvent('download');
    await page.locator('#export-otp').click();
    const download = await downloadEvent;
    if (!fs.readFileSync(await download.path()).equals(otp.buffer)) throw Error('Prepared OTP export changed the input bytes');
    await page.reload();
    await waitReady(true);
    await assertOpen(false);
    await setup.locator('summary').click();
    await page.locator('#firmware-file').setInputFiles(rejected);
    await page.locator('#firmware-otp').setInputFiles(otp);
    await page.locator('#prepare-firmware').click();
    await waitStatus('Unsupported firmware');
    await waitReady(true);
    await page.locator('#firmware-file').setInputFiles(process.env.OP1_FIRMWARE);
    await page.locator('#prepare-firmware').click();
    await page.locator('#cancel-firmware').click();
    await waitReady(true);
    if (!await page.locator('#firmware-state').textContent().then(t=>t.includes('11810')))
      throw Error('Failed or cancelled preparation lost the previous generation');
    console.log('Stock package preparation, persistence, unchanged OTP export, and failed/cancelled replacement passed with synthetic OTP.');
  }
  if(errors.length)throw Error(errors.join('\n'));
  console.log('Setup disclosure, invalid-input/no-fetch checks and three-import OPFS cleanup passed.');
}catch(e){if(page)console.error('UI status:',await page.locator('#status').textContent().catch(()=>''));throw e;}
finally{await browser?.close();server.close();fs.rmSync(out,{recursive:true,force:true});}
