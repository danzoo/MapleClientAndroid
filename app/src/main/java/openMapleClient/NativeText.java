package openMapleClient;

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
    private static final Paint PAINT = new Paint(
            Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG | Paint.LINEAR_TEXT_FLAG);
    private static final Rect BOUNDS = new Rect();

    private NativeText() {
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

        PAINT.setTextSize(pxSize);
        PAINT.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
        PAINT.setColor(Color.WHITE);

        float advance = PAINT.measureText(s);
        Paint.FontMetrics fm = PAINT.getFontMetrics();
        PAINT.getTextBounds(s, 0, s.length(), BOUNDS);

        int w = BOUNDS.width();
        int h = BOUNDS.height();

        if (out != null && out.length >= 6) {
            out[0] = advance;
            out[1] = BOUNDS.left;
            out[2] = -BOUNDS.top;
            out[3] = w;
            out[4] = h;
            out[5] = -fm.ascent;
        }

        if (w <= 0 || h <= 0) {
            return null;
        }

        Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
        bmp.eraseColor(0);

        Canvas canvas = new Canvas(bmp);
        // Place the pen so the glyph's ink top-left lands at (0,0): baseline sits at -BOUNDS.top
        // and the ink starts at -BOUNDS.left.
        canvas.drawText(s, -BOUNDS.left, -BOUNDS.top, PAINT);

        return bmp;
    }
}
