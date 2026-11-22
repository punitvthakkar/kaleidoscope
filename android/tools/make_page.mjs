// Android adapter for the interface page. Usage: make_page.mjs <index.html> <assetsDir>
// Patches <assetsDir>/web/index.html (the Mac's page, copied by build.sh) so that
// shim.js finds `window.webkit.messageHandlers.fx` and posts through the
// addJavascriptInterface object `fxAndroid` (MainActivity). The adapter also tells
// the app, per touch, whether it landed on the interface or the bare picture, using
// the shim's own overUI rule, so painting never waits on a separate overUI message.
import fs from 'node:fs';
import path from 'node:path';

const [, , , assets] = process.argv;
if (!assets) { console.error('make_page: usage: make_page.mjs <index.html> <assetsDir>'); process.exit(1); }
const page = path.join(assets, 'web', 'index.html');
const html = fs.readFileSync(page, 'utf8');

const ANCHOR = '/* fractonaut native bridge';
const at = html.indexOf(ANCHOR);
if (at < 0 || html.indexOf(ANCHOR, at + 1) >= 0) {
  console.error(`make_page: expected exactly one "${ANCHOR}" in ${page}`);
  process.exit(1);
}

const ADAPTER = `/* android adapter (android/tools/make_page.mjs) */
(() => {
  const a = window.fxAndroid;
  if (!a) return;
  window.webkit = { messageHandlers: { fx: { postMessage: (batch) => a.post(JSON.stringify(batch)) } } };
  addEventListener('pointerdown', (e) => {
    const t = e.target, ui = !(t === document.getElementById('c') || t === document.body || t === document.documentElement);
    a.down(e.clientX, e.clientY, ui);
  }, true);
})();
`;
fs.writeFileSync(page, html.slice(0, at) + ADAPTER + html.slice(at));
