// The fractonaut icon (C14 Neon sunburst): writes the SVG sources every platform's
// icon is made from.   node icons/make_icons.mjs   ->   icons/*.svg
// The PNGs beside them are these SVGs drawn on a browser canvas at the exact pixel
// size (1024, 1024, 432); mac/build.sh, the Android resources and index.html use them.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const DIR = path.dirname(fileURLToPath(import.meta.url));

const PINK = '#ff3d9a', PALE = '#ff9ccd';
const SLIM = 'M50 50 C45 40 45.5 24 50 13 C54.5 24 55 40 50 50Z';
const defs = `<defs>
  <radialGradient id="bg" cx=".5" cy=".45" r=".75"><stop offset="0" stop-color="#1a0a16"/><stop offset="1" stop-color="#000"/></radialGradient>
  <mask id="hole"><rect x="-50" y="-50" width="200" height="200" fill="#fff"/><circle cx="50" cy="50" r="9" fill="#000"/></mask>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="2"/></filter>
  <filter id="shadow" x="-20%" y="-20%" width="140%" height="150%"><feDropShadow dx="0" dy="1.2" stdDeviation="1.6" flood-opacity=".4"/></filter>
  <clipPath id="squircle"><rect x="9.8" y="9.8" width="80.4" height="80.4" rx="18.2"/></clipPath>
</defs>`;
const rays = `<g mask="url(#hole)">${Array.from({ length: 16 }, (_, i) =>
  `<path d="${SLIM}" transform="rotate(${i * 22.5} 50 50)" fill="${PINK}" fill-opacity=".14" stroke="${PINK}" stroke-width="1.1" stroke-linejoin="round"/>`).join('')}</g>` +
  `<circle cx="50" cy="50" r="4" fill="none" stroke="${PALE}" stroke-width="1.6"/>`;
const burst = `<g filter="url(#glow)" opacity=".8">${rays}</g>${rays}`;
const svg = (body, box = '0 0 100 100') => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${box}">${defs}${body}</svg>\n`;

const out = {
  // Full-bleed square: web app icons, apple-touch-icon.
  'square.svg': svg(`<rect width="100" height="100" fill="url(#bg)"/>${burst}`),
  // Mac: the squircle on the 824/1024 grid with its shadow, transparent outside.
  'mac.svg': svg(`<rect x="9.8" y="9.8" width="80.4" height="80.4" rx="18.2" fill="#000" filter="url(#shadow)"/>
    <g clip-path="url(#squircle)"><svg x="9.8" y="9.8" width="80.4" height="80.4" viewBox="4 4 92 92"><rect width="100" height="100" fill="url(#bg)"/>${burst}</svg></g>`),
  // Android adaptive foreground: transparent, the burst inside the middle 72 of 108.
  'android_fg.svg': svg(burst, '-19.444 -19.444 138.889 138.889'),
};
for (const [name, text] of Object.entries(out)) fs.writeFileSync(path.join(DIR, name), text);
console.log('make_icons: wrote ' + Object.keys(out).join(', '));
