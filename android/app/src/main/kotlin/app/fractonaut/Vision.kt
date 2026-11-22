package app.fractonaut

import android.Manifest
import android.annotation.SuppressLint
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.ImageFormat
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.OutputConfiguration
import android.hardware.camera2.params.SessionConfiguration
import android.media.ImageReader
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.os.SystemClock
import android.util.Log
import android.util.Range
import android.util.Size
import android.view.Surface
import com.google.mediapipe.framework.image.BitmapImageBuilder
import com.google.mediapipe.tasks.core.BaseOptions
import com.google.mediapipe.tasks.core.Delegate
import com.google.mediapipe.tasks.vision.core.RunningMode
import com.google.mediapipe.tasks.vision.handlandmarker.HandLandmarker
import com.google.mediapipe.tasks.vision.handlandmarker.HandLandmarkerResult
import kotlin.math.abs
import org.json.JSONArray
import org.json.JSONObject

// Camera light and Hands (shim protocol: `camera`, `hands`, `handsOptions`,
// `cameraDevice`, and the `cameras` event). One camera (the front one unless the page
// chose another), one session, up to two outputs: a SurfaceTexture the render thread
// samples (camera light) and a small ImageReader for the tracker (Hands). The camera
// is open only while one of them is on, and fully closed otherwise.
// Threads: everything here is main-thread state except the Tracker, which owns its own
// thread; camera callbacks are delivered on the main thread.
class Vision(private val act: MainActivity, private val handsGpu: Boolean, private val stats: () -> Boolean) {
    private val main = Handler(act.mainLooper)
    private val cm = act.getSystemService(CameraManager::class.java)

    // what the page asked for
    private var camOn = false
    private var handsOn = false
    private var twoHands = true
    private var cpu = false
    private var camId = 0        // pending replies
    private var handsId = 0
    private var foreground = true

    // the camera
    private var wanted = ""      // the page's choice; "" is automatic: the front camera
    private var cameraId: String? = null
    @Volatile private var sensorDeg = 0
    @Volatile private var facesUser = true
    private var listSent: String? = null
    private var device: CameraDevice? = null
    private var opening = false
    private var session: CameraCaptureSession? = null
    private var texture: SurfaceTexture? = null
    private var textureSurface: Surface? = null
    private var reader: ImageReader? = null
    private var gen = 0          // bumps on every reconfiguration; stale callbacks are ignored
    private var size = Size(640, 480)
    private val tracker = Tracker()

    // ---------------- messages ----------------
    fun camera(on: Boolean, id: Int) {
        if (!on) { camOn = false; reconfigure(); return }
        if (camId != 0) act.reply(camId, false, "failed")   // superseded
        camId = id
        permitted { ok ->
            if (!ok) { answerCamera(false, "denied"); return@permitted }
            camOn = true
            reconfigure()
        }
    }

    fun hands(on: Boolean, id: Int) {
        if (!on) {
            handsOn = false
            Log.i("fx", "hands off at ${now()}")
            Host.handsEnabled(false)
            Host.hands(FloatArray(0), 0, now())
            reconfigure()
            return
        }
        if (handsId != 0) act.reply(handsId, false, "failed")
        handsId = id
        permitted { ok ->
            if (!ok) { answerHands(false, "denied"); return@permitted }
            handsOn = true
            Host.handsEnabled(true)
            tracker.seenAt = System.nanoTime() / 1e9   // start quick, as if a hand had just left
            reconfigure()
        }
    }

    fun options(two: Boolean, cpuOnly: Boolean) {
        if (two == twoHands && cpuOnly == cpu) return
        twoHands = two; cpu = cpuOnly
        if (handsOn) tracker.configure(numHands(), delegateGpu())
    }

    fun foreground(on: Boolean) {
        foreground = on
        reconfigure()
    }

    // Which camera to use ("" for automatic). A running camera changes over at once.
    fun device(id: String) {
        if (id == wanted) return
        wanted = id
        cameraId = null
        reconfigure()
    }

    // The `cameras` event: every camera there is, named by where it points. Sent when
    // the page is ready and again whenever one is plugged in or removed.
    fun sendCameras(force: Boolean = false) {
        val list = JSONArray()
        val seen = HashMap<String, Int>()
        for (id in ids()) {
            val kind = when (facing(id)) {
                CameraMetadata.LENS_FACING_FRONT -> "Front camera"
                CameraMetadata.LENS_FACING_BACK -> "Back camera"
                else -> "External camera"
            }
            val n = (seen[kind] ?: 0) + 1
            seen[kind] = n
            list.put(JSONObject().put("id", id).put("label", if (n == 1) kind else "$kind $n"))
        }
        val json = JSONObject().put("list", list).toString()
        if (!force && json == listSent) return
        listSent = json
        act.pageEvent("cameras", json)
    }

