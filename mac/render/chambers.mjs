// Test helper: prints each chamber's target (web: chamberTarget(i) at startup, i.e.
// DEFAULTS overlaid with CHAMBERS[i].p) from index.html, one "slug key value" per line.
import fs from 'node:fs';
const html = fs.readFileSync(process.argv[2], 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const src = blocks.find((b) => b.includes('const CHAMBERS = ['));
const { CHAMBERS, DEFAULTS } = new Function(src + '; return { CHAMBERS, DEFAULTS };')();
for (const c of CHAMBERS) {
  const t = { ...DEFAULTS, ...c.p };
  for (const [k, v] of Object.entries(t)) console.log(c.slug, k, typeof v === 'boolean' ? +v : v);
}
