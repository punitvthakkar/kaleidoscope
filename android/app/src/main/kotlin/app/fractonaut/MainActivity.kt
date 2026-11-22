package app.fractonaut

import android.Manifest
import android.app.Activity
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.media.MediaCodecInfo.CodecProfileLevel
import android.media.MediaCodecList
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.util.Log
import android.view.Display
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.webkit.ConsoleMessage
import android.webkit.JavascriptInterface
import android.webkit.RenderProcessGoneDetail
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.window.OnBackInvokedDispatcher
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.io.File
import kotlin.math.hypot

// One activity: the picture (a SurfaceView the native render thread draws into) under
// the interface (a transparent WebView serving the built page from the APK's assets).
// Everything here runs on the main thread; native calls in through Host, which hops here.
class MainActivity : Activity() {
    private val main = Handler(Looper.getMainLooper())
    private lateinit var root: FrameLayout
    private lateinit var picture: SurfaceView
    private var web: WebView? = null
    private var ready = false                  // the page sent `ready`
    private var script: String? = null         // fx.script, run once the page is ready
    private var fullscreen = true
    private var started = false                // between onStart and onStop
    private val asked = HashMap<Int, Int>()    // permission request code -> message id
    private var nextAsk = 1
    private val touch = Touch()
    private var stats = false                  // fx.stats
    private lateinit var vision: Vision
    private val waiting = HashMap<Int, (Boolean) -> Unit>()   // Kotlin's own permission requests

    fun ui(fn: () -> Unit) { main.post(fn) }

    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        Host.app = this
        stats = packageName.endsWith(".dev")   // the dev app logs its stats (tag fx) once a second
        window.setDecorFitsSystemWindows(false)
        window.attributes = window.attributes.apply {
            layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
            preferredDisplayModeId = fastestMode(display!!)?.modeId ?: 0
        }
        window.decorView.setBackgroundColor(Color.BLACK)
        copyAssets()
        if (!Host.started) { startNative(); Host.started = true }

