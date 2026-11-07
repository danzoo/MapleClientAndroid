package openMapleClient;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;

import java.io.IOException;
import java.io.InputStream;
import java.util.HashMap;

/**
 * The dialog a non-player character talks through, built as a window of the platform rather than
 * drawn into the game's glyph atlas.
 *
 * Same arrangement as the item previews, and for the same reason: the dialog is mostly lettering,
 * and the atlas letters at eleven units and magnifies with everything else, so at the interface's
 * ~2.4x what a character says comes out soft with unequally heavy strokes. The look is the game's
 * own - every rectangle, line break, colour and button placement was worked out by the C++ window
 * and handed over as a display list in interface units; this window only paints what it is told.
 *
 * The art is split the way the previews split it. The chrome every dialog shares - the three bands
 * of the frame, the name bar, the buttons, the scroll bar - is the same handful of canvases out of
 * UIWindow2.img and Basic.img in every dialog, so it is exported once beside this window and read
 * from the APK. What differs per conversation is the character speaking, and no window can read an
 * archive, so those pixels come across with the list.
 *
 * Unlike a preview, this window has to carry the buttons and the scroll bar too. A panel is above
 * the game's own surface, so art the game still paints lands behind the frame painted here - and the
 * buttons sit inside the bottom band. Being read is what moved to the platform; being pressed stays
 * with the game, which hit-tests the same rectangles and only asks for a new list when they change.
 *
 * One window, the whole surface, always. It is never moved and never resized, because a panel whose
 * frame changes shows the previous content stretched across the new frame for a frame or two before
 * the first draw lands. Coming and going is done with strength alone. It is not focusable, since a
 * panel that took the focus would stop the game's update loop, and not touchable, since every press
 * in a dialog belongs to the game.
 *
 * Views are only touched on the main thread; the render thread hands work over through {@link #UI}.
 */
public final class NativeNpcTalk {
    // The record kinds, matching ms::NativeNpcTalk::List::Op. A record is eight wide: the kind,
    // three fields that depend on the kind, and the rectangle in interface units.
    private static final int OP_TEXT = 0;
    private static final int OP_TEXT_CLIPPED = 1;
    private static final int OP_PICTURE = 2;
    private static final int OP_ICON = 3;
    private static final int OP_ICON_FLIPPED = 4;
    private static final int OP_WIDTH = 8;

    // The two halves of a text record's last field: how big the platform should letter the run, and
    // whether it is one of the bold faces.
    private static final int SIZE_SHIFT = 1;
    private static final int BOLD_BIT = 1;

    private static final Handler UI = new Handler(Looper.getMainLooper());

    // The shared chrome, exported from UIWindow2.img's UtilDlgEx and Quest subtrees and from the
    // scroll bar in Basic.img. The names come across in the list already turned into file names -
    // the archive path with its separators made into underscores - so a piece drawn in a dialog can
    // be traced back to the canvas it came from.
    private static final String ART_DIR = "npctalk/";
    private static final String ART_EXT = ".png";

    private static final HashMap<String, Bitmap> artCache = new HashMap<>();
    private static final Paint FILTERED = new Paint(Paint.FILTER_BITMAP_FLAG);

    private static final Paint TEXT_PAINT = new Paint(Paint.ANTI_ALIAS_FLAG);

    private static Activity owner;
    private static WindowManager windows;
    private static Plate plate;
    private static WindowManager.LayoutParams params;

    // Interface units to device pixels. The game works in units - the space its own interface is
    // projected into - and the surface it draws into is that space magnified, so one unit is this
    // many pixels and every coordinate in a record has to be read through it.
    private static float unit = 1.0f;

    // The list last handed over. The window is built on the first dialog rather than at startup, so
    // a list that arrives before the window exists is kept and drawn once it does.
    private static int[] heldOps;
    private static String[] heldTexts;
    private static String[] heldNames;
    private static int[] heldPixels;

    // The art in force: what the window is holding, or what the list waiting for a window is asking
    // it to hold. A letter crossed to say one more word of a sentence carries no new pictures with
    // it, and this is where the ones it means are kept.
    private static String[] artNames;
    private static int[] artPixels;

    private NativeNpcTalk() {
    }

