// Site parity: the site's wasm build and the desktop CLI must give the same checksum and the same
// output ROM (compared by SHA-256, first differing offsets reported) for the same flags and seed.
// Builds seeds across Gob Standard and each biome with both and compares. Flag byte count, used-bit
// masks, code length rule and biome names are read from common/dwr.h and dwr.c.
//
// Usage, from the repo root, after building both (cmake --build build, and the wasm build):
//   node tests/site_parity.mjs [vanilla rom] [seeds per flag set]
// Defaults: C:/ROMs/Dragon_warrior.nes and 5 seeds; DWR_EXE picks another CLI. Exits 1 on any difference or failed build.
import { createRequire } from 'module';
import { execFileSync } from 'child_process';
import crypto from 'crypto';
import fs from 'fs';
import os from 'os';
import path from 'path';
import vm from 'vm';
import { fileURLToPath } from 'url';

const require = createRequire(import.meta.url);
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SITE = path.join(ROOT, 'site');
const EXE = process.env.DWR_EXE || path.join(ROOT, 'build', process.platform === 'win32' ? 'dwrandomizer.exe' : 'dwrandomizer');
const ROM = process.argv[2] || 'C:/ROMs/Dragon_warrior.nes';
const SEEDS = Number(process.argv[3] || 5);
const GOB_STANDARD = 'IVIAAV2EKADEAMAAAADYKEBWAAAJACAAAAIBQAAM75QA';
const SPRITE = 'Loto';   // the same fixed sprite for both builds, so the whole ROM can be compared
const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';

// Constants that live in common/dwr.h and dwr.c are read from there, so a new flag or biome there is
// followed by this test instead of being copied by hand.
function need(v, what) {
  if (v === null || v === undefined || Number.isNaN(v)) {
    console.error('site_parity: could not find ' + what + ' in common/dwr.h or dwr.c; the parser here needs updating');
    process.exit(1);
  }
  return v;
}
const dwrH = fs.readFileSync(path.join(ROOT, 'common', 'dwr.h'), 'utf8');
const dwrC = fs.readFileSync(path.join(ROOT, 'common', 'dwr.c'), 'utf8');
const defNum = (name) => { const m = dwrH.match(new RegExp('^#define\\s+' + name + '\\s+(0x[0-9a-fA-F]+|\\d+)\\b', 'm')); return m ? Number(m[1]) : null; };
const FLAGS_26_USED = need(defNum('FLAGS_26_USED'), 'FLAGS_26_USED');
const FLAGS_27_USED = need(defNum('FLAGS_27_USED'), 'FLAGS_27_USED');
const BIOME_COUNT = need(defNum('BIOME_COUNT'), 'BIOME_COUNT');
const BIOME_LAKE = need(defNum('BIOME_LAKE'), 'BIOME_LAKE');   // first biome kept in byte 25
const FLAG_BYTES = need(Number((dwrC.match(/sizeof\(\(\(dw_rom \*\)0\)->flags\)\s*==\s*(\d+)/) || [])[1]), 'the flags size assert in dwr.c');
const namesM = dwrC.match(/dwr_biome_names\[BIOME_COUNT\]\s*=\s*\{([^}]*)\}/);
const BIOMES = need(namesM ? namesM[1].match(/"([^"]*)"/g).map((s) => s.slice(1, -1)) : null, 'dwr_biome_names');
if (BIOMES.length !== BIOME_COUNT) { console.error('site_parity: dwr_biome_names has ' + BIOMES.length + ' names, BIOME_COUNT is ' + BIOME_COUNT); process.exit(1); }

