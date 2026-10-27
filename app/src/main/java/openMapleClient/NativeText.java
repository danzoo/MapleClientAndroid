package openMapleClient;

import android.content.res.AssetManager;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;

/**
 * Rasterizes single glyphs with the platform text renderer so the native client can draw text as
 * crisply as any other Android app, at any code point (including CJK). Called over JNI from the
 * render thread; software Bitmap/Canvas rendering is safe off the UI thread.
 */
public final class NativeText {
    /**
     * Transparent texels kept around every glyph. The renderer places a glyph's bitmap on the
     * screen one device pixel per texel, so a neighbour in the atlas could never be sampled
     * anyway; the border makes that true even when a position is a fraction of a pixel off.
     */
    private static final int PAD = 1;

    private static final String NORMAL_PATH = "fonts/PuHuiTi-Regular.otf";
    private static final String BOLD_PATH = "fonts/PuHuiTi-Bold.otf";

    // Greyscale coverage only. Subpixel and linear-text hints are deliberately off: the client
    // uploads a glyph as a single coverage channel, so the horizontal colour weighting LCD text
    // paints would just be thrown away, and the interpolated vertical positioning it implies is
    // one of the reasons two glyphs on a line came out heavier than the other.
    private static final Paint PAINT = new Paint(Paint.ANTI_ALIAS_FLAG);
    private static final Rect BOUNDS = new Rect();

    private static volatile Typeface normalFace = null;
    private static volatile Typeface boldFace = null;

    static {
        PAINT.setHinting(Paint.HINTING_ON);
    }

    private NativeText() {
    }

    /**
     * Load the bundled faces. Called from the activity before the render thread starts; until this
     * has run the rasterizer keeps using the platform faces.
     */
    public static void init(AssetManager assets) {
        if (normalFace != null) {
            return;
        }

        try {
            Typeface regular = Typeface.createFromAsset(assets, NORMAL_PATH);
            Typeface bold = Typeface.createFromAsset(assets, BOLD_PATH);

            normalFace = regular;
            boldFace = bold;
        } catch (Throwable thrown) {
            normalFace = null;
            boldFace = null;
        }
    }

    /**
     * The width the platform renderer would give the glyph at the given pixel size, without drawing
     * it. Layout counts these in whole logical pixels, so it is asked for at the logical size and
     * never depends on which target a glyph is later rasterised for.
     */
    public static float measure(int codePoint, float pxSize, boolean bold) {
        Typeface face = bold ? boldFace : normalFace;

        PAINT.setTextSize(pxSize);
        PAINT.setTypeface(face != null ? face : (bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT));

        return PAINT.measureText(new String(Character.toChars(codePoint)));
    }

    /**
     * Render one code point at the given pixel size.
     *
     * @param out receives, when length >= 6: [advance, inkLeft, inkTopAboveBaseline,
     *            inkWidth, inkHeight, ascentAboveBaseline]. ink* mirror FreeType's
     *            bitmap_left / bitmap_top / width / rows so the native layout code can treat a
     *            platform glyph exactly like the bundled faces.
     * @return an ARGB_8888 Bitmap holding white ink with per-pixel alpha = glyph coverage, sized
     *         inkWidth x inkHeight; null for blank glyphs such as space (advance is still valid).
     */
    public static Bitmap rasterize(int codePoint, float pxSize, boolean bold, float[] out) {
        String s = new String(Character.toChars(codePoint));

        Typeface face = bold ? boldFace : normalFace;

        PAINT.setTextSize(pxSize);
        PAINT.setTypeface(face != null ? face : (bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT));
        PAINT.setColor(Color.WHITE);

        float advance = PAINT.measureText(s);
        Paint.FontMetrics fm = PAINT.getFontMetrics();
        PAINT.getTextBounds(s, 0, s.length(), BOUNDS);

        int w = BOUNDS.width() + 2 * PAD;
        int h = BOUNDS.height() + 2 * PAD;

        if (out != null && out.length >= 6) {
            out[0] = advance;
            out[1] = BOUNDS.left - PAD;
            out[2] = -BOUNDS.top - PAD;
            out[3] = w;
            out[4] = h;
            out[5] = -fm.ascent;
        }

        if (BOUNDS.width() <= 0 || BOUNDS.height() <= 0) {
            return null;
        }

        Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        bmp.eraseColor(0);

        Canvas canvas = new Canvas(bmp);
        // Place the pen so the glyph's ink top-left lands at (PAD,PAD): the baseline sits at
        // PAD - BOUNDS.top and the ink starts at PAD - BOUNDS.left.
        canvas.drawText(s, PAD - BOUNDS.left, PAD - BOUNDS.top, PAINT);

        return bmp;
    }
}