    private fun ids(): List<String> = try { cm.cameraIdList.toList() } catch (_: Exception) { emptyList() }
    private fun facing(id: String): Int? =
        try { cm.getCameraCharacteristics(id).get(CameraCharacteristics.LENS_FACING) } catch (_: Exception) { null }

    init {
        cm.registerAvailabilityCallback(object : CameraManager.AvailabilityCallback() {
            override fun onCameraAvailable(id: String) = changed()
            override fun onCameraUnavailable(id: String) = changed()
            // A camera that was plugged in or pulled out: tell the page, and go back to
            // the chosen one if it has just returned.
            private fun changed() {
                if (listSent == null) return
                sendCameras()
                if (wanted.isNotEmpty() && cameraId != wanted && wanted in ids()) { cameraId = null; reconfigure() }
            }
        }, main)
    }

    private fun numHands() = if (twoHands) 2 else 1
    private fun delegateGpu() = handsGpu && !cpu

    private fun permitted(then: (Boolean) -> Unit) {
        if (act.checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) then(true)
        else { Log.i("fx", "camera: asking for permission"); act.ask(Manifest.permission.CAMERA, then) }
    }

    private fun answerCamera(ok: Boolean, error: String?) {
        if (camId != 0) { act.reply(camId, ok, error); camId = 0 }
    }
    private fun answerHands(ok: Boolean, error: String?) {
        if (handsId != 0) { act.reply(handsId, ok, error); handsId = 0 }
    }

    private fun fail(error: String) {
        Log.w("fx", "camera: $error")
        answerCamera(false, error)
        answerHands(false, error)
        if (handsOn) { act.pageEvent("handsFailed", "{}"); handsOn = false; Host.handsEnabled(false) }
        if (camOn) act.pageEvent("cameraFailed", "{}")
        camOn = false
        reconfigure()
    }

