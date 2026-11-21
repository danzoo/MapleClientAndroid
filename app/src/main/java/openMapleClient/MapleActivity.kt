package openMapleClient

import OpenMapleClient.BuildConfig
import android.app.NativeActivity
import android.graphics.Rect
import android.os.Bundle
import java.io.File
import java.io.FileOutputStream

class MapleActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        // Before super.onCreate(): that is where the native library is loaded and the render thread
        // that asks for glyphs begins. The release reader mmaps the zip, so it must be on disk first.
        if (!BuildConfig.DEBUG) releaseDataZip()
        NativeText.init(assets)
        super.onCreate(savedInstanceState)
    }

    // The release APK carries the Data tree as one STORE-only archive named "Data". Only the
    // archive itself is released, into the internal files dir (the external one already holds
    // the loose "Data" directory of debug installs); nothing is unpacked. The asset is stored
    // uncompressed, so its file-descriptor length is the full size and serves as the version
    // stamp (available() is an int and would overflow past 2GB).
    private fun releaseDataZip() {
        val target = File(filesDir, "Data")
        val want = assets.openFd("Data").use { it.length }
        if (target.exists() && target.length() == want) return
        val tmp = File(filesDir, "Data.tmp")
        val t0 = System.nanoTime()
        assets.open("Data").use { input ->
            FileOutputStream(tmp).use { out -> input.copyTo(out, 1 shl 20) }
        }
        check(tmp.renameTo(target)) { "Data release failed" }
        android.util.Log.i("MapleActivity",
            "Data released: $want bytes in ${(System.nanoTime() - t0) / 1_000_000} ms")
        // Earlier releases kept the archive next to the loose tree under its old name.
        File(getExternalFilesDir(null), "data.zip").delete()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus) return
        val decor = window.decorView
        decor.post {
            // Immersive mode hides the bars but Android still keeps the bottom ~55px for the
            // swipe-up home gesture, which steals any touch stream that ends there — the quick
            // slot bar's second row lives in exactly that strip. Exempt it from gesture spying.
            decor.systemGestureExclusionRects =
                listOf(Rect(0, decor.height - 120, decor.width, decor.height))
        }
    }
}