    /**
     * Creates the window. Called from the render thread; everything below {@link #build} runs on the
     * main thread.
     *
     * The size comes from the game rather than from the window metrics: the list is measured in the
     * pixels the game draws into, and the metrics an activity reports are the display's own
     * orientation with the system bars taken off, which is neither that nor stable.
     */
    public static void open(final Activity target, final float unitPixels,
                            final int screenWidth, final int screenHeight) {
        owner = target;
        unit = Math.max(0.1f, unitPixels);

        UI.post(() -> build(target, screenWidth, screenHeight));
    }

    public static void close() {
        UI.post(() -> {
            if (windows != null && plate != null) {
                windows.removeView(plate);
            }

            plate = null;
            windows = null;
            params = null;
            heldOps = null;
            heldPixels = null;
            heldTexts = null;
            heldNames = null;
            artNames = null;
            artPixels = null;
        });
    }

    /**
     * Puts a dialog on the screen, or takes the words off. An empty list is the request to take them
     * off.
     *
     * Called from the render thread, once per change rather than once per frame - the game knows when
     * what is being said has changed, and rebuilding a list the screen already shows would only cost
     * work.
     */
    public static void display(final int[] ops, final String[] texts, final String[] names,
                               final int[] pixels) {
        UI.post(() -> {
            // A list with no art on it is saying that the pictures have not changed since the last one
            // went over. The ones it means are kept here, because the window may not exist yet to be
            // holding them.
            final String[] givenNames;
            final int[] givenPixels;

            if (names == null) {
                givenNames = artNames;
                givenPixels = artPixels;
            }
            else {
                givenNames = names;
                givenPixels = pixels;
                artNames = names;
                artPixels = pixels;
            }

            if (plate == null) {
                heldOps = ops;
                heldTexts = texts;
                heldNames = givenNames;
                heldPixels = givenPixels;
                return;
            }

            plate.setList(ops, texts, givenNames, givenPixels);
        });
    }

    private static void build(Activity target, int screenWidth, int screenHeight) {
        // Rebuilt only when the surface it covers has changed size. A window already in place is
        // left alone rather than taken down and put up again, because that is exactly the frame
        // change that shows the old content stretched.
        if (plate != null && params != null
                && params.width == screenWidth && params.height == screenHeight) {
            return;
        }

        if (windows != null && plate != null) {
            windows.removeView(plate);
        }

        params = new WindowManager.LayoutParams(
                screenWidth,
                screenHeight,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);

        params.gravity = Gravity.TOP | Gravity.START;
        params.x = 0;
        params.y = 0;
        // Held back until the first dialog is actually handed over, so the window costs nothing
        // while the reader is doing something that has no dialog to show.
        params.alpha = 0.0f;

        plate = new Plate(target);
        windows = target.getWindowManager();
        windows.addView(plate, params);

        if (heldOps != null) {
            plate.setList(heldOps, heldTexts, heldNames, heldPixels);
            heldOps = null;
            heldPixels = null;
            heldTexts = null;
            heldNames = null;
        }
    }

    /**
     * The surface a display list is painted onto.
     *
     * The list is kept rather than consumed, because android decides when to draw and may ask again
     * for any reason - a surface loss, part of the screen becoming visible.
     */
    private static final class Plate extends View {
        private int[] ops = new int[0];
        private String[] texts = new String[0];
        private String[] names = new String[0];

        // The pixels the icons below were cut from, kept for their identity alone. The game crosses the
        // same array object again to say that nothing about the art has changed, and making a bitmap
        // costs a copy of every pixel in it.
        private int[] pixels = new int[0];

        // The pictures that came across with the list, paired with the payload offset each was cut
        // from. A platform bitmap is the only thing that can hold them and making one costs a copy
        // of the pixels, so the moment to pay for that is when the list arrives rather than every
        // time the screen is drawn. The offsets are kept beside them because an icon record names
        // its pixels by where they start, and the same canvas asked for twice is the same offset
        // twice.
        private Bitmap[] icons = new Bitmap[0];
        private int[] iconOffsets = new int[0];

        // Reused across records so painting a dialog allocates nothing.
        private final Rect destination = new Rect();
        private final Rect source = new Rect();
        private final HashMap<Bitmap, Bitmap> tiles = new HashMap<>();
        private final HashMap<Bitmap, Bitmap> mirrored = new HashMap<>();

        Plate(Activity context) {
            super(context);
        }

