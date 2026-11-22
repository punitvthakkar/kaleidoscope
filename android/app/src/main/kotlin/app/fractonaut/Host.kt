package app.fractonaut

import android.view.Surface

// The JNI surface of android/native/host.cpp (contract: android/native/host.h).
// Kotlin -> native: the external functions, main thread only.
// Native -> Kotlin: the @JvmStatic callbacks, from any thread; each hops to the main
// thread through the activity, which is the only state they touch.
object Host {
    init { System.loadLibrary("fractonaut") }

    @JvmStatic external fun start(
        sdk: Int, displayW: Int, displayH: Int, refreshHz: Float, density: Float,
        displayHlg: Boolean, displayMaxNits: Float,
        hevcEncoder: String, hevcMain10: Boolean, avcEncoder: String, filesDir: String, handsDelegate: Int,
    )
    @JvmStatic external fun cameraTexture(st: android.graphics.SurfaceTexture?, aspect: Float)
    @JvmStatic external fun handsEnabled(on: Boolean)
    @JvmStatic external fun cameraAspect(widthOverHeight: Float)
    // Any thread: converts one camera image (three direct plane buffers) into `out`.
    @JvmStatic external fun yuvToBitmap(y: java.nio.ByteBuffer, u: java.nio.ByteBuffer, v: java.nio.ByteBuffer, yRow: Int, yPix: Int,
                                        cRow: Int, cPix: Int, w: Int, h: Int, rot: Int, out: android.graphics.Bitmap): Boolean
    @JvmStatic external fun gripHeld(): Boolean
    @JvmStatic external fun hands(data: FloatArray, n: Int, t: Double)
    @JvmStatic external fun surfaceCreated(surface: Surface)
    @JvmStatic external fun surfaceChanged(w: Int, h: Int)
    @JvmStatic external fun surfaceDestroyed()
    @JvmStatic external fun foreground(on: Boolean)
    @JvmStatic external fun touch(id: Int, kind: Int, x: Float, y: Float, t: Double)
    @JvmStatic external fun thermal(headroom: Float, status: Int)
    @JvmStatic external fun setParam(key: String, num: Double, str: String?)
    @JvmStatic external fun setSystem(key: String, num: Double)
    @JvmStatic external fun message(type: String, on: Boolean, id: Int, num: Double, str: String, w: Int, h: Int)
    @JvmStatic external fun permission(id: Int, granted: Boolean)
    @JvmStatic external fun debug(key: String, value: String)

    // Set in MainActivity.onCreate before start(); the activity lives as long as the process.
    lateinit var app: MainActivity
    var started = false   // host_start runs once per process, even if the activity is recreated

    @JvmStatic fun pageState(json: String) = app.ui { app.pageState(json) }
    @JvmStatic fun pageEvent(name: String, json: String) = app.ui { app.pageEvent(name, json) }
    @JvmStatic fun reply(id: Int, ok: Boolean, error: String?) = app.ui { app.reply(id, ok, error) }
    @JvmStatic fun haptic(kind: Int) = app.ui { app.haptic(kind) }
    @JvmStatic fun awake(on: Boolean) = app.ui { app.awake(on) }
    @JvmStatic fun requestPermission(what: String, id: Int) = app.ui { app.requestPermission(what, id) }
    @JvmStatic fun mediaOpen(file: String, mime: String): Int = Media.open(app, file, mime)
    @JvmStatic fun mediaClose(fd: Int, keep: Boolean) = Media.close(app, fd, keep)
    @JvmStatic fun share(file: String) = app.ui { app.share(file) }
    @JvmStatic fun copy(text: String) = app.ui { app.copy(text) }
}
