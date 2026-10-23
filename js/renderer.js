// WebGL2 pipeline:
//   [raymarch] → feedback(prev → next) → ink(max-blended into next) → scene(display fold, palette)
//   → bloom down/up chain → final(chroma, bloom, tonemap, vignette, grain) → canvas

const Renderer = (() => {
  let gl, canvas;
  let progs = {};
  let vao, inkVao, inkBuf;
  let hdrFormat;                    // { internal, type }
  let fb = [null, null], cur = 0;   // feedback ping-pong
  let scene, rm, veil, backup, bloom = [];
  let veilAmt = 0;
  let lumCur, lumAd = [null, null], lumIdx = 0, lumReset = true;                  // crossfade from the frame before a structural change
  let camTex, camAspect = 16 / 9, camReady = false;
  let W = 0, H = 0;                 // render resolution
  let frameNo = 0;
  const MAX_SEG = 16384;
  const inkData = new Float32Array(MAX_SEG * 8);
  const colorMat = new Float32Array(9);
  const rmRot = new Float32Array(9);

  function compile(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(s);
      console.error(log, src.split('\n').map((l, i) => `${i + 1}: ${l}`).join('\n'));
      throw new Error('Shader compile failed: ' + log);
    }
    return s;
  }

  function program(vs, fs) {
    const p = gl.createProgram();
    gl.attachShader(p, compile(gl.VERTEX_SHADER, vs));
    gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error('Link failed: ' + gl.getProgramInfoLog(p));
    const loc = {};
    const n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < n; i++) {
      const name = gl.getActiveUniform(p, i).name;
      loc[name] = gl.getUniformLocation(p, name);
    }
    return { p, loc };
  }

  function target(w, h, hdr, wrap, mips) {
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    if (hdr) gl.texImage2D(gl.TEXTURE_2D, 0, hdrFormat.internal, w, h, 0, gl.RGBA, hdrFormat.type, null);
    else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mips ? gl.LINEAR_MIPMAP_LINEAR : gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, wrap);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, wrap);
    const fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    gl.clearColor(0, 0, 0, 1);
    gl.clear(gl.COLOR_BUFFER_BIT);
    return { tex, fbo, w, h };
  }

  function free(t) {
    if (!t) return;
    gl.deleteTexture(t.tex);
    gl.deleteFramebuffer(t.fbo);
  }

  function init(c) {
    canvas = c;
    gl = canvas.getContext('webgl2', { antialias: false, alpha: false, depth: false, stencil: false, powerPreference: 'high-performance', desynchronized: true });
    if (!gl) throw new Error('WebGL2 is not available in this browser.');
    hdrFormat = gl.getExtension('EXT_color_buffer_float')
      ? { internal: gl.RGBA16F, type: gl.HALF_FLOAT }
      : { internal: gl.RGBA8, type: gl.UNSIGNED_BYTE };

    const S = SHADERS;
    progs.feedback = program(S.FULLSCREEN_VS, S.FEEDBACK_FS);
    progs.raymarch = program(S.FULLSCREEN_VS, S.RAYMARCH_FS);
    progs.scene = program(S.FULLSCREEN_VS, S.SCENE_FS);
    progs.down = program(S.FULLSCREEN_VS, S.DOWN_FS);
    progs.up = program(S.FULLSCREEN_VS, S.UP_FS);
    progs.final = program(S.FULLSCREEN_VS, S.FINAL_FS);
    progs.copy = program(S.FULLSCREEN_VS, S.COPY_FS);
    progs.lum = program(S.FULLSCREEN_VS, S.LUM_FS);
    progs.adapt = program(S.FULLSCREEN_VS, S.ADAPT_FS);
    progs.ink = program(S.INK_VS, S.INK_FS);

    vao = gl.createVertexArray();

    inkVao = gl.createVertexArray();
    gl.bindVertexArray(inkVao);
    inkBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, inkBuf);
    gl.bufferData(gl.ARRAY_BUFFER, inkData.byteLength, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 32, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 32, 16);
    gl.bindVertexArray(null);

    camTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, camTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(4));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.MIRRORED_REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.MIRRORED_REPEAT);

    canvas.addEventListener('webglcontextlost', (e) => { e.preventDefault(); UI.toast('GPU context lost — reload the page'); });
    return gl;
  }

  function copyInto(src, dst) {
    gl.bindFramebuffer(gl.FRAMEBUFFER, dst.fbo);
    gl.viewport(0, 0, dst.w, dst.h);
    use(progs.copy);
    tex(0, src.tex);
    u1i(progs.copy, 'uSrc', 0);
    u2f(progs.copy, 'uOut', dst.w, dst.h);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  // Reallocate render targets, resampling the feedback image so nothing resets.
  function resize(w, h) {
    w = Math.max(16, Math.round(w)); h = Math.max(16, Math.round(h));
    if (w === W && h === H) return;
    gl.bindVertexArray(vao);
    const old = fb[cur];
    const nfb = [target(w, h, true, gl.MIRRORED_REPEAT), target(w, h, true, gl.MIRRORED_REPEAT)];
    if (old) copyInto(old, nfb[0]);
    free(fb[0]); free(fb[1]);
    fb = nfb; cur = 0;
    free(scene); scene = target(w, h, true, gl.CLAMP_TO_EDGE, true);
    if (!lumCur) { lumCur = target(1, 1, true, gl.CLAMP_TO_EDGE); lumAd = [target(1, 1, true, gl.CLAMP_TO_EDGE), target(1, 1, true, gl.CLAMP_TO_EDGE)]; }
    free(veil); veil = target(w, h, true, gl.CLAMP_TO_EDGE); veilAmt = 0;
    free(backup); backup = null;
    free(rm); rm = target(Math.max(16, w >> 1), Math.max(16, h >> 1), true, gl.MIRRORED_REPEAT);
    bloom.forEach(free); bloom = [];
    let bw = w, bh = h;
    for (let i = 0; i < 6 && bw > 8 && bh > 8; i++) {
      bw = Math.max(1, bw >> 1); bh = Math.max(1, bh >> 1);
      bloom.push(target(bw, bh, true, gl.CLAMP_TO_EDGE));
    }
    W = w; H = h;
  }

  // Clearing keeps a copy so it can be undone.
  function clear() {
    if (!backup) backup = target(W, H, true, gl.MIRRORED_REPEAT);
    gl.bindVertexArray(vao);
    copyInto(fb[cur], backup);
    for (const t of fb) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.clearColor(0, 0, 0, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
    }
  }

  function restore() {
    if (!backup) return;
    gl.bindVertexArray(vao);
    copyInto(backup, fb[cur]);
  }

  // Freeze the current picture and fade it out over the next ~2 s, so chamber
  // and shape changes dissolve instead of cutting.
  function startVeil() {
    if (!scene) return;
    gl.bindVertexArray(vao);
    copyInto(scene, veil);
    veilAmt = 1;
  }
  function fadeVeil(dt) { veilAmt = Math.max(0, veilAmt - dt / 2.2); }

  // ---- uniform helpers (cached locations; unknown names are skipped) ----
  function use(pr) { gl.useProgram(pr.p); }
  function tex(unit, t) { gl.activeTexture(gl.TEXTURE0 + unit); gl.bindTexture(gl.TEXTURE_2D, t); }
  function u1f(pr, n, v) { const l = pr.loc[n]; if (l) gl.uniform1f(l, v); }
  function u1i(pr, n, v) { const l = pr.loc[n]; if (l) gl.uniform1i(l, v); }
  function u2f(pr, n, a, b) { const l = pr.loc[n]; if (l) gl.uniform2f(l, a, b); }
  function u3f(pr, n, a, b, c) { const l = pr.loc[n]; if (l) gl.uniform3f(l, a, b, c); }
  function u4f(pr, n, a, b, c, d) { const l = pr.loc[n]; if (l) gl.uniform4f(l, a, b, c, d); }
  function u3v(pr, n, v) { const l = pr.loc[n]; if (l) gl.uniform3fv(l, v); }
  function m3(pr, n, m) { const l = pr.loc[n]; if (l) gl.uniformMatrix3fv(l, false, m); }

  function setCommon(pr, w, h) {
    const f = fold;
    u2f(pr, 'uRes', w, h);
    u1i(pr, 'uMode', f.mode);
    u1f(pr, 'uSym', f.sym);
    u1i(pr, 'uMirror', f.mirror ? 1 : 0);
    u1f(pr, 'uSpin', f.spin);
    u2f(pr, 'uCenter', f.cx, f.cy);
    u1f(pr, 'uViewZoom', f.vz);
    u4f(pr, 'uWarp', f.warpAmt, f.warpScale, f.warpT, 0);
    u1i(pr, 'uKifsIters', f.kIters);
    u2f(pr, 'uKifs', f.kScale, f.kAngle);
    u2f(pr, 'uKifsOff', f.kOffX, f.kOffY);
    u4f(pr, 'uHyp', f.hypC, f.hypR, f.hypAx, f.hypAy);
    u2f(pr, 'uDroste', f.drL, f.drPhase);
    u1f(pr, 'uTile', f.tile);
    u3v(pr, 'uPalA', Palette.a);
    u3v(pr, 'uPalB', Palette.b);
    u3v(pr, 'uPalC', Palette.c);
    u3v(pr, 'uPalD', Palette.d);
  }

  // Decay × saturation × hue rotation about the gray axis, as one matrix.
  function buildColorMat(decay, sat, hue) {
    const c = Math.cos(hue), s = Math.sin(hue), k = (1 - c) / 3, q = Math.sqrt(1 / 3) * s;
    const h = [c + k, k - q, k + q, k + q, c + k, k - q, k - q, k + q, c + k];
    const lr = 0.299 * (1 - sat), lg = 0.587 * (1 - sat), lb = 0.114 * (1 - sat);
    const S = [lr + sat, lr, lr, lg, lg + sat, lg, lb, lb, lb + sat]; // column-major
    // colorMat = decay * S * H (column-major)
    for (let col = 0; col < 3; col++) {
      for (let row = 0; row < 3; row++) {
        let v = 0;
        for (let i = 0; i < 3; i++) v += S[i * 3 + row] * h[col * 3 + i];
        colorMat[col * 3 + row] = v * decay;
      }
    }
  }

  function buildRmRot(a) {
    // rotation about normalized (1, 1, 0) axis
    const x = Math.SQRT1_2, y = Math.SQRT1_2, z = 0;
    const c = Math.cos(a), s = Math.sin(a), t = 1 - c;
    rmRot.set([t * x * x + c, t * x * y + s * z, t * x * z - s * y,
               t * x * y - s * z, t * y * y + c, t * y * z + s * x,
               t * x * z + s * y, t * y * z - s * x, t * z * z + c]);
  }

  function uploadCamera(video) {
    if (!video || video.readyState < 2) return;
    gl.bindTexture(gl.TEXTURE_2D, camTex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, video);
    camAspect = video.videoWidth / Math.max(1, video.videoHeight);
    camReady = true;
  }

  // f: per-frame values computed in main (dt-scaled feedback params, ink count, ...)
  function render(f) {
    const P = params;
    frameNo++;
    gl.bindVertexArray(vao);
    gl.disable(gl.BLEND);

    // 1. raymarched fractal source
    const useRm = P.gen === 'fractal3d';
    if (useRm) {
      const pr = progs.raymarch;
      gl.bindFramebuffer(gl.FRAMEBUFFER, rm.fbo);
      gl.viewport(0, 0, rm.w, rm.h);
      use(pr);
      setCommon(pr, rm.w, rm.h);
      buildRmRot(0.35 + 0.25 * Math.sin(f.rmT * 0.07));
      m3(pr, 'uRmRot', rmRot);
      u1f(pr, 'uRmScale', P.rmScale);
      u1f(pr, 'uRmZ', f.rmZ);
      u1f(pr, 'uRmT', f.rmT);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    // 2. feedback
    const src = fb[cur], dst = fb[1 - cur];
    {
      const pr = progs.feedback;
      gl.bindFramebuffer(gl.FRAMEBUFFER, dst.fbo);
      gl.viewport(0, 0, W, H);
      use(pr);
      setCommon(pr, W, H);
      tex(0, src.tex); u1i(pr, 'uPrev', 0);
      tex(1, camTex); u1i(pr, 'uCam', 1);
      tex(2, rm.tex); u1i(pr, 'uRM', 2);
      u1i(pr, 'uFoldLoop', P.foldInLoop ? 1 : 0);
      buildColorMat(f.decay, P.fbSat, f.hue);
      m3(pr, 'uColorMat', colorMat);
      u1f(pr, 'uFbRot', f.fbRot);
      u1f(pr, 'uFbZoom', f.fbZoom);
      u1f(pr, 'uFbSwirl', f.fbSwirl);
      u1f(pr, 'uFbWarp', f.fbWarp);
      u1f(pr, 'uFbWarpT', f.fbWarpT);
      u1f(pr, 'uSharpen', P.fbSharpen * Math.min(f.n, 2));
      u1i(pr, 'uGen', GENERATORS.indexOf(P.gen));
      u1f(pr, 'uGenK', P.genMix * 2);
      u1f(pr, 'uGenScale', P.genScale);
      // A hyperbolic chamber shows one small triangle everywhere; enlarge the
      // pattern inside it so a single passing line can't light the whole screen.
      u1f(pr, 'uGenFold', fold.mode === 3 ? 0.8 / (fold.hypC - fold.hypR) : 1);
      u1f(pr, 'uGenT', f.genT);
      u1f(pr, 'uGenRadius', P.genRadius);
      u1i(pr, 'uCamOn', f.camOn && camReady ? 1 : 0);
      u1f(pr, 'uCamK', P.camMix);
      u1f(pr, 'uCamZoom', P.camZoom);
      u1f(pr, 'uCamAspect', camAspect);
      u1f(pr, 'uCamColorize', P.camColorize);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    // 3. ink
    if (f.inkCount > 0) {
      const pr = progs.ink;
      use(pr);
      gl.enable(gl.BLEND);
      gl.blendEquation(gl.MAX);
      gl.bindVertexArray(inkVao);
      gl.bindBuffer(gl.ARRAY_BUFFER, inkBuf);
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, inkData, 0, f.inkCount * 8);
      const copies = f.inkCopies;
      gl.vertexAttribDivisor(0, copies);
      gl.vertexAttribDivisor(1, copies);
      u2f(pr, 'uRes', W, H);
      u1i(pr, 'uCopies', copies);
      u1i(pr, 'uInkMirror', fold.mirror ? 1 : 0);
      u1f(pr, 'uSym', fold.sym);
      u1f(pr, 'uSpin', fold.spin);
      u2f(pr, 'uCenter', fold.cx, fold.cy);
      u1f(pr, 'uGlow', P.inkGlow);
      gl.drawArraysInstanced(gl.TRIANGLE_STRIP, 0, 4, f.inkCount * copies);
      gl.blendEquation(gl.FUNC_ADD);
      gl.disable(gl.BLEND);
      gl.bindVertexArray(vao);
    }
    cur = 1 - cur;

    // 4. scene (display fold + palette remap)
    {
      const pr = progs.scene;
      gl.bindFramebuffer(gl.FRAMEBUFFER, scene.fbo);
      gl.viewport(0, 0, W, H);
      use(pr);
      setCommon(pr, W, H);
      tex(0, fb[cur].tex); u1i(pr, 'uFB', 0);
      u1i(pr, 'uFoldLoop', P.foldInLoop ? 1 : 0);
      u1f(pr, 'uFisheye', P.fisheye);
      u1f(pr, 'uPalMap', P.paletteMap);
      u1f(pr, 'uPalShift', f.palPhase);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    // 4b. flash guard: scene luminance → 1×1, eased adaptation
    if (system.flashGuard) {
      gl.bindTexture(gl.TEXTURE_2D, scene.tex);
      gl.generateMipmap(gl.TEXTURE_2D);
      let pr = progs.lum;
      gl.bindFramebuffer(gl.FRAMEBUFFER, lumCur.fbo);
      gl.viewport(0, 0, 1, 1);
      use(pr);
      tex(0, scene.tex); u1i(pr, 'uSrc', 0);
      u1f(pr, 'uLod', Math.max(0, Math.log2(Math.max(W, H) / 16)));
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      pr = progs.adapt;
      gl.bindFramebuffer(gl.FRAMEBUFFER, lumAd[1 - lumIdx].fbo);
      use(pr);
      tex(0, lumAd[lumIdx].tex); u1i(pr, 'uPrev', 0);
      tex(1, lumCur.tex); u1i(pr, 'uCur', 1);
      u1f(pr, 'uK', lumReset ? 1 : 1 - Math.exp(-f.dt / 0.5));
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      lumIdx = 1 - lumIdx;
      lumReset = false;
    } else {
      lumReset = true;
    }

    // 5. bloom
    const doBloom = P.bloom > 0 && bloom.length > 0;
    if (doBloom) {
      let pr = progs.down;
      use(pr);
      u1f(pr, 'uThreshold', P.bloomThreshold);
      let s = scene;
      for (let i = 0; i < bloom.length; i++) {
        const d = bloom[i];
        gl.bindFramebuffer(gl.FRAMEBUFFER, d.fbo);
        gl.viewport(0, 0, d.w, d.h);
        tex(0, s.tex); u1i(pr, 'uSrc', 0);
        u2f(pr, 'uTexel', 1 / s.w, 1 / s.h);
        u2f(pr, 'uOut', d.w, d.h);
        u1i(pr, 'uPrefilter', i === 0 ? 1 : 0);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
        s = d;
      }
      pr = progs.up;
      use(pr);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.ONE, gl.ONE);
      for (let i = bloom.length - 1; i > 0; i--) {
        const sm = bloom[i], d = bloom[i - 1];
        gl.bindFramebuffer(gl.FRAMEBUFFER, d.fbo);
        gl.viewport(0, 0, d.w, d.h);
        tex(0, sm.tex); u1i(pr, 'uSrc', 0);
        u2f(pr, 'uTexel', 0.5 / sm.w, 0.5 / sm.h);
        u2f(pr, 'uOut', d.w, d.h);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
      gl.disable(gl.BLEND);
    }

    // 6. final to canvas
    {
      const pr = progs.final;
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, canvas.width, canvas.height);
      use(pr);
      tex(0, scene.tex); u1i(pr, 'uScene', 0);
      tex(1, doBloom ? bloom[0].tex : scene.tex); u1i(pr, 'uBloomTex', 1);
      tex(2, veil.tex); u1i(pr, 'uVeilTex', 2);
      tex(3, lumCur.tex); u1i(pr, 'uLumCur', 3);
      tex(4, lumAd[lumIdx].tex); u1i(pr, 'uLumAdapt', 4);
      u1i(pr, 'uGuard', system.flashGuard ? 1 : 0);
      u1f(pr, 'uVeil', veilAmt * veilAmt * (3 - 2 * veilAmt));
      u2f(pr, 'uOut', canvas.width, canvas.height);
      u1f(pr, 'uBloom', doBloom ? P.bloom * 0.22 : 0);
      u1f(pr, 'uChroma', P.chroma);
      u1f(pr, 'uGrain', P.grain);
      u1f(pr, 'uVignette', P.vignette);
      u1f(pr, 'uExposure', P.exposure);
      const l = pr.loc.uFrame; if (l) gl.uniform1ui(l, frameNo);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
  }

  return {
    init, resize, clear, restore, startVeil, fadeVeil, render, uploadCamera, inkData, MAX_SEG,
    get size() { return [W, H]; },
    get gl() { return gl; },
    get maxSize() { return gl.getParameter(gl.MAX_TEXTURE_SIZE); },
  };
})();