// FLAG_CODE_LENGTH(raw) is a plain expression, so it is evaluated as it is written in dwr.h.
const lenM = dwrH.match(/^#define\s+FLAG_CODE_LENGTH\(raw\)\s+(.+)$/m);
const codeLength = need(lenM ? new Function('raw', 'return ' + lenM[1] + ';') : null, 'FLAG_CODE_LENGTH');
// BIOME(x): the low two biomes are two bits of byte 2, the rest a number in byte 25.
const biomeM = dwrH.match(/^#define\s+BIOME\(x\)\s+.*flags\[2\] & (0x[0-9a-fA-F]+)\) >> (\d+)\) \| \(x->flags\[2\] & (0x[0-9a-fA-F]+)\)/m);
need(biomeM, 'the BIOME(x) macro shape');
const BIOME_HI = Number(biomeM[1]), BIOME_SHIFT = Number(biomeM[2]), BIOME_LO = Number(biomeM[3]);

// The site has its own copy of the length list and used masks; it must agree with dwr.h.
const siteM = fs.readFileSync(path.join(SITE, 'index.html'), 'utf8')
  .match(/FLAG_LENGTHS\s*=\s*\[([\d,\s]+)\]\s*,\s*FLAGS_26_USED\s*=\s*(0x[0-9a-fA-F]+)\s*,\s*FLAGS_27_USED\s*=\s*(0x[0-9a-fA-F]+)/);
need(siteM, 'FLAG_LENGTHS, FLAGS_26_USED and FLAGS_27_USED in site/index.html');
{
  const lens = siteM[1].split(',').map((s) => Number(s.trim()));
  const want = [...new Set([codeLength(new Array(28).fill(0)),
    codeLength(Object.assign(new Array(28).fill(0), { 25: 1 })),
    codeLength(Object.assign(new Array(28).fill(0), { 26: 0xff }))])].sort((a, b) => a - b);
  if (lens.join() !== want.join() || Number(siteM[2]) !== FLAGS_26_USED || Number(siteM[3]) !== FLAGS_27_USED) {
    console.error('site_parity: site/index.html has FLAG_LENGTHS ' + lens + ', FLAGS_26_USED ' + siteM[2] + ', FLAGS_27_USED ' + siteM[3] +
      '; common/dwr.h has ' + want + ', ' + FLAGS_26_USED + ', ' + FLAGS_27_USED);
    process.exit(1);
  }
}

// The site's own length rule (in flags()) must agree with FLAG_CODE_LENGTH for every value of bytes 25 to 27,
// including byte 27 bits that no flag uses yet.
const SITE_HTML = fs.readFileSync(path.join(SITE, 'index.html'), 'utf8');
{
  const m = SITE_HTML.match(/const len=(\(\(bytes\[26\][^;]+);/);
  const siteLength = need(m ? new Function('bytes', 'return ' + m[1] + ';') : null, 'the length rule in site/index.html flags()');
  for (const b25 of [0, 1, 0xff]) for (let b26 = 0; b26 < 256; b26++) for (let b27 = 0; b27 < 256; b27 += 16) {
    const raw = Object.assign(new Array(28).fill(0), { 25: b25, 26: b26, 27: b27 });
    if (siteLength(raw) !== codeLength(raw)) {
      console.error('site_parity: code length differs for bytes 25-27 = ' + [b25, b26, b27].map((x) => x.toString(16)) +
        ': site ' + siteLength(raw) + ', dwr.h ' + codeLength(raw));
      process.exit(1);
    }
  }
}

// The site's PERSONAL options must not change the checksum, or race links that leave them out would split races.
const SITE_PERSONAL = need((SITE_HTML.match(/const PERSONAL = \[([^\]]*)\]/) || [])[1], 'PERSONAL in site/index.html')
  .match(/'([^']+)'/g).map((s) => s.slice(1, -1));
const SITE_FLAG_MAP = JSON.parse(need((SITE_HTML.match(/const FLAG_MAP = (\{.*?\});\s*\n/) || [])[1], 'FLAG_MAP in site/index.html'));

function decode(code) {
  let bits = '';
  for (const c of code) bits += A.indexOf(c).toString(2).padStart(5, '0');
  const out = new Array(FLAG_BYTES).fill(0);
  for (let i = 0; i * 8 + 8 <= bits.length && i < FLAG_BYTES; i++) out[i] = parseInt(bits.slice(i * 8, i * 8 + 8), 2);
  return out;
}

function encode(raw) {
  const n = codeLength(raw);
  let bits = '';
  for (let i = 0; i < FLAG_BYTES; i++) bits += (i < FLAG_BYTES - 1 ? raw[i] : 0).toString(2).padStart(8, '0');
  let s = '';
  for (let i = 0; i < n; i++) s += A[parseInt(bits.slice(i * 5, i * 5 + 5), 2)];
  return s;
}

// Gob Standard with one fixed biome, set the way cli/main.c --biome sets it. The 0x33 clear is that
// file's own choice (it also clears the two bits next to the pairs), so it stays here.
function withBiome(code, biome) {
  const raw = decode(code);
  raw[2] &= ~0x33 & 0xff;
  raw[25] = 0;
  if (biome >= BIOME_LAKE) raw[25] = biome;
  else raw[2] |= ((biome & 2) << BIOME_SHIFT) & BIOME_HI | (biome & 1) & BIOME_LO;
  return encode(raw);
}

// Runs the CLI and returns its checksum and the bytes of the .nes it wrote into dir.
function cliBuild(flags, seed, dir) {
  let log;
  try {
    log = execFileSync(EXE, [ROM, flags, String(seed), SPRITE, dir], { encoding: 'utf8' });
  } catch (e) {
    return { sum: 'CLI failed', rom: null };
  }
  const winter = log.match(/Checksum without winter theme: ([0-9a-fA-F]{16})/);
  const plain = log.match(/Checksum: ([0-9a-fA-F]{16})/);
  const nes = fs.readdirSync(dir).filter((f) => f.toLowerCase().endsWith('.nes'));
  return {
    sum: (winter || plain || [null, 'no checksum'])[1].toLowerCase(),   // the CLI prints upper case
    rom: nes.length === 1 ? fs.readFileSync(path.join(dir, nes[0])) : null,
  };
}

// First offset where two ROMs differ, or a length note, or '' when identical.
function firstDiff(a, b) {
  if (!a || !b) return 'a ROM is missing';
  const n = Math.min(a.length, b.length);
  const at = [];
  let count = 0;
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) { count++; if (at.length < 8) at.push('0x' + i.toString(16) + ' site ' + a[i].toString(16) + ' cli ' + b[i].toString(16)); }
  if (count) return count + ' bytes differ, first at ' + at.join('; ');
  return a.length === b.length ? '' : 'lengths differ (site ' + a.length + ', cli ' + b.length + ')';
}
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');

// Gob Standard with Random Map off and the Zelda II map on, set the way cli/main.c --z2-map and
// --lock-tantegel set byte 26: 0x10 map, 0x0c shuffle (1 Lite, 2 Deep), 0x02 Lock Tantegel.
function withZ2(code, shuffle, lock) {
  const raw = decode(code);
  raw[0] &= ~0x03 & 0xff;
  raw[26] = (raw[26] & ~0x1e & 0xff) | 0x10 | (shuffle << 2) | (lock ? 0x02 : 0);
  return encode(raw);
}

const flagSets = [['Gob Standard', GOB_STANDARD]]
  .concat(BIOMES.map((b, i) => [b, withBiome(GOB_STANDARD, i)]))
  .concat([['Z2', withZ2(GOB_STANDARD, 0, false)], ['Z2 Lite', withZ2(GOB_STANDARD, 1, false)],
    ['Z2 Deep Lock', withZ2(GOB_STANDARD, 2, true)]]);
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'dwr-parity-'));
const rom = fs.readFileSync(ROM);