        root = FrameLayout(this)
        picture = SurfaceView(this)
        picture.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(h: SurfaceHolder) = Host.surfaceCreated(h.surface)
            override fun surfaceChanged(h: SurfaceHolder, format: Int, w: Int, hh: Int) = Host.surfaceChanged(w, hh)
            override fun surfaceDestroyed(h: SurfaceHolder) = Host.surfaceDestroyed()
        })
        root.addView(picture, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)
        vision = Vision(this, HANDS_GPU) { stats }
        makeWeb()
        setFullscreen(true)
        onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT) { moveTaskToBack(true) }
        debugExtras(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        debugExtras(intent)
    }

    override fun onStart() {
        super.onStart()
        started = true
        web?.onResume()
        Host.foreground(true)
        vision.foreground(true)
        main.post(thermalTick)
    }

    override fun onStop() {
        started = false
        main.removeCallbacks(thermalTick)
        vision.foreground(false)
        Host.foreground(false)
        web?.onPause()
        super.onStop()
    }

    // ---------------- device facts (the Kotlin half of Caps) ----------------
    private fun fastestMode(d: Display): Display.Mode? {
        val cur = d.mode
        return d.supportedModes.filter { it.physicalWidth == cur.physicalWidth && it.physicalHeight == cur.physicalHeight }
            .maxByOrNull { it.refreshRate }
    }

    private fun startNative() {
        val d = display!!
        val mode = fastestMode(d) ?: d.mode
        @Suppress("DEPRECATION") val hdr = d.hdrCapabilities
        val hlg = hdr?.supportedHdrTypes?.contains(Display.HdrCapabilities.HDR_TYPE_HLG) == true
        var hevc = ""; var hevc10 = false; var avc = ""
        for (c in MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos) {
            if (!c.isEncoder || !c.isHardwareAccelerated) continue
            for (t in c.supportedTypes) {
                if (t.equals("video/hevc", true) && hevc.isEmpty()) {
                    hevc = c.name
                    hevc10 = c.getCapabilitiesForType(t).profileLevels.any { it.profile == CodecProfileLevel.HEVCProfileMain10 }
                } else if (t.equals("video/avc", true) && avc.isEmpty()) avc = c.name
            }
        }
        Host.start(
            Build.VERSION.SDK_INT, minOf(mode.physicalWidth, mode.physicalHeight), maxOf(mode.physicalWidth, mode.physicalHeight),
            mode.refreshRate, resources.displayMetrics.density, hlg, hdr?.desiredMaxLuminance ?: 0f,
            hevc, hevc10, avc, filesDir.absolutePath, if (HANDS_GPU) 1 else 0,
        )
    }

    // The renderer reads plain files: assets shaders/ and chambers.txt, fresh every start.
    private fun copyAssets() {
        fun copy(path: String) {
            val kids = assets.list(path) ?: emptyArray()
            if (kids.isEmpty()) {
                val out = File(filesDir, path)
                out.parentFile?.mkdirs()
                try { assets.open(path).use { i -> out.outputStream().use { i.copyTo(it) } } } catch (_: java.io.FileNotFoundException) {}
            } else kids.forEach { copy("$path/$it") }
        }
        File(filesDir, "shaders").deleteRecursively()
        File(filesDir, "chambers.txt").delete()
        copy("shaders")
        copy("chambers.txt")
    }

    private val thermalTick = object : Runnable {
        override fun run() {
            if (!started) return
            val pm = getSystemService(PowerManager::class.java)
            Host.thermal(pm.getThermalHeadroom(0), pm.currentThermalStatus)
            main.postDelayed(this, 1000)
        }
    }

    private fun debugExtras(i: Intent?) {
        val ex = i?.extras ?: return
        for (k in ex.keySet().filter { it.startsWith("fx.") }) {
            @Suppress("DEPRECATION") val v = ex.get(k)?.toString() ?: continue
            if (k == "fx.script") { script = v; if (ready) runScript() }
            if (k == "fx.stats") stats = v == "true"
            if (k == "fx.hands") { script = "Inputs.setHands($v).catch((e) => console.warn(String(e)));" + (script ?: ""); if (ready) runScript() }
            Host.debug(k, v)
        }
    }

    private fun runScript() {
        val s = script ?: return
        script = null
        web?.evaluateJavascript("$s\n;0") { r -> Log.i("fx", "script done: $r") }
    }

    // ---------------- the interface ----------------
    private fun makeWeb() {
        val w = WebView(this)
        w.setBackgroundColor(Color.TRANSPARENT)
        w.isVerticalScrollBarEnabled = false
        w.isHorizontalScrollBarEnabled = false
        w.overScrollMode = WebView.OVER_SCROLL_NEVER
        w.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            allowFileAccess = false
            allowContentAccess = false
            setSupportZoom(false)
            builtInZoomControls = false
        }
        w.addJavascriptInterface(Bridge(), "fxAndroid")
        w.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(v: WebView, r: WebResourceRequest): WebResourceResponse = serve(r.url)
            override fun shouldOverrideUrlLoading(v: WebView, r: WebResourceRequest) = r.url.host != HOST
            override fun doUpdateVisitedHistory(v: WebView, url: String, reload: Boolean) {
                val u = Uri.parse(url)
                if (u.host == HOST) getSharedPreferences("fx", MODE_PRIVATE).edit().putString("view", u.fragment ?: "").apply()
            }
            override fun onRenderProcessGone(v: WebView, d: RenderProcessGoneDetail): Boolean {
                Log.w("fx", "interface process ended (crash=${d.didCrash()}), reloading")
                root.removeView(v)
                v.destroy()
                if (web === v) { web = null; ready = false; makeWeb() }
                return true
            }
        }
        w.webChromeClient = object : WebChromeClient() {
            override fun onConsoleMessage(m: ConsoleMessage): Boolean {
                Log.i("fx", "js ${m.messageLevel()} ${m.message()} (${m.sourceId().substringAfterLast('/')}:${m.lineNumber()})")
                return true
            }
        }
        root.addView(w, FrameLayout.LayoutParams(-1, -1))
        web = w
        val hash = getSharedPreferences("fx", MODE_PRIVATE).getString("view", "") ?: ""
        w.loadUrl("https://$HOST/web/index.html" + if (hash.isNotEmpty()) "#$hash" else "")
    }

    private fun serve(url: Uri): WebResourceResponse {
        val path = url.path ?: ""
        if (url.host == HOST && path.startsWith("/web/") && !path.contains("..")) {
            try {
                val type = MIME[path.substringAfterLast('.', "").lowercase()] ?: "application/octet-stream"
                return WebResourceResponse(type, if (type.startsWith("text/")) "utf-8" else null, assets.open(path.substring(1)))
                    .apply { responseHeaders = mapOf("Cache-Control" to "no-cache") }
            } catch (_: java.io.IOException) {}
        }
        return WebResourceResponse("text/plain", "utf-8", 404, "Not Found", emptyMap(), ByteArrayInputStream(ByteArray(0)))
    }

    // shim.js posts through the adapter android/tools/make_page.mjs adds to the page.
    // Both calls arrive on the WebView's bridge thread and hop to the main thread.
    private inner class Bridge {
        @JavascriptInterface fun post(json: String) { main.post { batch(json) } }
        @JavascriptInterface fun down(x: Float, y: Float, ui: Boolean) {
            val d = resources.displayMetrics.density
            main.post { touch.verdict(x * d, y * d, ui) }
        }
    }

    private fun batch(json: String) {
        val arr = try { JSONArray(json) } catch (e: Exception) { Log.w("fx", "bad batch: $e"); return }
        for (i in 0 until arr.length()) {
            val m = arr.optJSONObject(i) ?: continue
            when (val type = m.optString("type")) {
                "set" -> {
                    m.optJSONObject("params")?.let { p ->
                        for (k in p.keys()) when (val v = p.get(k)) {
                            is String -> Host.setParam(k, 0.0, v)
                            is Boolean -> Host.setParam(k, if (v) 1.0 else 0.0, null)
                            is Number -> Host.setParam(k, v.toDouble(), null)
                        }
                    }
                    m.optJSONObject("system")?.let { s ->
                        for (k in s.keys()) when (val v = s.get(k)) {
                            is Boolean -> Host.setSystem(k, if (v) 1.0 else 0.0)
                            is Number -> Host.setSystem(k, v.toDouble())
                        }
                    }
                }
                "camera" -> vision.camera(m.optBoolean("on"), m.optInt("id"))
                "hands" -> vision.hands(m.optBoolean("on"), m.optInt("id"))
                "cameraDevice" -> vision.device(m.optString("device"))
                "fullscreen" -> {
                    setFullscreen(m.optBoolean("on"))
                    pageEvent("fullscreen", JSONObject().put("on", fullscreen).toString())
                }
                else -> {
                    val on = if (type == "handsOptions") m.optBoolean("twoHands") else m.optBoolean("on")
                    val num = when (type) {
                        "veil" -> m.optDouble("seconds", 0.0)
                        "root" -> m.optDouble("chamber", 0.0)
                        "handsOptions" -> if (m.optBoolean("cpu")) 1.0 else 0.0
                        else -> 0.0
                    }
                    val str = m.optString("file", m.optString("text", ""))
                    Host.message(type, on, m.optInt("id"), num, str, m.optInt("width"), m.optInt("height"))
                    if (type == "handsOptions") vision.options(m.optBoolean("twoHands"), m.optBoolean("cpu"))
                    if (type == "ready") {
                        ready = true
                        pageEvent("fullscreen", JSONObject().put("on", fullscreen).toString())
                        vision.sendCameras(force = true)
                        runScript()
                    }
                }
            }
        }
    }

    private fun setFullscreen(on: Boolean) {
        fullscreen = on
        val c = window.insetsController ?: return
        c.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        if (on) c.hide(WindowInsets.Type.systemBars()) else c.show(WindowInsets.Type.systemBars())
    }

    // ---------------- native -> page and system (main thread, through Host) ----------------
    fun pageState(json: String) { if (ready) web?.evaluateJavascript("window.__fx && __fx.state($json)", null) }
    fun pageEvent(name: String, json: String) { web?.evaluateJavascript("window.__fx && __fx.event(${JSONObject.quote(name)}, $json)", null) }
    fun reply(id: Int, ok: Boolean, error: String?) {
        val p = JSONObject().put("id", id).put("ok", ok)
        if (error != null) p.put("error", error)
        pageEvent("reply", p.toString())
    }
    fun haptic(kind: Int) {
        picture.performHapticFeedback(if (kind == 1) HapticFeedbackConstants.CONFIRM else HapticFeedbackConstants.CLOCK_TICK)
    }
    fun awake(on: Boolean) {
        if (on) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
    fun requestPermission(what: String, id: Int) {
        val perm = if (what == "camera") Manifest.permission.CAMERA else Manifest.permission.RECORD_AUDIO
        if (checkSelfPermission(perm) == PackageManager.PERMISSION_GRANTED) { Host.permission(id, true); return }
        val code = nextAsk++ and 0xffff
        asked[code] = id
        requestPermissions(arrayOf(perm), code)
    }
    // A permission Kotlin itself needs (the camera); `then` runs on the main thread.
    fun ask(perm: String, then: (Boolean) -> Unit) {
        val code = nextAsk++ and 0xffff
        waiting[code] = then
        requestPermissions(arrayOf(perm), code)
    }
    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, results: IntArray) {
        val granted = results.isNotEmpty() && results[0] == PackageManager.PERMISSION_GRANTED
        Log.i("fx", "permission ${perms.joinToString()} ${if (granted) "granted" else "refused"}")
        waiting.remove(code)?.let { it(granted); return }
        val id = asked.remove(code) ?: return
        Host.permission(id, granted)
    }
    fun share(file: String) {
        val uri = Media.find(this, file) ?: run { Log.w("fx", "share: no film named $file"); return }
        val send = Intent(Intent.ACTION_SEND).setType("video/mp4").putExtra(Intent.EXTRA_STREAM, uri)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        startActivity(Intent.createChooser(send, null))
    }
    fun copy(text: String) {
        getSystemService(ClipboardManager::class.java).setPrimaryClip(ClipData.newPlainText("fractonaut", text))
    }

    // ---------------- touch ----------------
    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        touch.handle(ev)
        return super.dispatchTouchEvent(ev)   // the page always sees the touch too
    }

    // Every touch also reaches the page. The page says, per touch, whether it landed on
    // the interface; until it has, the touch's samples are held (with their times), then
    // sent to the engine or dropped. With no answer in 250 ms the touch paints.
    private inner class Touch {
        inner class Ptr(val x: Float, val y: Float) {
            var state = PENDING
            var ended = false
            val held = ArrayList<DoubleArray>()   // kind, x, y, t
        }
        private val ptrs = HashMap<Int, Ptr>()

        private fun sample(id: Int, p: Ptr, kind: Int, x: Float, y: Float, t: Double) {
            when (p.state) {
                PAINT -> Host.touch(id + ID_BASE, kind, x, y, t)
                PENDING -> if (p.held.size < 512) p.held.add(doubleArrayOf(kind.toDouble(), x.toDouble(), y.toDouble(), t))
            }
        }

        private fun decide(id: Int, p: Ptr, ui: Boolean) {
            if (p.state != PENDING || ptrs[id] !== p) return
            p.state = if (ui) IGNORE else PAINT
            if (!ui) for (s in p.held) Host.touch(id + ID_BASE, s[0].toInt(), s[1].toFloat(), s[2].toFloat(), s[3])
            p.held.clear()
            if (p.ended) ptrs.remove(id)
        }

        private fun end(id: Int, kind: Int, ev: MotionEvent, i: Int) {
            val p = ptrs[id] ?: return
            sample(id, p, kind, ev.getX(i), ev.getY(i), ev.eventTime / 1000.0)
            p.ended = true
            if (p.state != PENDING) ptrs.remove(id)
        }

        fun handle(ev: MotionEvent) {
            when (ev.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    val i = ev.actionIndex
                    val id = ev.getPointerId(i)
                    val p = Ptr(ev.getX(i), ev.getY(i))
                    ptrs[id] = p
                    sample(id, p, PTR_DOWN, p.x, p.y, ev.eventTime / 1000.0)
                    main.postDelayed({ decide(id, p, false) }, 250)
                }
                MotionEvent.ACTION_MOVE -> for (i in 0 until ev.pointerCount) {
                    val id = ev.getPointerId(i)
                    val p = ptrs[id] ?: continue
                    if (p.state == IGNORE) continue
                    for (h in 0 until ev.historySize)
                        sample(id, p, PTR_MOVE, ev.getHistoricalX(i, h), ev.getHistoricalY(i, h), ev.getHistoricalEventTime(h) / 1000.0)
                    sample(id, p, PTR_MOVE, ev.getX(i), ev.getY(i), ev.eventTime / 1000.0)
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> end(ev.getPointerId(ev.actionIndex), PTR_UP, ev, ev.actionIndex)
                MotionEvent.ACTION_CANCEL -> for (i in 0 until ev.pointerCount) end(ev.getPointerId(i), PTR_CANCEL, ev, i)
            }
        }

        // The page's answer for its latest pointerdown: the nearest touch still waiting.
        fun verdict(x: Float, y: Float, ui: Boolean) {
            val best = ptrs.entries.filter { it.value.state == PENDING }.minByOrNull { hypot(it.value.x - x, it.value.y - y) } ?: return
            if (hypot(best.value.x - x, best.value.y - y) > 64 * resources.displayMetrics.density) return
            decide(best.key, best.value, ui)
        }
    }

    companion object {
        const val HOST = "appassets.androidplatform.net"   // fixed https origin: storage and the URL hash persist
        val MIME = mapOf(
            "html" to "text/html", "js" to "text/javascript", "css" to "text/css", "json" to "application/json",
            "png" to "image/png", "jpg" to "image/jpeg", "webp" to "image/webp", "svg" to "image/svg+xml",
            "woff2" to "font/woff2", "txt" to "text/plain",
        )
        // engine.h PTR_*
        const val PTR_MOVE = 0; const val PTR_DOWN = 1; const val PTR_UP = 2; const val PTR_CANCEL = 3
        // Hands tracker delegate (Caps.handsDelegate). Measured on the S22 Ultra with no hand
        // in view: CPU 33-39 ms a frame, GPU 44-53 ms with a 1.6 s start and stalls of up to
        // 0.6 s, so CPU. The page's `cpu` option forces CPU either way.
        const val HANDS_GPU = false
        const val ID_BASE = 10   // engine pointer ids for touches (7 is fx.stroke's)
        const val PENDING = 0; const val PAINT = 1; const val IGNORE = 2
    }
}
