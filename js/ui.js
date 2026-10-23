// Interface: a dock with the three ingredients of a kaleidoscope (Chamber,
// Light, Mood) plus Wander, live panels with a few plain-language dials, and
// "All settings" for every parameter. Also: chambers + morphing, Turn,
// the URL hash, keyboard, capture.
// UI built with Material Next: an independent design language built on Material 3, not an official Google design.

const UI = (() => {
  const $ = (s) => document.querySelector(s);
  let canvas;
  let morph = null;
  let vjTimer = 0, hashTimer = 0, refreshTimer = 0, captionTimer = 0, idleTimer = 0;
  let recorder = null, snapRequested = false;
  let chamber = 0, openPanel = null;
  const stats = { fps: 0, quality: 1 };
  const NUMERIC = Object.keys(SCHEMA).filter((k) => typeof SCHEMA[k][0] === 'number');
  const STEPPED = ['symmetry', 'kifsIters', 'hypQ'];        // integers that re-fold the picture
  const STRUCTURAL = ['mode', 'gen', 'foldInLoop', 'mirror', ...STEPPED];
  const refreshers = [];                                     // fn() that pull values from params

  const cap = (s) => s[0].toUpperCase() + s.slice(1);
  const name = (v) => OPTION_NAMES[v] || cap(String(v));

  // ---------------- params ----------------
  function setParam(key, value) {
    if (params[key] === value) return;
    if (STRUCTURAL.includes(key)) Renderer.startVeil();
    params[key] = value;
    if (morph) morph.keys = morph.keys.filter((k) => k !== key);
    hashTimer = 0.4;
    if (key === 'mode') renderShapeDial();
    refreshSoon();
  }

  function setNormalized(key, v) {
    const s = SCHEMA[key];
    if (Array.isArray(s[1])) setParam(key, s[1][Math.min(s[1].length - 1, Math.floor(v * s[1].length))]);
    else if (typeof s[0] === 'boolean') setParam(key, v > 0.5);
    else setParam(key, s[3] >= 1 ? Math.round(s[1] + v * (s[2] - s[1])) : s[1] + v * (s[2] - s[1]));
  }

  function chamberTarget(i) {
    const t = { ...DEFAULTS, ...CHAMBERS[i].p };
    for (const k of STICKY) t[k] = params[k];
    return t;
  }

  // Structural values switch at once behind a crossfade; numbers glide.
  function morphTo(t, instant) {
    const from = { ...params };
    const changesShape = STRUCTURAL.some((k) => k in t && t[k] !== from[k]);
    if (changesShape && !instant) Renderer.startVeil();
    for (const k of Object.keys(SCHEMA)) {
      if (!(k in t)) continue;
      if (typeof SCHEMA[k][0] !== 'number' || STEPPED.includes(k)) params[k] = t[k];
    }
    const keys = NUMERIC.filter((k) => !STEPPED.includes(k) && k in t && t[k] !== from[k]);
    if (instant || system.morphTime <= 0) {
      for (const k of keys) params[k] = t[k];
      morph = null;
    } else {
      morph = { from, to: t, keys, t: 0 };
    }
    renderShapeDial();
    hashTimer = 0.4;
    refresh();
  }

  function updateMorph(dt) {
    if (!morph) return;
    morph.t = Math.min(1, morph.t + dt / system.morphTime);
    const e = morph.t * morph.t * (3 - 2 * morph.t);
    for (const k of morph.keys) {
      const v = morph.from[k] + (morph.to[k] - morph.from[k]) * e;
      params[k] = SCHEMA[k][3] >= 1 ? Math.round(v) : v;
    }
    if (morph.t >= 1) { morph = null; hashTimer = 0.4; }
    refreshSoon();
  }

  function enterChamber(i, { instant = false, quiet = false } = {}) {
    chamber = (i + CHAMBERS.length) % CHAMBERS.length;
    morphTo(chamberTarget(chamber), instant);
    if (!quiet) showCaption();
  }

  const rand = (lo, hi) => lo + Math.random() * (hi - lo);
  const pick = (a) => a[Math.floor(Math.random() * a.length)];

  // Like turning a real kaleidoscope: same chamber, a new arrangement.
  function turn() {
    const t = { ...params };
    for (const [k, s] of Object.entries(SCHEMA)) {
      const m = s[6];
      if (!m || Math.random() < 0.5) continue;
      if (Array.isArray(s[1])) t[k] = pick(s[1]);
      else if (typeof s[0] === 'boolean') t[k] = Math.random() < 0.5;
      else {
        const [lo, hi] = Array.isArray(m) ? m : [s[1], s[2]];
        t[k] = s[3] >= 1 ? Math.round(rand(lo, hi)) : rand(lo, hi);
      }
    }
    // One wanderer stroke is tiled across the whole hyperbolic disk, so
    // wanderers there turn calm into flicker; Turn leaves them as they are.
    if (t.mode === 'hyperbolic') t.autoPens = params.autoPens;
    morphTo(t);
  }

  function pinch() { setParam('symmetry', params.symmetry >= 16 ? 3 : params.symmetry + 1); }
  function note(n) { enterChamber(n % CHAMBERS.length); }

  // ---------------- URL hash ----------------
  function writeHash() {
    const diff = { _c: CHAMBERS[chamber].slug };
    for (const k of Object.keys(SCHEMA)) {
      const v = params[k];
      if (v !== DEFAULTS[k]) diff[k] = typeof v === 'number' ? +v.toFixed(4) : v;
    }
    const enc = btoa(JSON.stringify(diff)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
    history.replaceState(null, '', '#' + enc);
  }

  function readHash() {
    const h = location.hash.slice(1);
    if (!h) return null;
    try {
      const obj = JSON.parse(atob(h.replace(/-/g, '+').replace(/_/g, '/')));
      const out = { params: {}, chamber: Math.max(0, CHAMBERS.findIndex((c) => c.slug === obj._c)) };
      for (const [k, v] of Object.entries(obj)) {
        const s = SCHEMA[k];
        if (s && (Array.isArray(s[1]) ? s[1].includes(v) : typeof v === typeof s[0])) out.params[k] = v;
      }
      return out;
    } catch (e) {
      return null;
    }
  }

  // ---------------- building blocks ----------------
  function el(tag, attrs = {}, html = '') {
    const e = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
      if (k === 'class') e.className = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else e.setAttribute(k, v);
    }
    if (html) e.innerHTML = html;
    return e;
  }

  // A dial: one plain-language control with words at both ends.
  // spec: { label, sub, ends, min, max, step, get(), set(v), out(v) }
  let dialId = 0;
  function dial(host, spec) {
    host.innerHTML = '';
    const id = 'dial-' + ++dialId;
    const head = el('div', { class: 'dial-head' });
    head.append(el('label', { class: 'mn-title-s', for: id }, spec.label));
    const out = el('output', { class: 'mn-label-l mn-num mn-muted', for: id });
    head.append(out);
    host.append(head);
    if (spec.sub) host.append(el('p', { class: 'mn-body-s mn-muted dial-sub' }, spec.sub));
    const input = el('input', { type: 'range', id, min: spec.min, max: spec.max, step: spec.step });
    input.addEventListener('input', () => { spec.set(+input.value); out.textContent = spec.out(spec.get()); });
    host.append(input);
    host.append(el('div', { class: 'dial-ends mn-label-s mn-muted' }, `<span>${spec.ends[0]}</span><span>${spec.ends[1]}</span>`));
    const pull = () => {
      if (document.activeElement !== input) input.value = spec.get();
      out.textContent = spec.out(spec.get());
    };
    pull();
    refreshers.push(pull);
    return pull;
  }

  const clamp01 = (v) => Math.max(0, Math.min(1, v));
  const echoSeconds = (d) => (d >= 0.9999 ? '∞' : d <= 0 ? '0 s' : `${(Math.log(0.5) / (60 * Math.log(d))).toFixed(1)} s`);

  // ---------------- Chamber panel ----------------
  function buildChambers() {
    const host = $('#chambers');
    CHAMBERS.forEach((c, i) => {
      const b = el('button', { class: 'mn-card chamber', 'data-i': i },
        `<div class="media"><img src="thumbs/${c.slug}.jpg" alt="" loading="lazy" decoding="async"></div>
         <span class="mn-title-s">${c.name}</span><span class="mn-body-s mn-muted mn-clamp" style="--lines: 2">${c.line}</span>`);
      b.addEventListener('click', () => enterChamber(i));
      host.append(b);
    });
    refreshers.push(() => {
      host.querySelectorAll('.chamber').forEach((b, i) => {
        if (i === chamber) b.setAttribute('aria-current', 'true'); else b.removeAttribute('aria-current');
      });
      $('#dockChamber').textContent = CHAMBERS[chamber].name;
    });
    $('#turn').addEventListener('click', turn);
  }

  // The shape dial means whatever "how many mirrors" means in this chamber.
  let shapeMode = null;
  function renderShapeDial() {
    if (shapeMode === params.mode) return;
    shapeMode = params.mode;
    const intDial = (key, label, ends, min, max) => ({
      label, ends, min, max, step: 1,
      get: () => params[key], set: (v) => setParam(key, Math.round(v)), out: (v) => String(Math.round(v)),
    });
    const specs = {
      none: intDial('symmetry', 'Mirrors', ['Few', 'Many'], 2, 16),
      kaleido: intDial('symmetry', 'Mirrors', ['Few', 'Many'], 2, 16),
      hyperbolic: intDial('symmetry', 'Mirrors', ['Few', 'Many'], 3, 12),
      kifs: intDial('kifsIters', 'Fold depth', ['Shallow', 'Deep'], 2, 10),
      droste: { label: 'Repeat ratio', ends: ['Tight', 'Wide'], min: 1.5, max: 8, step: 0.01,
        get: () => params.drosteScale, set: (v) => setParam('drosteScale', v), out: (v) => `${v.toFixed(1)}×` },
      tiles: { label: 'Tiles', ends: ['Few', 'Many'], min: 0.5, max: 6, step: 0.01,
        get: () => params.tileScale, set: (v) => setParam('tileScale', v), out: (v) => v.toFixed(1) },
    };
    const i = refreshers.indexOf(shapePull);
    if (i >= 0) refreshers.splice(i, 1);
    shapePull = dial($('#shapeDial'), specs[params.mode]);
  }
  let shapePull = null;

  // ---------------- Light panel ----------------
  function buildLight() {
    const seg = $('#brushSeg');
    for (const b of BRUSHES) {
      seg.append(el('button', { 'aria-pressed': 'false', onclick: () => setParam('brush', b) }, name(b)));
    }
    dial($('#wanderersDial'), {
      label: 'Wanderers', sub: 'Lights that paint on their own', ends: ['None', 'Many'], min: 0, max: 6, step: 1,
      get: () => params.autoPens, set: (v) => setParam('autoPens', v), out: (v) => String(v),
    });
    const chips = $('#genChips');
    for (const g of GENERATORS) {
      chips.append(el('button', { class: 'mn-chip', 'aria-pressed': 'false', onclick: () => setParam('gen', g) }, name(g)));
    }
    const sw = (id, fn) => $(id).addEventListener('change', async (e) => {
      const on = e.target.checked;
      try { await fn(on); } catch (err) { e.target.checked = false; toast(err.message || "That input isn't available here"); }
      refresh();
    });
    sw('#camSwitch', Inputs.setCam);
    sw('#handSwitch', Inputs.setHands);
    $('#clearGlass').addEventListener('click', clearGlass);
    refreshers.push(() => {
      seg.querySelectorAll('button').forEach((b, i) => b.setAttribute('aria-pressed', BRUSHES[i] === params.brush));
      chips.querySelectorAll('button').forEach((b, i) => b.setAttribute('aria-pressed', GENERATORS[i] === params.gen));
      $('#camSwitch').checked = Inputs.state.cam;
      $('#handSwitch').checked = Inputs.state.hands;
      $('#dockLight').textContent = Inputs.state.hands ? 'Fingertips' : Inputs.state.cam ? 'Camera'
        : params.gen !== 'none' ? name(params.gen) : name(params.brush);
    });
  }

  function clearGlass() {
    Renderer.clear();
    Pens.clear();
    mn.toast('Glass cleared', { action: 'Undo', onAction: () => Renderer.restore() });
  }

  // ---------------- Mood panel ----------------
  function buildMood() {
    const host = $('#swatches');
    for (const p of Object.keys(PALETTES)) {
      const b = el('button', { class: 'swatch', 'aria-label': cap(p), title: cap(p), 'aria-pressed': 'false', onclick: () => setParam('palette', p) });
      b.style.setProperty('--swatch', Palette.css(p));
      host.append(b);
    }
    refreshers.push(() => {
      host.querySelectorAll('.swatch').forEach((b) => b.setAttribute('aria-pressed', b.title.toLowerCase() === params.palette));
      $('#paletteName').textContent = cap(params.palette);
      $('#dockMood').textContent = cap(params.palette);
    });
    dial($('#paceDial'), {
      label: 'Pace', ends: ['Still', 'Lively'], min: 0.1, max: 2.5, step: 0.01,
      get: () => params.pace, set: (v) => setParam('pace', v), out: (v) => `${v.toFixed(1)}×`,
    });
    dial($('#echoDial'), {
      label: 'Echo', sub: 'How long light lingers', ends: ['Fleeting', 'Lingering'], min: 0, max: 1, step: 0.005,
      get: () => clamp01((params.fbDecay - 0.6) / 0.395), set: (v) => setParam('fbDecay', 0.6 + v * 0.395),
      out: () => echoSeconds(params.fbDecay),
    });
    dial($('#meltDial'), {
      label: 'Melt', ends: ['Crisp', 'Molten'], min: 0, max: 1, step: 0.005,
      get: () => clamp01(params.warpAmount / 0.5),
      set: (v) => { setParam('warpAmount', v * 0.5); setParam('fbWarp', v * 0.005); },
      out: (v) => `${Math.round(v * 100)}%`,
    });
    dial($('#glowDial'), {
      label: 'Glow', ends: ['Soft', 'Radiant'], min: 0, max: 1, step: 0.005,
      get: () => clamp01(params.bloom / 2.2),
      set: (v) => { setParam('bloom', v * 2.2); setParam('inkGlow', 0.4 + v * 1.8); },
      out: (v) => `${Math.round(v * 100)}%`,
    });
  }

  // ---------------- All settings ----------------
  const GROUPS = [
    ['Chamber', ['Chamber', 'Fractal', 'Circle limit', 'Spiral', 'Maze']],
    ['Light', ['Light']],
    ['Pattern', ['Patterns']],
    ['Camera', ['Camera']],
    ['Color & time', ['Color & time']],
    ['Echo', ['Echo']],
    ['Finish', ['Finish']],
  ];
  const MODE_OF_GROUP = { Fractal: 'kifs', 'Circle limit': 'hyperbolic', Spiral: 'droste', Maze: 'tiles' };

  function fmt(v, step) {
    if (step >= 1) return String(Math.round(v));
    return (+v.toFixed(Math.min(3, Math.ceil(-Math.log10(step))))).toString();
  }

  function settingRow(label, control, opts = {}) {
    const row = el('div', { class: 'set-row', 'data-search': (opts.search || label).toLowerCase() });
    if (opts.mode) row.dataset.mode = opts.mode;
    const head = el('div', { class: 'set-head' });
    head.append(el('label', { class: 'mn-body-m', for: control.id }, label));
    if (opts.out) head.append(opts.out);
    if (opts.inline) head.append(control);
    row.append(head);
    if (!opts.inline) row.append(control);
    return row;
  }

  let rowId = 0;
  function paramRow(key, mode) {
    const s = SCHEMA[key], id = 's-' + ++rowId;
    if (Array.isArray(s[1])) {
      const sel = el('select', { id, class: 'set-select' });
      s[1].forEach((v) => sel.append(el('option', { value: v }, name(v))));
      sel.addEventListener('change', () => setParam(key, sel.value));
      refreshers.push(() => { if (document.activeElement !== sel) sel.value = params[key]; });
      return settingRow(s[5], sel, { mode, inline: true, search: `${s[5]} ${s[4]}` });
    }
    if (typeof s[0] === 'boolean') {
      const sw = el('input', { id, type: 'checkbox', class: 'mn-switch', role: 'switch' });
      sw.addEventListener('change', () => setParam(key, sw.checked));
      refreshers.push(() => { sw.checked = params[key]; });
      return settingRow(s[5], sw, { mode, inline: true, search: `${s[5]} ${s[4]}` });
    }
    const input = el('input', { id, type: 'range', min: s[1], max: s[2], step: s[3] });
    const out = el('output', { class: 'mn-label-m mn-num mn-muted', for: id });
    input.addEventListener('input', () => { setParam(key, s[3] >= 1 ? Math.round(+input.value) : +input.value); out.textContent = fmt(params[key], s[3]); });
    refreshers.push(() => {
      if (document.activeElement !== input) input.value = params[key];
      out.textContent = fmt(params[key], s[3]);
    });
    return settingRow(s[5], input, { mode, out, search: `${s[5]} ${s[4]}` });
  }

  function group(title, rows, open) {
    const d = el('details', { class: 'mn-group' });
    if (open) d.open = true;
    const labels = rows.map((r) => r.querySelector('label')?.textContent).filter(Boolean);
    d.append(el('summary', {}, `<span class="text"><span class="title">${title}</span><span class="sub">${labels.slice(0, 3).join(', ')}</span></span><span class="count">${rows.filter((r) => !r.dataset.mode).length}</span>`));
    const body = el('div', { class: 'body set-body' });
    rows.forEach((r) => body.append(r));
    d.append(body);
    return d;
  }

  function systemRange(key, label, min, max, step, out) {
    const id = 's-' + ++rowId;
    const input = el('input', { id, type: 'range', min, max, step });
    const o = el('output', { class: 'mn-label-m mn-num mn-muted', for: id });
    input.addEventListener('input', () => { system[key] = +input.value; o.textContent = out(system[key]); if (key !== 'autoVJInterval' && key !== 'morphTime') Main.resize(); });
    refreshers.push(() => { if (document.activeElement !== input) input.value = system[key]; o.textContent = out(system[key]); });
    return settingRow(label, input, { out: o });
  }

  function switchRow(label, get, set, search) {
    const id = 's-' + ++rowId;
    const sw = el('input', { id, type: 'checkbox', class: 'mn-switch', role: 'switch' });
    sw.addEventListener('change', async () => {
      try { await set(sw.checked); } catch (err) { sw.checked = false; toast(err.message); }
      refresh();
    });
    refreshers.push(() => { sw.checked = get(); });
    return settingRow(label, sw, { inline: true, search });
  }

  function buildAll() {
    const host = $('#allGroups');
    GROUPS.forEach(([title, keyGroups], gi) => {
      const rows = [];
      for (const [k, s] of Object.entries(SCHEMA)) if (keyGroups.includes(s[4])) rows.push(paramRow(k, MODE_OF_GROUP[s[4]]));
      host.append(group(title, rows, gi === 0));
    });

    // Devices
    const S = Inputs.state;
    const learnSel = el('select', { id: 's-learn', class: 'set-select' });
    NUMERIC.forEach((k) => learnSel.append(el('option', { value: k }, SCHEMA[k][5])));
    learnSel.value = 'fbZoom';
    const learnBtn = el('button', { class: 'mn-btn tonal sm', onclick: () => Inputs.learn(learnSel.value) }, 'Assign next knob');
    const forget = el('button', { class: 'mn-btn text sm', onclick: () => { try { localStorage.removeItem('ks-midi'); } catch (e) {} toast('Knob assignments forgotten · reload to use the defaults'); } }, 'Forget knobs');
    const learnRow = settingRow('Knob controls', learnSel, { inline: true, search: 'midi knob learn assign' });
    const learnActions = el('div', { class: 'mn-row set-actions' });
    learnActions.append(learnBtn, forget);
    learnRow.append(learnActions);
    host.append(group('Devices', [
      switchRow('MIDI controller', () => S.midi, Inputs.setMidi, 'midi controller knobs'),
      learnRow,
      switchRow('All five fingertips paint', () => S.allFingers, (v) => { S.allFingers = v; }, 'fingertips hands'),
      switchRow('Tilt to steer', () => S.tilt, Inputs.setTilt, 'tilt phone motion'),
    ]));

    // Journey and performance
    const readout = el('p', { class: 'mn-body-s mn-muted mn-num set-readout' });
    const readRow = el('div', { class: 'set-row', 'data-search': 'fps frame rate resolution' });
    readRow.append(readout);
    refreshers.push(() => {
      const [w, h] = Renderer.size;
      readout.textContent = `${Math.round(stats.fps)} fps · ${w} × ${h}${stats.quality < 1 ? ` · ${Math.round(stats.quality * 100)}% for smoothness` : ''}`;
    });
    host.append(group('Journey & performance', [
      systemRange('autoVJInterval', 'Wander to a new view every', 10, 120, 1, (v) => `${v} s`),
      systemRange('morphTime', 'Transition length', 0, 15, 0.5, (v) => `${v} s`),
      switchRow('Soften flashes', () => system.flashGuard, (v) => { system.flashGuard = v; }, 'flash photosensitive seizure brightness'),
      switchRow('Keep motion smooth', () => system.autoQuality, (v) => { system.autoQuality = v; }, 'auto quality performance'),
      systemRange('renderScale', 'Resolution', 0.25, 2, 0.05, (v) => `${Math.round(v * 100)}%`),
      systemRange('maxDpr', 'Pixel density limit', 0.5, 3, 0.25, (v) => `${v}×`),
      readRow,
    ]));

    refreshers.push(() => {
      host.querySelectorAll('.set-row[data-mode]').forEach((r) => { r.hidden = r.dataset.mode !== params.mode || r.dataset.searchHidden === '1'; });
    });

    // Search opens groups with hits; clearing restores what was open.
    const search = $('#search');
    let saved = null;
    search.addEventListener('input', () => {
      const q = search.value.trim().toLowerCase();
      const groups = [...host.querySelectorAll('.mn-group')];
      if (q && !saved) saved = groups.map((g) => g.open);
      let any = false;
      groups.forEach((g, i) => {
        let hits = 0;
        g.querySelectorAll('.set-row').forEach((r) => {
          const hit = !q || r.dataset.search.includes(q);
          r.dataset.searchHidden = hit ? '0' : '1';
          r.hidden = !hit || (r.dataset.mode && r.dataset.mode !== params.mode);
          if (hit && !r.hidden) hits++;
        });
        g.hidden = q && hits === 0;
        g.querySelector('.count').textContent = q ? hits : g.querySelectorAll('.set-row:not([data-mode])').length;
        if (q) g.open = hits > 0; else if (saved) g.open = saved[i];
        any = any || hits > 0;
      });
      if (!q) saved = null;
      $('#noHits').hidden = any || !q;
    });
  }

  // ---------------- panels ----------------
  const compact = () => !matchMedia('(min-width: 840px)').matches;

  function togglePanel(name) {
    const next = openPanel === name ? null : name;
    document.querySelectorAll('dialog.panel[open]').forEach((d) => d.close());
    document.querySelectorAll('.dock-tab').forEach((t) => t.setAttribute('aria-expanded', t.dataset.panel === next));
    openPanel = next;
    if (next) { $('#panel-' + next).show(); refresh(); }
    document.body.classList.toggle('panel-open', !!next);
  }

  function buildPanels() {
    document.querySelectorAll('.dock-tab').forEach((t) => t.addEventListener('click', () => togglePanel(t.dataset.panel)));
    document.querySelectorAll('dialog.panel [data-close]').forEach((b) => b.addEventListener('click', () => togglePanel(null)));
    // On phones the panel covers the picture: tapping the picture puts it away.
    canvas.addEventListener('pointerdown', () => { if (openPanel && compact()) togglePanel(null); });
    $('#wander').addEventListener('click', () => setWander(!system.autoVJ));
    $('#more').addEventListener('click', (e) => {
      const b = e.target.closest('[data-act]');
      if (!b) return;
      $('#more').hidePopover();
      actions[b.dataset.act]();
    });
    $('#begin').addEventListener('click', () => { mn.done('intro'); showCaption(); });
  }

  function setWander(on) {
    system.autoVJ = on;
    vjTimer = 0;
    const b = $('#wander');
    b.setAttribute('aria-pressed', on);
    b.innerHTML = on ? '<i data-icon="pause"></i><span>Wandering</span>' : '<i data-icon="play"></i><span>Wander</span>';
    if (on && !mn.store.get('wander-taught')) {
      toast(`A new view every ${system.autoVJInterval} s`);
      mn.store.set('wander-taught', 1);
    }
  }

  // ---------------- caption ----------------
  function showCaption() {
    $('#captionName').textContent = CHAMBERS[chamber].name;
    $('#captionLine').textContent = CHAMBERS[chamber].line;
    $('#caption').classList.add('show');
    captionTimer = 6;
  }

  // ---------------- capture ----------------
  function stamp() { return new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19); }

  function download(blob, file) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = file;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
  }

  function afterRender() {
    if (!snapRequested) return;
    snapRequested = false;
    canvas.toBlob((b) => b && download(b, `kaleidoscope-${CHAMBERS[chamber].slug}-${stamp()}.png`), 'image/png');
    toast('Image saved to downloads');
  }

  // Re-render the live state at a higher resolution: the echo buffer is
  // resampled up, then a burst of frames regrows real detail before capture.
  async function exportHiRes(longEdge) {
    const max = Math.min(Renderer.maxSize, 16384);
    const aspect = canvas.width / canvas.height;
    let w = aspect >= 1 ? longEdge : Math.round(longEdge * aspect);
    let h = aspect >= 1 ? Math.round(longEdge / aspect) : longEdge;
    const k = Math.min(1, max / Math.max(w, h));
    w = Math.round(w * k); h = Math.round(h * k);
    toast(`Rendering ${w} × ${h}…`);
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
    UI.paused = true;
    const cw = canvas.width, ch = canvas.height;
    try {
      canvas.width = w; canvas.height = h;
      Renderer.resize(w, h);
      for (let i = 0; i < 60; i++) Main.step(1 / 60);
      const blob = await new Promise((r) => canvas.toBlob(r, 'image/png'));
      if (blob) download(blob, `kaleidoscope-${CHAMBERS[chamber].slug}-${w}x${h}-${stamp()}.png`);
      toast(blob ? `${w} × ${h} image saved to downloads` : 'Not enough graphics memory · try a smaller size');
    } catch (e) {
      toast('Not enough graphics memory · try a smaller size');
    } finally {
      canvas.width = cw; canvas.height = ch;
      Main.resize(true);
      UI.paused = false;
    }
  }

  function setRecording(on) {
    $('#recLabel').textContent = on ? 'Stop recording' : 'Record video';
    $('#recDot').hidden = !on;
  }

  function toggleRecord() {
    if (recorder) { recorder.stop(); return; }
    if (!window.MediaRecorder || !canvas.captureStream) { toast("This browser can't record video · try Chrome"); return; }
    const types = ['video/mp4;codecs=avc1.640033', 'video/mp4', 'video/webm;codecs=vp9', 'video/webm'];
    const mime = types.find((t) => MediaRecorder.isTypeSupported(t));
    const chunks = [];
    const r = new MediaRecorder(canvas.captureStream(60), { mimeType: mime, videoBitsPerSecond: 40e6 });
    r.ondataavailable = (e) => e.data.size && chunks.push(e.data);
    r.onstop = () => {
      download(new Blob(chunks, { type: mime }), `kaleidoscope-${CHAMBERS[chamber].slug}-${stamp()}.${mime.includes('mp4') ? 'mp4' : 'webm'}`);
      recorder = null;
      setRecording(false);
      toast('Video saved to downloads');
    };
    r.start(250);
    recorder = r;
    setRecording(true);
    toast('Recording · press R to stop');
  }

  const actions = {
    snapshot: () => { snapRequested = true; },
    export4k: () => exportHiRes(3840),
    record: toggleRecord,
    copyLink: () => {
      writeHash();
      navigator.clipboard.writeText(location.href).then(() => toast('Link copied · it opens this exact view'), () => toast(location.href));
    },
    fullscreen: () => (document.fullscreenElement ? document.exitFullscreen() : document.documentElement.requestFullscreen?.()),
    hide: () => setHidden(true),
    all: () => togglePanel('all'),
    keys: () => mn.sheet('keys'),
  };

  function setHidden(h) {
    document.body.classList.toggle('clean', h);
    if (h) { togglePanel(null); toast('Controls hidden · press H to bring them back'); }
  }

  // ---------------- keyboard ----------------
  function cycle(key, dir = 1) {
    const opts = SCHEMA[key][1];
    setParam(key, opts[(opts.indexOf(params[key]) + dir + opts.length) % opts.length]);
    toast(`${SCHEMA[key][5]} · ${name(params[key])}`);
  }

  function onKey(e) {
    if (e.target.closest && e.target.closest('input, select, textarea')) return;
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    const k = e.key;
    if (k >= '1' && k <= '9') { if (CHAMBERS[+k - 1]) enterChamber(+k - 1); return; }
    switch (k) {
      case ' ': turn(); break;
      case 'ArrowRight': enterChamber(chamber + 1); break;
      case 'ArrowLeft': enterChamber(chamber - 1); break;
      case 'ArrowUp': setParam('symmetry', Math.min(16, params.symmetry + 1)); break;
      case 'ArrowDown': setParam('symmetry', Math.max(2, params.symmetry - 1)); break;
      case 'w': case 'W': setWander(!system.autoVJ); break;
      case 'b': case 'B': cycle('brush'); break;
      case 'p': cycle('palette'); break;
      case 'P': cycle('palette', -1); break;
      case 'c': case 'C': Inputs.setCam(!Inputs.state.cam).then(refresh, (err) => toast(err.message)); break;
      case 't': case 'T': Inputs.setHands(!Inputs.state.hands).then(refresh, (err) => toast(err.message)); break;
      case 'x': case 'X': case 'Backspace': clearGlass(); break;
      case 'i': case 'I': actions.snapshot(); break;
      case 'r': case 'R': toggleRecord(); break;
      case 'l': case 'L': actions.copyLink(); break;
      case 'e': case 'E': togglePanel('all'); break;
      case 'h': case 'H': setHidden(!document.body.classList.contains('clean')); break;
      case 'f': case 'F': actions.fullscreen(); break;
      case '?': case '/': mn.sheet('keys'); break;
      case 'Escape': if (openPanel) togglePanel(null); else return; break;
      default: return;
    }
    e.preventDefault();
  }

  // ---------------- lifecycle ----------------
  function toast(msg) { mn.toast(msg); }

  function refresh() { for (const f of refreshers) f(); }
  function refreshSoon() { if (refreshTimer <= 0) refreshTimer = 0.15; }

  function wake() {
    idleTimer = 5;
    document.body.classList.remove('idle');
  }

  function init(c) {
    canvas = c;
    const fromHash = readHash();
    if (fromHash) {
      chamber = fromHash.chamber;
      morphTo({ ...chamberTarget(chamber), ...fromHash.params }, true);
    } else {
      if (matchMedia('(prefers-reduced-motion: reduce)').matches) params.pace = 0.4;
      enterChamber(0, { instant: true, quiet: true });
    }
    buildChambers();
    buildLight();
    buildMood();
    buildAll();
    renderShapeDial();
    buildPanels();
    refresh();
    window.addEventListener('keydown', (e) => { wake(); onKey(e); });
    window.addEventListener('pointermove', wake, { passive: true });
    window.addEventListener('pointerdown', wake, { passive: true });
    wake();
    if (mn.store.get('done:intro') || document.querySelector('[data-once="intro"]')?.hidden) showCaption();
  }

  function update(dt) {
    updateMorph(dt);
    if (system.autoVJ) {
      vjTimer += dt;
      if (vjTimer >= system.autoVJInterval) {
        vjTimer = 0;
        if (Math.random() < 0.5) enterChamber(chamber + 1); else turn();
      }
    }
  }

  function afterFrame(dt) {
    if (hashTimer > 0 && (hashTimer -= dt) <= 0) writeHash();
    if (refreshTimer > 0 && (refreshTimer -= dt) <= 0) refresh();
    if (captionTimer > 0 && (captionTimer -= dt) <= 0) $('#caption').classList.remove('show');
    if (idleTimer > 0 && (idleTimer -= dt) <= 0 && !openPanel) document.body.classList.add('idle');
  }

  return {
    init, update, afterFrame, afterRender, toast, refresh, pinch, note, setNormalized, stats,
    paused: false,
  };
})();
