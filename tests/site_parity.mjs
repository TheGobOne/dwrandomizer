// Site parity: the site's wasm build and the desktop CLI must give the same checksum for the same
// flags and seed. Builds seeds across Gob Standard and each biome with both and compares.
//
// Usage, from the repo root, after building both (cmake --build build, and the wasm build):
//   node tests/site_parity.mjs [vanilla rom] [seeds per flag set]
// Defaults: C:/ROMs/Dragon_warrior.nes and 5 seeds; DWR_EXE picks another CLI. Exits 1 on any difference or failed build.
import { createRequire } from 'module';
import { execFileSync } from 'child_process';
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
const BIOMES = ['Continent', 'Caldera', 'Wasteland', 'Riverlands', 'Lake', 'Earth', 'Westeros'];
const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';

function decode(code) {
  let bits = '';
  for (const c of code) bits += A.indexOf(c).toString(2).padStart(5, '0');
  const out = new Array(28).fill(0);
  for (let i = 0; i * 8 + 8 <= bits.length && i < 28; i++) out[i] = parseInt(bits.slice(i * 8, i * 8 + 8), 2);
  return out;
}

// Same length rule as FLAG_CODE_LENGTH in common/dwr.h.
function encode(raw) {
  const n = (raw[26] & 0x3f) ? 44 : (raw[25] || raw[26]) ? 42 : 40;
  let bits = '';
  for (let i = 0; i < 28; i++) bits += (i < 27 ? raw[i] : 0).toString(2).padStart(8, '0');
  let s = '';
  for (let i = 0; i < n; i++) s += A[parseInt(bits.slice(i * 5, i * 5 + 5), 2)];
  return s;
}

// Gob Standard with one fixed biome, set the way cli/main.c --biome sets it.
function withBiome(code, biome) {
  const raw = decode(code);
  raw[2] &= ~0x33 & 0xff;
  raw[25] = 0;
  if (biome >= 4) raw[25] = biome;
  else raw[2] |= ((biome & 2) << 3) | (biome & 1);
  return encode(raw);
}

function cliChecksum(flags, seed, dir) {
  let log;
  try {
    log = execFileSync(EXE, [ROM, flags, String(seed), 'Loto', dir], { encoding: 'utf8' });
  } catch (e) {
    return 'CLI failed';
  }
  const winter = log.match(/Checksum without winter theme: ([0-9a-fA-F]{16})/);
  const plain = log.match(/Checksum: ([0-9a-fA-F]{16})/);
  return (winter || plain || [null, 'no checksum'])[1].toLowerCase();   // the CLI prints upper case
}

const flagSets = [['Gob Standard', GOB_STANDARD]].concat(BIOMES.map((b, i) => [b, withBiome(GOB_STANDARD, i)]));
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
  for (const [name, flags] of flagSets) {
    for (let s = 1; s <= SEEDS; s++) {
      const seed = 1000 + s;
      FS.writeFile('/input.nes', rom);
      M.ccall('custom_music_clear', null, [], []);
      M.ccall('dwr_set_spoiler', null, ['number'], [0]);
      let sum = M.ccall('dwr_randomize', 'bigint', ['string', 'bigint', 'string', 'string', 'string'],
        ['/input.nes', BigInt(seed), flags, 'Random', '/']);
      if (sum < 0n) sum += 1n << 64n;
      const out = '/DWR.' + seed + '.' + flags + '.nes';
      try { FS.unlink(out); } catch (e) { /* no file: the build failed */ }
      const site = sum ? sum.toString(16).padStart(16, '0') : 'site failed';
      const dir = fs.mkdtempSync(path.join(work, 'r'));
      const cli = cliChecksum(flags, seed, dir);
      total++;
      if (site !== cli) bad++;
      console.log((site === cli ? 'same ' : 'DIFF ') + name.padEnd(12) + ' seed ' + seed + ' site ' + site + ' cli ' + cli);
    }
  }
  fs.rmSync(work, { recursive: true, force: true });
  console.log(bad ? bad + ' of ' + total + ' differ' : 'all ' + total + ' match');
  process.exit(bad ? 1 : 0);
}
