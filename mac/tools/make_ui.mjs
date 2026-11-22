// Builds the native app's interface page from the web app: ../index.html with its
// engine blocks swapped for web/shim.js and a few anchored patches where the
// interface touched browser features that are now native.
//   node tools/make_ui.mjs   ->   build/web/index.html (+ thumbs/)
// Every anchor must match exactly once, or the build stops: a change to index.html
// can never quietly produce a broken interface.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const MAC = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const SRC = path.resolve(MAC, '..', 'index.html');
const OUT = path.join(MAC, 'build', 'web');
const fail = (msg) => { console.error(`make_ui: ${msg}`); process.exit(1); };

function once(text, find, what) {
  const i = text.indexOf(find);
  if (i < 0) fail(`anchor not found: ${what}`);
  if (text.indexOf(find, i + 1) >= 0) fail(`anchor not unique: ${what}`);
  return i;
}
const replace = (text, find, to, what) => { const i = once(text, find, what); return text.slice(0, i) + to + text.slice(i + find.length); };
// Replace from `start` through the end of `end` (the first `end` after `start`).
function replaceSpan(text, start, end, to, what) {
  const i = once(text, start, what + ' (start)');
  const j = text.indexOf(end, i);
  if (j < 0) fail(`anchor not found: ${what} (end)`);
  return text.slice(0, i) + to + text.slice(j + end.length);
}

let html = fs.readFileSync(SRC, 'utf8');
const shim = fs.readFileSync(path.join(MAC, 'web', 'shim.js'), 'utf8');

// ---------------- script blocks, known by their first comment ----------------
const KINDS = [
  ['kit', '// Material Next kit runtime', 'keep'],
  ['params', '// Parameter schema, palettes and chambers', 'keep'],
  ['fold', '// Space folding shared by GPU and CPU', 'keep'],
  ['shaders', '// GLSL ES 3.0 sources', 'shim'],
  ['renderer', '// WebGL2 pipeline', 'drop'],
  ['pens', '// Pens: pointers', 'drop'],
  ['inputs', '// External inputs', 'drop'],
  ['sound', '// Sound, both ways', 'drop'],
  ['mp4', '// Browsers record MP4 in fragments', 'drop'],
  ['ui', '// Interface: a dock', 'ui'],
  ['main', '// Frame loop', 'start'],
];
const seen = new Set();
const blocks = [];
const bodies = {};
html = html.replace(/<script>([\s\S]*?)<\/script>/g, (all, body) => {
  const first = body.trimStart().split('\n')[0];
  const kind = KINDS.find(([, head]) => first.startsWith(head));
  if (!kind) fail(`unknown script block starting "${first.slice(0, 60)}"`);
  if (seen.has(kind[0])) fail(`script block "${kind[0]}" appears twice`);
  seen.add(kind[0]);
  blocks.push(kind[0]);
  bodies[kind[0]] = body;
  return `\u0000${kind[0]}\u0000`;
});
for (const [k] of KINDS) if (!seen.has(k)) fail(`script block "${k}" not found`);
if (blocks.join() !== KINDS.map(([k]) => k).join()) fail(`script blocks out of order: ${blocks.join(', ')}`);

// ---------------- patches to the interface block ----------------
let ui = bodies.ui;

// 1. Picture: the frame is saved natively (the canvas has no picture of its own).
ui = replace(ui,
  "canvas.toBlob((b) => b && download(b, `fractonaut-${CHAMBERS[chamber].slug}-${stamp()}.png`), 'image/png');",
  'Main.snapshot(`fractonaut-${CHAMBERS[chamber].slug}-${stamp()}.png`);',
  'afterRender snapshot');

// 2. 4K export: rendered and saved natively; same sizes, toasts and file name.
ui = replaceSpan(ui,
  '    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));\n    UI.paused = true;',
  '      UI.paused = false;\n    }\n',
  '    const saved = await Main.exportStill(w, h, `fractonaut-${CHAMBERS[chamber].slug}-${w}x${h}-${stamp()}.png`);\n' +
  "    toast(saved ? `${w} × ${h} image saved to downloads` : 'Not enough graphics memory · try a smaller size');\n",
  'exportHiRes body');

// 3. Film: no MediaRecorder needed natively.
ui = replace(ui,
  "    if (!window.MediaRecorder || !canvas.captureStream) { toast(\"This browser can't record video · try Chrome\"); return; }\n",
  '',
  'toggleRecord MediaRecorder check');

// 4. Film: recorded, saved and shared natively; same states, toasts and keys.
ui = replaceSpan(ui,
  '  function beginRecord() {',
  "    toast(audio ? 'Recording with sound · press R to stop' : 'Recording · press R to stop');\n  }\n",
  `  function beginRecord() {
    const end = () => { recorder = null; stopping = false; Main.filming = false; setRecording(false); };
    const r = Mp4.record(\`fractonaut-\${CHAMBERS[chamber].slug}-\${stamp()}.mp4\`, {
      started: (sound) => { recorder = r; refresh(); toast(sound ? 'Recording with sound · press R to stop' : 'Recording · press R to stop'); },
      saved: (file) => { end(); mn.toast('Video saved to downloads', { action: 'Share or save to Photos', onAction: () => Mp4.share(file), timeout: 12000 }); },
      failed: () => { end(); toast('Recording stopped · the browser reported an error'); },
    });
  }
`,
  'beginRecord');

// 5. Link: the public address, not the app's own; copied natively.
ui = replace(ui,
  "navigator.clipboard.writeText(location.href).then(() => toast('Link copied · it opens this exact view'), () => toast(location.href));",
  "const href = 'https://punitvthakkar.github.io/kaleidoscope/' + location.hash;\n      Main.copy(href).then(() => toast('Link copied · it opens this exact view'), () => toast(href));",
  'copyLink');

// ---------------- head: no remote hosts, and a see-through page ----------------
html = html.replace(/<meta http-equiv="Content-Security-Policy" content="[^"]*">/, (m) => {
  seen.add('csp');
  return `<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self' data:; object-src 'none'; base-uri 'none'; form-action 'none'">`;
});
if (!seen.has('csp')) fail('anchor not found: Content-Security-Policy meta');
// The Metal picture shows through. Last in the cascade; nothing else changes.
// Endless decoration (the turning orb, the flowing wordmark) would recomposite the whole
// page over the picture every frame: here it runs only while the halo is open.
html = replace(html, '</head>', `  <style>
html, body, #c { background: transparent; }
.orb-iris, .brand-o, .brand-word { animation-play-state: paused; }
body:has(.orb[aria-expanded="true"]) :is(.orb-iris, .brand-o, .brand-word) { animation-play-state: running; }
</style>
</head>`, '</head>');

// ---------------- assemble ----------------
const tag = (s) => `<script>\n${s}</script>`;
const parts = {
  kit: tag(bodies.kit), params: tag(bodies.params), fold: tag(bodies.fold),
  shaders: tag(shim), renderer: '', pens: '', inputs: '', sound: '', mp4: '',
  ui: tag(ui), main: tag('Main.start();\n'),
};
html = html.replace(/\u0000(\w+)\u0000\n?/g, (m, k) => (parts[k] ? parts[k] + '\n' : ''));

fs.mkdirSync(OUT, { recursive: true });
fs.writeFileSync(path.join(OUT, 'index.html'), html);
if (html.includes('thumbs/')) fs.cpSync(path.resolve(MAC, '..', 'thumbs'), path.join(OUT, 'thumbs'), { recursive: true });
console.log(`make_ui: wrote ${path.relative(process.cwd(), path.join(OUT, 'index.html'))}`);