globalThis.require = require;
globalThis.__dirname = SITE;
globalThis.__filename = path.join(SITE, 'dwrandomizer.js');
globalThis.Module = {
  print: () => {},
  printErr: () => {},
  locateFile: (f) => path.join(SITE, f),
  onRuntimeInitialized() { run(); },
};
vm.runInThisContext(fs.readFileSync(path.join(SITE, 'dwrandomizer.js'), 'utf8'), { filename: globalThis.__filename });

function run() {
  const M = globalThis.Module;
  const FS = globalThis.FS || M.FS;
  let bad = 0, total = 0;
  {
    // Every PERSONAL option off, then every one on: the CLI checksum must stay the same.
    const set = (on) => {
      const raw = decode(GOB_STANDARD);
      for (const id of SITE_PERSONAL) {
        const m = SITE_FLAG_MAP[id];
        if (!m) { console.error('site_parity: PERSONAL option ' + id + ' is not in FLAG_MAP'); process.exit(1); }
        raw[m.byte] = (raw[m.byte] & ~(m.mask << m.shift) & 0xff) | ((on ? 1 : 0) << m.shift);
      }
      return encode(raw);
    };
    const off = cliBuild(set(false), 1001, fs.mkdtempSync(path.join(work, 'p'))).sum;
    const on = cliBuild(set(true), 1001, fs.mkdtempSync(path.join(work, 'p'))).sum;
    total++;
    if (off !== on || !/^[0-9a-f]{16}$/.test(off)) bad++;
    console.log((off === on ? 'same ' : 'DIFF ') + 'PERSONAL (' + SITE_PERSONAL.length + ' options) off ' + off + ' on ' + on);

    // --race-card prints the flags without those options. Its masks in cli/main.c must clear the same bits as
    // the site's PERSONAL list, so the printed code equals the all-on code with every PERSONAL bit cleared.
    const cleared = decode(set(true));
    for (const id of SITE_PERSONAL) { const m = SITE_FLAG_MAP[id]; cleared[m.byte] &= ~(m.mask << m.shift) & 0xff; }
    const want = encode(cleared);
    let card = '';
    try {
      card = execFileSync(EXE, ['--race-card', 'on', ROM, set(true), '1001', SPRITE, fs.mkdtempSync(path.join(work, 'c'))], { encoding: 'utf8' });
    } catch (e) { /* reported below */ }
    const got = (card.match(/^Flags: (\S+)/m) || [])[1];
    total++;
    if (got !== want) bad++;
    console.log((got === want ? 'same ' : 'DIFF ') + '--race-card flags ' + got + ' want ' + want);
  }
  for (const [name, flags] of flagSets) {
    for (let s = 1; s <= SEEDS; s++) {
      const seed = 1000 + s;
      // dwr_randomize reseeds its RNG from time(NULL) after the checksum (common/dwr.c, "reseed the RNG so
      // the rest isn't deterministic"), and the music placement uses it. The site build and the CLI build
      // therefore only give the same ROM when they start in the same second. A pair that straddles a
      // second boundary (the slow Riverlands and Earth maps often do) is run again, up to TRIES times; a
      // real difference in the output stays different on every try.
      const TRIES = 30;
      let site, cli, siteRom, romDiff, same, tries = 0;
      do {
        tries++;
        FS.writeFile('/input.nes', rom);
        M.ccall('custom_music_clear', null, [], []);
        M.ccall('dwr_set_spoiler', null, ['number'], [0]);
        let sum = M.ccall('dwr_randomize', 'bigint', ['string', 'bigint', 'string', 'string', 'string'],
          ['/input.nes', BigInt(seed), flags, SPRITE, '/']);
        if (sum < 0n) sum += 1n << 64n;
        const out = '/DWR.' + seed + '.' + flags + '.nes';
        siteRom = null;
        try { siteRom = Buffer.from(FS.readFile(out)); FS.unlink(out); } catch (e) { /* no file: the build failed */ }
        site = sum ? sum.toString(16).padStart(16, '0') : 'site failed';
        const dir = fs.mkdtempSync(path.join(work, 'r'));
        cli = cliBuild(flags, seed, dir);
        romDiff = site === cli.sum ? firstDiff(siteRom, cli.rom) : '';
        same = site === cli.sum && !romDiff && siteRom && sha(siteRom) === sha(cli.rom);
      } while (!same && site === cli.sum && tries < TRIES);
      // A slow map (Riverlands seed 1005) can take longer than a second to build, so the two builds never
      // reseed in the same second. Then the only fair compare leaves out the music banks (4 and 5, file offsets
      // 0x10010 to 0x18010), where the clock decides the song layout. Everything else must still match.
      let musicOnly = false;
      if (!same && site === cli.sum && siteRom && cli.rom && siteRom.length === cli.rom.length) {
        const a = Buffer.from(siteRom), b = Buffer.from(cli.rom);
        a.fill(0, 0x10010, 0x18010); b.fill(0, 0x10010, 0x18010);
        if (sha(a) === sha(b)) { same = true; musicOnly = true; }
      }
      total++;
      if (!same) bad++;
      console.log((same ? 'same ' : 'DIFF ') + name.padEnd(12) + ' seed ' + seed + ' site ' + site + ' cli ' + cli.sum +
        (siteRom ? ' sha ' + sha(siteRom).slice(0, 12) : '') + (tries > 1 ? ' (try ' + tries + ')' : '') + (musicOnly ? ' (music banks differ only: clock reseed)' : '') +
        (romDiff ? ' ROM: ' + romDiff : ''));
    }
  }
  fs.rmSync(work, { recursive: true, force: true });
  console.log(bad ? bad + ' of ' + total + ' differ' : 'all ' + total + ' match');
  process.exit(bad ? 1 : 0);
}
