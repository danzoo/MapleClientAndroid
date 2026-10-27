package openMapleClient

import android.app.NativeActivity
import android.os.Bundle
import android.view.Gravity

class MapleActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        // Before super.onCreate(): that is where the native library is loaded and the render thread
        // that asks for glyphs begins.
        NativeText.init(assets)
        super.onCreate(savedInstanceState)
    }
}
