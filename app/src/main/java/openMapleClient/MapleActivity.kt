package openMapleClient

import android.app.NativeActivity
import android.graphics.Rect
import android.os.Bundle

class MapleActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        // Before super.onCreate(): that is where the native library is loaded and the render thread
        // that asks for glyphs begins.
        NativeText.init(assets)
        super.onCreate(savedInstanceState)
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
