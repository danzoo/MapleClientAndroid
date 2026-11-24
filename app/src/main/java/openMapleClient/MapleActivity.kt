package openMapleClient

import OpenMapleClient.BuildConfig
import android.app.NativeActivity
import android.graphics.Rect
import android.os.Bundle
import android.view.WindowInsets
import android.view.WindowInsetsController
import java.io.File
import java.io.FileOutputStream

class MapleActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        // NativeActivity starts native initialization inside super.onCreate(). Request the same
        // immersive state first, so the surface is not laid out with bars and resized immediately.
        applyImmersiveWindow()
        // Before super.onCreate(): that is where the native library is loaded and the render thread
        // that asks for glyphs begins. The release reader mmaps the zip, so it must be on disk first.
        if (!BuildConfig.DEBUG) {
            releaseDataZip()
            releaseHostIp()
        }
        NativeText.init(assets)
        super.onCreate(savedInstanceState)
    }

    private fun applyImmersiveWindow() {
        // Ensure DecorView exists before obtaining the controller. Before attachment it records
        // the request in its pending controller and applies it when the window becomes visible.
        window.decorView.windowInsetsController?.apply {
            systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            hide(WindowInsets.Type.systemBars())
        }
    }

    // The release APK carries the Data tree as one STORE-only archive named "Data", released
    // as a file into the external files dir; nothing is unpacked. The asset is stored
    // uncompressed, so its file-descriptor length is the full size and serves as the version
    // stamp (available() is an int and would overflow past 2GB).
    private fun releaseDataZip() {
        val target = File(getExternalFilesDir(null), "Data")
        // A debug install on the same device leaves the loose "Data" directory here; a file
        // cannot replace it, so leave it alone and let the native side read the loose tree.
        if (target.isDirectory) {
            android.util.Log.w("MapleActivity", "loose Data directory present; skipping release")
            return
        }
        val want = assets.openFd("Data").use { it.length }
        if (target.exists() && target.length() == want) return
        val tmp = File(target.parentFile, "Data.tmp")
        val t0 = System.nanoTime()
        assets.open("Data").use { input ->
            FileOutputStream(tmp).use { out -> input.copyTo(out, 1 shl 20) }
        }
        check(tmp.renameTo(target)) { "Data release failed" }
        android.util.Log.i("MapleActivity",
            "Data released: $want bytes in ${(System.nanoTime() - t0) / 1_000_000} ms")
        // Earlier releases kept the archive in the internal files dir, or next to the loose
        // tree under its old name.
        File(filesDir, "Data").delete()
        File(filesDir, "Data.tmp").delete()
        File(getExternalFilesDir(null), "data.zip").delete()
    }

    // The native connection code always reads hostip.txt from the external files dir. Seed it from
    // the APK on first release install, but never overwrite an address the user configured there.
    private fun releaseHostIp() {
        val target = File(getExternalFilesDir(null), "hostip.txt")
        if (target.exists()) return
        assets.open("hostip.txt").use { input ->
            FileOutputStream(target).use { out -> input.copyTo(out) }
        }
        android.util.Log.i("MapleActivity", "hostip.txt released to ${target.absolutePath}")
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