    // ---------------- the camera ----------------
    // Brings the camera to exactly what is wanted now.
    private fun reconfigure() {
        val wantCam = camOn && foreground
        val wantHands = handsOn && foreground
        gen++
        if (!wantCam && !wantHands) { closeAll(); return }
        val id = cameraId ?: pickCamera() ?: return fail("unavailable")
        var dev = device
        if (dev != null && dev.id != id) { closeAll(); dev = null }   // another camera was chosen
        if (dev == null) {
            if (opening) return
            opening = true
            @SuppressLint("MissingPermission")
            fun open() = cm.openCamera(id, object : CameraDevice.StateCallback() {
                override fun onOpened(d: CameraDevice) {
                    opening = false
                    device = d
                    Log.i("fx", "camera open at ${now()}")
                    reconfigure()   // whatever is wanted by now, possibly nothing
                }
                override fun onDisconnected(d: CameraDevice) { d.close(); if (device === d) { device = null; session = null }; opening = false; fail("failed") }
                override fun onError(d: CameraDevice, e: Int) { d.close(); if (device === d) { device = null; session = null }; opening = false; fail("failed") }
                override fun onClosed(d: CameraDevice) { Log.i("fx", "camera closed at ${now()}") }
            }, main)
            try { open() } catch (e: Exception) { opening = false; fail("failed") }
            return
        }
        // outputs
        if (wantCam && texture == null) {
            val st = SurfaceTexture(false)
            val ts = textureSize(id)
            st.setDefaultBufferSize(ts.width, ts.height)
            texture = st
            textureSurface = Surface(st)
            Host.cameraTexture(st, upright(ts.width, ts.height))
        }
        if (wantHands && reader == null) {
            val r = ImageReader.newInstance(size.width, size.height, ImageFormat.YUV_420_888, 2)
            tracker.attach(r, numHands(), delegateGpu())
            reader = r
        }
        val outputs = ArrayList<OutputConfiguration>()
        val targets = ArrayList<Surface>()
        if (wantCam) textureSurface?.let { s ->
            outputs += OutputConfiguration(s).apply { if (Build.VERSION.SDK_INT >= 33) mirrorMode = OutputConfiguration.MIRROR_MODE_NONE }
            targets += s
        }
        if (wantHands) reader?.surface?.let { s -> outputs += OutputConfiguration(s); targets += s }
        Host.cameraAspect(upright(size.width, size.height))   // both outputs are 4:3
        val g = gen
        val cfg = SessionConfiguration(SessionConfiguration.SESSION_REGULAR, outputs, act.mainExecutor,
            object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(s: CameraCaptureSession) {
                    if (g != gen || device !== dev) { s.close(); return }
                    session = s
                    try {
                        val req = dev.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                        targets.forEach { req.addTarget(it) }
                        req.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, fpsRange)
                        s.setRepeatingRequest(req.build(), null, main)
                    } catch (e: Exception) { fail("failed"); return }
                    if (wantCam) answerCamera(true, null)
                    if (wantHands) answerHands(true, null)
                    // Outputs no longer wanted go once the new session runs without them.
                    if (!wantCam) dropTexture()
                    if (!wantHands) dropReader()
                }
                override fun onConfigureFailed(s: CameraCaptureSession) { if (g == gen) fail("failed") }
            })
        try { dev.createCaptureSession(cfg) } catch (e: Exception) { fail("failed") }
    }

    private fun closeAll() {
        try { session?.close() } catch (_: Exception) {}
        session = null
        device?.close()
        device = null
        dropTexture()
        dropReader()
    }

    private fun dropTexture() {
        if (texture == null) return
        Host.cameraTexture(null, 0f)   // returns once the render thread let go of it
        textureSurface?.release()
        texture?.release()
        textureSurface = null
        texture = null
    }

    private fun dropReader() {
        val r = reader ?: return
        tracker.detach(r)   // closes it on the tracker thread, after any frame in hand
        reader = null
    }

    // The chosen camera if it is there; otherwise the front one, then any other.
    private var fpsRange = Range(30, 30)

    private fun pickCamera(): String? {
        val all = ids()
        val order = all.filter { it == wanted } + all.filter { facing(it) == CameraMetadata.LENS_FACING_FRONT } + all
        for (id in order) {
            val c = try { cm.getCameraCharacteristics(id) } catch (_: Exception) { continue }
            val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP) ?: continue
            cameraId = id
            sensorDeg = c.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 0
            facesUser = c.get(CameraCharacteristics.LENS_FACING) == CameraMetadata.LENS_FACING_FRONT
            // The web's camera: VGA for the tracker (4:3, as the sensor is).
            size = near(map.getOutputSizes(ImageFormat.YUV_420_888), 640) ?: Size(640, 480)
            // A steady 30 frames a second, never fewer: in dim light a camera allowed to drop
            // to 15 doubles the hands' delay and blurs them. The fastest fixed rate up to 30 wins.
            val ranges = c.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES) ?: emptyArray()
            fpsRange = ranges.filter { it.upper <= 30 }.maxWithOrNull(compareBy<Range<Int>>({ it.upper }, { it.lower })) ?: Range(15, 30)
            return id
        }
        return null
    }

    // Camera light: 4:3 like the tracker's image, about 1280 wide (the web's HD wish).
    private fun textureSize(id: String): Size {
        val map = cm.getCameraCharacteristics(id).get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
        return near(map?.getOutputSizes(SurfaceTexture::class.java), 1280) ?: Size(1280, 960)
    }

    // The 4:3 size closest to `width`; a camera with no 4:3 size gives its closest of any shape.
    private fun near(sizes: Array<Size>?, width: Int): Size? {
        val all = sizes ?: return null
        return all.filter { abs(it.width * 3 - it.height * 4) < 8 }.minByOrNull { abs(it.width - width) }
            ?: all.minByOrNull { abs(it.width - width) }
    }

    // The upright image's width / height for a sensor-oriented size.
    private fun upright(w: Int, h: Int): Float {
        val r = rotation()
        return if (r % 180 == 90) h.toFloat() / w else w.toFloat() / h
    }

    // Clockwise degrees that make the camera's image upright on the screen now. A camera
    // facing away turns the other way with the display (any thread: both fields are set
    // when a camera is picked, before its first output exists).
    private fun rotation(): Int {
        val display = (act.display?.rotation ?: Surface.ROTATION_0) * 90
        return (sensorDeg - (if (facesUser) display else -display) + 360) % 360
    }

    private fun now() = System.nanoTime() / 1e9

    // ---------------- the tracker ----------------
    // MediaPipe HandLandmarker in live-stream mode on its own thread. A camera frame is
    // used only when the previous one has been answered and the engine's cadence allows;
    // otherwise it is dropped at once.
    private inner class Tracker {
        // Urgent priority: MediaPipe's own threads are made from this one and inherit it, so
        // the tracker runs on the fast cores instead of queueing behind the picture.
        private val thread = HandlerThread("fx.hands", Process.THREAD_PRIORITY_URGENT_DISPLAY).apply { start() }
        private val h = Handler(thread.looper)
        // tracker-thread state
        // Two trackers: looking for a second hand costs a palm search on every frame (about
        // 50 ms on the CPU), so one hand is followed by a one-hand tracker, frame after frame,
        // while a two-hand tracker glances a few times a second for another; once it holds
        // two hands it follows both on every frame (following needs no search).
        private var lm: HandLandmarker? = null       // one hand
        private var lm2: HandLandmarker? = null      // two hands (only when two are wanted)
        private var pair = false                     // lm2 is following two hands
        private var glance = 0
        private var busy = false
        private var sentAt = -1.0
        // Tracking cadence: every camera frame while a hand is here (responsiveness first),
        // a few glances a second once it has left, one a second after ten seconds of
        // nobody there.
        @Volatile var seenAt = 0.0
        private var fastUntil = 0.0
        private var heardAt = 0.0
        private val was = BooleanArray(2)
        private val gx = DoubleArray(2)
        private val gy = DoubleArray(2)
        private fun gap(t: Double): Double {
            val away = t - seenAt
            return if (away < 1.5) 0.0 else if (away < 10) 0.140 else 1.0
        }
        private var frameT = 0.0
        private var sentNanos = 0L
        private var lastMs = 0L
        // Two images: while the tracker works on one, the newest camera frame is made ready
        // in the other and handed over the moment the tracker answers (no wait for the camera).
        private val bitmaps = arrayOfNulls<Bitmap>(2)
        private var slot = 0                 // the image the tracker has now
        private var ready2: Bitmap? = null   // the next one, made ready while it works
        private var readyT = 0.0
        private val bitmap get() = bitmaps[slot]
        private var ready = false
        private var gpu = false
        // stats, one line a second
        private var statAt = 0.0
        private var nFrames = 0
        private var nSeen = 0
        private var detectNs = 0L
        private var convertNs = 0L

        fun attach(r: ImageReader, n: Int, useGpu: Boolean) {
            r.setOnImageAvailableListener({ onImage(it) }, h)
            h.post { create(n, useGpu) }
        }

        fun configure(n: Int, useGpu: Boolean) = h.post { if (lm != null) create(n, useGpu) }

        fun detach(r: ImageReader) {
            h.post {
                r.setOnImageAvailableListener(null, null)
                r.close()
                lm?.close()
                lm = null
                lm2?.close()
                lm2 = null
                pair = false
                busy = false
                ready = false
                bitmaps.forEach { it?.recycle() }
                bitmaps.fill(null)
                ready2 = null
            }
        }

        private fun create(n: Int, useGpu: Boolean) {
            lm?.close(); lm2?.close()
            lm = null; lm2 = null; pair = false
            busy = false
            gpu = useGpu
            val t0 = SystemClock.uptimeMillis()
            fun make(hands: Int): HandLandmarker {
                val base = BaseOptions.builder().setModelAssetPath("hand_landmarker.task")
                    .setDelegate(if (useGpu) Delegate.GPU else Delegate.CPU).build()
                val opts = HandLandmarker.HandLandmarkerOptions.builder().setBaseOptions(base)
                    .setRunningMode(RunningMode.LIVE_STREAM).setNumHands(hands)
                    .setResultListener { r, _ -> h.post { onResult(r, hands) } }
                    .setErrorListener { e -> Log.w("fx", "hands: $e"); h.post { busy = false } }
                    .build()
                return HandLandmarker.createFromOptions(act, opts)
            }
            try {
                lm = make(1)
                if (n >= 2) lm2 = make(2)
            } catch (e: Exception) {
                Log.w("fx", "hands: tracker could not start: $e")
                main.post { if (handsOn) { act.pageEvent("handsFailed", "{}"); handsOn = false; Host.handsEnabled(false); reconfigure() } }
                return
            }
            Log.i("fx", "hands: tracker ready in ${SystemClock.uptimeMillis() - t0} ms, ${if (useGpu) "gpu" else "cpu"}, $n hands")
            if (!ready) { ready = true; main.post { if (handsOn) act.pageEvent("handsReady", "{}") } }
        }

        private fun onImage(r: ImageReader) {
            val img = try { r.acquireLatestImage() } catch (_: Exception) { null } ?: return
            img.use {
                if (lm == null) return
                val t = System.nanoTime() / 1e9
                if (sentAt >= 0 && t - sentAt < gap(t)) return
                val c0 = System.nanoTime()
                if (busy) {
                    // The tracker is working: make this (newest) frame ready in the other image.
                    ready2 = convert(img, rotation(), 1 - slot); readyT = t
                    convertNs += System.nanoTime() - c0
                    return
                }
                val bmp = convert(img, rotation(), slot)
                convertNs += System.nanoTime() - c0
                send(bmp, t)
            }
        }

        private fun send(bmp: Bitmap, t: Double) {
            val one = lm ?: return
            // Two hands held: follow both. Otherwise the one-hand tracker, and every eighth
            // frame (about four times a second) a glance for a second hand.
            val two = lm2
            val l = if (two != null && (pair || ++glance % 8 == 0)) two else one
            sentAt = t
            frameT = t
            busy = true
            sentNanos = System.nanoTime()
            val ms = maxOf(lastMs + 1, SystemClock.uptimeMillis())
            lastMs = ms
            try { l.detectAsync(BitmapImageBuilder(bmp).build(), ms) } catch (e: Exception) { busy = false; Log.w("fx", "hands: $e") }
        }

        private fun onResult(r: HandLandmarkerResult, from: Int) {
            busy = false
            if (from == 2) pair = r.landmarks().size >= 2
            detectNs += System.nanoTime() - sentNanos
            nFrames++
            val marks = r.landmarks()
            val sides = r.handedness()
            val n = minOf(marks.size, 2)
            val data = FloatArray(44 * n)
            for (i in 0 until n) {
                val p = marks[i]
                for (k in 0 until 21) { data[44 * i + k] = p[k].x(); data[44 * i + 21 + k] = p[k].y() }
                val side = sides.getOrNull(i)?.firstOrNull()
                data[44 * i + 42] = if (side?.categoryName() == "Right") 1f else 0f
                data[44 * i + 43] = side?.score() ?: 0f
            }
            if (n > 0) nSeen++
            // A hand that just appeared, or a grip point moving faster than 0.3 screen-widths
            // a second, earns the full rate for half a second.
            val secs = maxOf(0.01, frameT - heardAt)
            heardAt = frameT
            val seen = BooleanArray(2)
            for (i in 0 until n) {
                val s = if (data[44 * i + 42] > 0.5f) 1 else 0
                val p = 44 * i
                val x = (data[p + 4] + data[p + 8]) / 2.0 * 1.25
                val y = (data[p + 21 + 4] + data[p + 21 + 8]) / 2.0 * 1.25
                if (!was[s] || Math.hypot(x - gx[s], y - gy[s]) / secs > 0.3) fastUntil = frameT + 0.5
                gx[s] = x; gy[s] = y; seen[s] = true
            }
            was[0] = seen[0]; was[1] = seen[1]
            if (n > 0) seenAt = frameT
            Host.hands(data, n, frameT)
            // A frame made ready meanwhile goes straight in (it is at most one camera frame old).
            ready2?.let { b -> ready2 = null; slot = 1 - slot; send(b, readyT) }
            val t = System.nanoTime() / 1e9
            if (t - statAt >= 1) {
                if (stats() && statAt > 0)
                    Log.i("fx", "hands fps=$nFrames seen=$nSeen detect_ms=${"%.1f".format(detectNs / 1e6 / maxOf(1, nFrames))} " +
                        "convert_ms=${"%.1f".format(convertNs / 1e6 / maxOf(1, nFrames))} gap_ms=${"%.0f".format(gap(t) * 1000)} " +
                        "delegate=${if (gpu) "gpu" else "cpu"} image=${bitmap?.width}x${bitmap?.height} trackers=${if (lm2 != null) 2 else 1} pair=$pair")
                statAt = t; nFrames = 0; nSeen = 0; detectNs = 0; convertNs = 0
            }
        }

        // YUV_420_888 to an upright, unmirrored ARGB bitmap (the web's video frame).
        private fun convert(img: android.media.Image, rot: Int, into: Int): Bitmap {
            val sw = img.width; val sh = img.height
            val turned = rot % 180 == 90
            val ow = if (turned) sh else sw
            val oh = if (turned) sw else sh
            var b = bitmaps[into]
            if (b == null || b.width != ow || b.height != oh) {
                b?.recycle()
                b = Bitmap.createBitmap(ow, oh, Bitmap.Config.ARGB_8888)
                bitmaps[into] = b
            }
            val yP = img.planes[0]; val uP = img.planes[1]; val vP = img.planes[2]
            if (!Host.yuvToBitmap(yP.buffer, uP.buffer, vP.buffer, yP.rowStride, yP.pixelStride, uP.rowStride, uP.pixelStride,
                                  sw, sh, rot, b)) Log.w("fx", "hands: camera image did not convert")
            return b
        }
    }
}