        void setList(int[] newOps, String[] newTexts, String[] newNames, int[] newPixels) {
            ops = newOps != null ? newOps : new int[0];
            texts = newTexts != null ? newTexts : new String[0];

            final String[] namesGiven = newNames != null ? newNames : new String[0];
            final int[] pixelsGiven = newPixels != null ? newPixels : new int[0];

            // Only a list that brings different pictures pays for them. The words are what change from
            // one letter to the next, and the character speaking does not.
            if (namesGiven != names || pixelsGiven != pixels) {
                names = namesGiven;
                pixels = pixelsGiven;
                icons = buildIcons(pixelsGiven);

                // Every tile was cut from a bitmap this call threw away.
                tiles.clear();
                mirrored.clear();
            }

            // Coming and going by strength alone: the frame never changes, so nothing is ever shown
            // stretched across a new one. A dialog appears the moment the character starts speaking
            // and leaves the moment an answer is sent, which is what it always did. A layout is only
            // asked for when that changes, since the rest of the time it is the words that moved.
            final float alpha = ops.length == 0 ? 0.0f : 1.0f;

            if (params.alpha != alpha) {
                params.alpha = alpha;
                windows.updateViewLayout(this, params);
            }

            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            // Records are painted in the order the game emitted them, which is the order the GL pass
            // drew them in. That is not incidental: the frame writes some of its own pieces over
            // others, and a dialog that tidied that up would put different art on top.
            for (int record = 0; record + OP_WIDTH <= ops.length; record += OP_WIDTH) {
                final int kind = ops[record];
                final int a = ops[record + 1];
                final int b = ops[record + 2];
                final int c = ops[record + 3];
                final int x1 = ops[record + 4];
                final int y1 = ops[record + 5];
                final int x2 = ops[record + 6];
                final int y2 = ops[record + 7];

                if (kind == OP_TEXT || kind == OP_TEXT_CLIPPED) {
                    if (a < 0 || a >= texts.length) {
                        continue;
                    }

                    // A lettered run carries no rectangle at all: its last two fields are its colour
                    // and its size, and its first two are the pen and the baseline. Reading it
                    // through the rectangle below would call it an empty box - the colour of a lit
                    // pixel is a negative int - and everything the character says would go missing.
                    TEXT_PAINT.setColor(x2);
                    TEXT_PAINT.setTextSize(px(y2 >> SIZE_SHIFT));
                    TEXT_PAINT.setFakeBoldText((y2 & BOLD_BIT) != 0);

                    if (kind == OP_TEXT) {
                        canvas.drawText(texts[a], px(x1), px(y1), TEXT_PAINT);
                    }
                    else {
                        // The band the pass clipped this line to, so what has scrolled past simply
                        // is not shown. Only the vertical edges are the band - the layout already
                        // keeps the runs inside the frame horizontally.
                        canvas.save();
                        canvas.clipRect(0, px(b), canvas.getWidth(), px(c));
                        canvas.drawText(texts[a], px(x1), px(y1), TEXT_PAINT);
                        canvas.restore();
                    }

                    continue;
                }

                // Left and right are rounded separately. Rounding a width instead would let the two
                // edges of one piece disagree by a pixel and leave a seam between them.
                destination.set(px(x1), px(y1), px(x2), px(y2));

                if (destination.isEmpty()) {
                    continue;
                }

                switch (kind) {
                    case OP_PICTURE:
                        if (a >= 0 && a < names.length) {
                            drawInto(canvas, art(names[a]), FILTERED);
                        }
                        break;

                    case OP_ICON:
                        drawPixelArt(canvas, icon(a, b, c), false);
                        break;

                    case OP_ICON_FLIPPED:
                        drawPixelArt(canvas, icon(a, b, c), true);
                        break;

                    default:
                        break;
                }
            }
        }

        private void drawInto(Canvas canvas, Bitmap bitmap, Paint paint) {
            if (bitmap == null || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
                return;
            }

            source.set(0, 0, bitmap.getWidth(), bitmap.getHeight());
            canvas.drawBitmap(bitmap, source, destination, paint);
        }

