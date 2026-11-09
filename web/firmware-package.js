/** Parse original OP-1 update packages without modifying their loaders. */
export const MAX_INPUT_BYTES = 64 * 1024 * 1024;
export const MAX_ARCHIVE_BYTES = 256 * 1024 * 1024;
export const MAX_DICTIONARY_BYTES = 64 * 1024 * 1024;
export const STOCK_246_SHA256 = 'c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9';
const ENCRYPTED_246_SHA256 = 'f79d299f6f9019c998013a4176223a6a1bc999a2d8c54451346861f6ea455c74';
const check = (ok, message) => { if (!ok) throw new Error(message); };
const asBytes = value => value instanceof Uint8Array ? value : new Uint8Array(value);
const view = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const trustedPackage = Symbol('canonical-package');
export function crc32(input) {
  let crc = 0xffffffff;
  for (const byte of asBytes(input)) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function tarText(bytes) {
  const end = bytes.indexOf(0);
  return new TextDecoder('utf-8', {fatal: true}).decode(end < 0 ? bytes : bytes.subarray(0, end));
}
function octal(bytes) {
  const text = tarText(bytes).trim();
  check(/^[0-7]*$/.test(text), 'Unsupported or invalid TAR numeric field');
  const n = text ? parseInt(text, 8) : 0;
  check(Number.isSafeInteger(n), 'TAR numeric field exceeds safe range');
  return n;
}
function safeName(name) {
  while (name.startsWith('./')) name = name.slice(2);
  check(name.length > 0 && !name.startsWith('/') && !name.includes('\\') && !/[\x00-\x1f\x7f]/.test(name), 'Unsafe TAR member path');
  check(!name.split('/').some(part => part === '..' || part === '.'), 'Unsafe TAR member path');
  return name;
}
/** Links and extended headers fail closed. Bytes are archive views, not disk writes. */
export function parseTar(input) {
  const bytes = asBytes(input);
  check(bytes.length <= MAX_ARCHIVE_BYTES, 'TAR exceeds archive limit');
  check(bytes.length % 512 === 0, 'TAR is not a multiple of 512 bytes');
  const entries = [], names = new Set();
  let offset = 0, ended = false;
  while (offset < bytes.length) {
    const header = bytes.subarray(offset, offset + 512);
    if (header.every(b => b === 0)) {
      check(bytes.length - offset >= 1024, 'TAR is missing its end markers');
      check(bytes.subarray(offset).every(b => b === 0), 'Nonzero data after TAR end marker');
      ended = true; break;
    }
    const stored = octal(header.subarray(148, 156));
    let sum = 0;
    for (let i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? 32 : header[i];
    check(sum === stored, 'Invalid TAR header checksum');
    const magic = tarText(header.subarray(257, 263));
    // Old GNU stores metadata, not POSIX prefixes, in this region.
    const prefix = magic === 'ustar' && header[262] === 0 ? tarText(header.subarray(345, 500)) : '';
    const name = safeName((prefix ? prefix + '/' : '') + tarText(header.subarray(0, 100)));
    check(!names.has(name), 'Duplicate TAR member'); names.add(name);
    check(names.size <= 65536, 'Too many TAR members');
    const size = octal(header.subarray(124, 136)), type = header[156];
    check(type === 0 || type === 48 || type === 53, 'Unsupported TAR member type (only files and directories allowed)');
    check(type !== 53 || size === 0, 'TAR directory contains a payload');
    const start = offset + 512, end = start + size;
    check(end <= bytes.length, 'Truncated TAR member');
    check(size <= MAX_INPUT_BYTES, 'TAR member exceeds 64 MiB limit');
    entries.push({name, bytes: bytes.subarray(start, end), type: type === 53 ? 'directory' : 'file', mode: octal(header.subarray(100, 108)), mtime: octal(header.subarray(136, 148))});
    offset = start + Math.ceil(size / 512) * 512;
  }
  check(ended, 'TAR is missing its end markers');
  return entries;
}
/** Low-level decoder injection MUST enforce limits during decoding and return
 * {bytes: Uint8Array, bytesRead: number}. Production uses extractFirmware246. */
export async function extractFirmware(input, {decompressLzma, [trustedPackage]: trusted = false} = {}) {
  const bytes = asBytes(input);
  check(bytes.length <= MAX_INPUT_BYTES, 'Firmware input exceeds 64 MiB limit');
  check(bytes.length >= 17, 'Truncated OP-1 package header');
  const compressed = bytes.subarray(4), header = view(compressed), packageCrc32 = crc32(compressed);
  check(view(bytes).getUint32(0, true) === packageCrc32, 'Package CRC32 mismatch');
  check(compressed[0] < 225, 'Invalid LZMA properties byte');
  check(header.getUint32(1, true) <= MAX_DICTIONARY_BYTES, 'LZMA dictionary exceeds 64 MiB limit');
  const declared = header.getBigUint64(5, true);
  check(declared === 0xffffffffffffffffn || declared <= BigInt(MAX_ARCHIVE_BYTES), 'Declared LZMA output exceeds archive limit');
  check(typeof decompressLzma === 'function', 'A bounded LZMA-alone decoder is required');
  const result = await decompressLzma(compressed, {maxOutputBytes: MAX_ARCHIVE_BYTES, maxDictionaryBytes: MAX_DICTIONARY_BYTES});
  check(result && result.bytes instanceof Uint8Array, 'LZMA decoder returned invalid output');
  check(result.bytes.length <= MAX_ARCHIVE_BYTES, 'LZMA output exceeds archive limit');
  // Only byte-for-byte canonical packages permit decoders without consumed count.
  check((trusted && result.bytesRead === undefined) || result.bytesRead === compressed.length, 'Unexpected data after LZMA stream');
  check(declared === 0xffffffffffffffffn || declared === BigInt(result.bytes.length), 'LZMA output length mismatch');
  const entries = parseTar(result.bytes);
  const loader = entries.find(entry => entry.name === 'OP1_vdk.ldr' && entry.type === 'file')?.bytes;
  check(loader, 'TAR does not contain a root OP1_vdk.ldr regular file');
  return {loader, entries, packageCrc32};
}
export async function sha256(input) {
  check(globalThis.crypto?.subtle, 'SHA-256 requires a secure browser context');
  const digest = new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', asBytes(input)));
  return Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join('');
}
/** Only the canonical package reaches the LZMA decoder. Both boot streams
 * remain exactly as shipped in the archive. */
export async function extractFirmware246(input, options = {}) {
  const bytes = asBytes(input);
  check(bytes.length <= MAX_INPUT_BYTES, 'Firmware input exceeds 64 MiB limit');
  check(await sha256(bytes) === STOCK_246_SHA256, 'Unsupported firmware: select the original OP-1 firmware 246 .op1 package');
  const result = await extractFirmware(bytes, {...options, [trustedPackage]: true});
  const bootloader = result.entries.find(entry => entry.name === 'te-boot.ldr' && entry.type === 'file')?.bytes;
  check(bootloader, 'TAR does not contain a root te-boot.ldr regular file');
  check(await sha256(result.loader) === ENCRYPTED_246_SHA256, 'Canonical package loader SHA-256 mismatch');
  return {...result, bootloader};
}
