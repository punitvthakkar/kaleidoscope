package app.fractonaut

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Environment
import android.os.ParcelFileDescriptor
import android.provider.MediaStore
import java.util.concurrent.ConcurrentHashMap

// Saved files: pending MediaStore entries in Pictures/fractonaut (stills) and
// Movies/fractonaut (films), published when native closes them. Blocking; native calls
// these from its own worker threads, never the render or audio thread.
object Media {
    private val open = ConcurrentHashMap<Int, Uri>()   // fd -> its pending entry

    private fun collection(mime: String): Pair<Uri, String> =
        if (mime.startsWith("video/"))
            MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY) to "${Environment.DIRECTORY_MOVIES}/fractonaut"
        else
            MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY) to "${Environment.DIRECTORY_PICTURES}/fractonaut"

    fun open(ctx: Context, file: String, mime: String): Int {
        val (where, dir) = collection(mime)
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, file)
            put(MediaStore.MediaColumns.MIME_TYPE, mime)
            put(MediaStore.MediaColumns.RELATIVE_PATH, dir)
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        return try {
            val uri = ctx.contentResolver.insert(where, values) ?: return -1
            val pfd = ctx.contentResolver.openFileDescriptor(uri, "w")
            if (pfd == null) { ctx.contentResolver.delete(uri, null, null); return -1 }
            val fd = pfd.detachFd()
            open[fd] = uri
            fd
        } catch (e: Exception) {
            android.util.Log.w("fx", "media: cannot create $file: $e")
            -1
        }
    }

    fun close(ctx: Context, fd: Int, keep: Boolean) {
        val uri = open.remove(fd)
        try { ParcelFileDescriptor.adoptFd(fd).close() } catch (_: Exception) {}
        if (uri == null) return
        try {
            if (keep) ctx.contentResolver.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
            else ctx.contentResolver.delete(uri, null, null)
        } catch (e: Exception) {
            android.util.Log.w("fx", "media: cannot finish $uri: $e")
        }
    }

    // A film this app saved, by its file name (the page's `share` names it).
    fun find(ctx: Context, file: String): Uri? {
        val where = MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        ctx.contentResolver.query(
            where, arrayOf(MediaStore.MediaColumns._ID),
            "${MediaStore.MediaColumns.DISPLAY_NAME} = ? AND ${MediaStore.MediaColumns.IS_PENDING} = 0", arrayOf(file),
            "${MediaStore.MediaColumns.DATE_ADDED} DESC",
        )?.use { c -> if (c.moveToFirst()) return android.content.ContentUris.withAppendedId(where, c.getLong(0)) }
        return null
    }
}