        /**
         * A picture of the character speaking, magnified by hand rather than by asking the canvas
         * to. Standing art is pixel work asked for at its own size and then magnified onto the
         * surface, so one source pixel is several device pixels wide; interpolating that dissolves
         * the art's own outline, which is why the scene pass uses GL_NEAREST for the same pixels. A
         * scaled draw ignores the paint's filtering flag here, so the tile is grown with the one
         * call that takes the choice as an argument and then laid onto the canvas at one to one,
         * where there is nothing left to average. The shared chrome keeps its filtered draw - it
         * comes at half this magnification and reads better smoothed than stair-stepped.
         *
         * The dialog is drawn with the character facing the words, so the mirroring the pass did
         * with a negative scale is done here about the middle of the box the piece lands in.
         */
        private void drawPixelArt(Canvas canvas, Bitmap bitmap, boolean flipped) {
            if (bitmap == null || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
                return;
            }

            final HashMap<Bitmap, Bitmap> held = flipped ? mirrored : tiles;
            Bitmap tile = held.get(bitmap);

            if (tile == null || tile.getWidth() != destination.width()
                    || tile.getHeight() != destination.height()) {
                tile = Bitmap.createScaledBitmap(bitmap, destination.width(),
                                                 destination.height(), false);

                if (flipped) {
                    android.graphics.Matrix mirror = new android.graphics.Matrix();

                    mirror.preScale(-1.0f, 1.0f);
                    tile = Bitmap.createBitmap(tile, 0, 0, tile.getWidth(), tile.getHeight(),
                                               mirror, false);
                }

                held.put(bitmap, tile);
            }

            canvas.drawBitmap(tile, destination.left, destination.top, null);
        }

        /**
         * The canvas cut from the payload starting at this offset. The size is carried along so a
         * record whose pixels did not survive the crossing draws nothing instead of part of another
         * picture.
         */
        private Bitmap icon(int offset, int width, int height) {
            for (int k = 0; k < iconOffsets.length; k++) {
                if (iconOffsets[k] == offset) {
                    final Bitmap held = icons[k];

                    if (held == null
                            || held.getWidth() != width || held.getHeight() != height) {
                        return null;
                    }

                    return held;
                }
            }

            return null;
        }

        private Bitmap[] buildIcons(int[] payload) {
            // One slot per icon record, which is the most the list can ask for; the ones that turn
            // out to be a repeat of an icon already cut are dropped and the arrays shrunk to fit.
            int icon_records = 0;

            for (int record = 0; record + OP_WIDTH <= ops.length; record += OP_WIDTH) {
                if (is_icon(ops[record])) {
                    icon_records++;
                }
            }

            final Bitmap[] result = new Bitmap[icon_records];
            final int[] offsets = new int[icon_records];

            int count = 0;

            for (int record = 0; record + OP_WIDTH <= ops.length; record += OP_WIDTH) {
                if (!is_icon(ops[record])) {
                    continue;
                }

                final int offset = ops[record + 1];
                final int width = ops[record + 2];
                final int height = ops[record + 3];

                boolean seen = false;

                for (int k = 0; k < count; k++) {
                    if (offsets[k] == offset) {
                        seen = true;
                        break;
                    }
                }

                if (seen || width <= 0 || height <= 0
                        || offset < 0 || offset + width * height > payload.length) {
                    continue;
                }

                // Straight alpha, as the game sent it. createBitmap takes the components apart the
                // same way and leaves them unpremultiplied, which is what the platform wants.
                result[count] = Bitmap.createBitmap(payload, offset, width, width, height,
                                                    Bitmap.Config.ARGB_8888);
                offsets[count] = offset;
                count++;
            }

            iconOffsets = new int[count];
            System.arraycopy(offsets, 0, iconOffsets, 0, count);

            final Bitmap[] kept = new Bitmap[count];
            System.arraycopy(result, 0, kept, 0, count);

            return kept;
        }

        private static boolean is_icon(int kind) {
            return kind == OP_ICON || kind == OP_ICON_FLIPPED;
        }
    }

    /**
     * One of the shared canvases. The archives they come out of are read by the native side, so the
     * pieces a dialog is drawn from are shipped as loose images beside this window; they are decoded
     * once and kept, because the same frame is needed by every dialog in the session.
     */
    private static Bitmap art(String name) {
        Bitmap canvas = artCache.get(name);

        if (canvas == null) {
            try (InputStream file = owner.getAssets().open(ART_DIR + name + ART_EXT)) {
                canvas = BitmapFactory.decodeStream(file);
            } catch (IOException | IllegalStateException unreadable) {
                canvas = null;
            }

            if (canvas == null) {
                // An absent canvas draws nothing rather than throwing the dialog away: a frame with
                // a hole in it still tells the reader what is being said, and a null here would take
                // the whole window down with it.
                canvas = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888);
            }

            artCache.put(name, canvas);
        }

        return canvas;
    }

    /** One interface unit, in the pixels the surface is measured in. */
    private static int px(float value) {
        return Math.round(value * unit);
    }
}
