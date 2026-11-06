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
import android.view.animation.OvershootInterpolator;

import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.HashMap;

/**
 * The item, equipment and skill previews, built as a window of the platform rather than drawn into
 * the game's glyph atlas.
 *
 * A preview is mostly pictures and a few lines of lettering, and the lettering is the only part the
 * atlas cannot do well: it is rasterized at eleven units and magnified with everything else, so at
 * the interface's ~2.4x a description comes out soft and its strokes unequally heavy. The fix is the
 * one the message window already uses - let the platform letter the words at the size they are drawn
 * at - and the same fix carries the rest of the window with it, because a preview whose text is
 * crisp and whose frame is blurred looks like two windows.
 *
 * What is kept is the game's own work. Every number in here was computed by the C++ preview: the
 * frame's rectangles from the same {@code DrawArgument} arithmetic the GL pass used, the line breaks
 * and colours from the same layout, the requirement digits from the same character tables. The game
 * hands over a display list - the same records, in the same order, in interface units - and this
 * window only paints them. That is why the look does not change: nothing here decides where anything
 * goes.
 *
 * The art is split the way the message window split it. The chrome every preview shares - frames,
 * covers, the requirement glyphs - is the same handful of canvases out of UI/UIToolTip.img in every
 * preview, so it is exported once beside this window and read from the APK. What differs per item is
 * the item's own icon, and no window can read an archive, so those pixels come across with the list.
 *
 * One window, the whole surface, always. It is never moved and never resized, because a panel whose
 * frame changes shows the previous content stretched across the new frame for a frame or two before
 * the first draw lands - the message window learned that at its own expense. Coming and going is done
 * with strength alone. It is not focusable, since a panel that took the focus would stop the game's
 * update loop, and not touchable, since a preview is read rather than used and the press that opened
 * it belongs to the game.
 *
 * Views are only touched on the main thread; the render thread hands work over through {@link #UI}.
 */
public final class NativeTooltip {
    // The record kinds, matching ms::NativeTooltip::List::Op. A record is eight wide: the kind,
    // three fields that depend on the kind, and the rectangle in interface units.
    private static final int OP_TEXT = 0;
    private static final int OP_BLOCK = 1;
    private static final int OP_PICTURE = 2;
    private static final int OP_ICON = 3;
    private static final int OP_WIDTH = 8;

    // The two halves of a text record's last field: how big the platform should letter the run, and
    // whether it is one of the bold faces.
    private static final int SIZE_SHIFT = 1;
    private static final int BOLD_BIT = 1;

    private static final Handler UI = new Handler(Looper.getMainLooper());

    // The shared chrome, exported from UI/UIToolTip.img's Item subtree. The names come across in the
    // list already turned into file names - the archive path with its separators made into
    // underscores - so a piece drawn in a preview can be traced back to the canvas it came from.
    private static final String ART_DIR = "tooltip/";
    private static final String ART_EXT = ".png";

    // A preview arrives rather than appears. It grows out of the middle of the box it is about to
    // occupy and reaches full strength in the same beat, which carries the reader's eye from the
    // piece they just touched onto the words. The overshoot is what makes it a pop instead of a
    // fade; it is kept small because the box is mostly lettering, and a frame that swells past its
    // own edge and settles back makes the words swim.
    private static final long POP_MILLIS = 150;
    private static final float POP_FROM = 0.86f;
    private static final float POP_OVERSHOOT = 0.9f;

    private static final HashMap<String, Bitmap> artCache = new HashMap<>();
    private static final Paint FILTERED = new Paint(Paint.FILTER_BITMAP_FLAG);

    private static final Paint BLOCK_PAINT = new Paint();
    private static final Paint TEXT_PAINT = new Paint(Paint.ANTI_ALIAS_FLAG);

    private static Activity owner;
    private static WindowManager windows;
    private static Plate plate;
    private static WindowManager.LayoutParams params;

    // Interface units to device pixels. The game works in units - the space its own interface is
    // projected into - and the surface it draws into is that space magnified, so one unit is this
    // many pixels and every coordinate in a record has to be read through it.
    private static float unit = 1.0f;

    // The list last handed over. The window is built on the first preview rather than at startup, so
    // a list that arrives before the window exists is kept and drawn once it does - the same way the
    // message window keeps a line posted too early.
    private static int[] heldOps;
    private static String[] heldTexts;
    private static String[] heldNames;
    private static int[] heldPixels;

    private NativeTooltip() {
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
        });
    }

    /**
     * Puts a preview on the screen, or takes it off. An empty list is the request to take it off.
     *
     * Called from the render thread, once per change rather than once per frame - the game knows when
     * a preview has changed, and rebuilding a list the screen already shows would only cost work.
     */
    public static void display(final int[] ops, final String[] texts, final String[] names,
                               final int[] pixels) {
        UI.post(() -> {
            if (plate == null) {
                heldOps = ops;
                heldTexts = texts;
                heldNames = names;
                heldPixels = pixels;
                return;
            }

            plate.setList(ops, texts, names, pixels);
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
        // Held back until the first preview is actually handed over, so the window costs nothing
        // while the reader is doing something that has no preview to show.
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

        // The icons that came across with the list, paired with the payload offset each was cut from.
        // A platform bitmap is the only thing that can hold them and making one costs a copy of the
        // pixels, so the moment to pay for that is when the list arrives rather than every time the
        // screen is drawn. The offsets are kept beside them because an icon record names its pixels
        // by where they start, and the same canvas asked for twice is the same offset twice.
        private Bitmap[] icons = new Bitmap[0];
        private int[] iconOffsets = new int[0];

        // Reused across records so painting a preview allocates nothing.
        private final Rect destination = new Rect();
        private final Rect source = new Rect();
        private final HashMap<Bitmap, Bitmap> tiles = new HashMap<>();

        Plate(Activity context) {
            super(context);
        }

        void setList(int[] newOps, String[] newTexts, String[] newNames, int[] newPixels) {
            final int[] nextOps = newOps != null ? newOps : new int[0];
            final String[] nextTexts = newTexts != null ? newTexts : new String[0];
            final String[] nextNames = newNames != null ? newNames : new String[0];
            final int[] nextPixels = newPixels != null ? newPixels : new int[0];

            // A preview that has not changed is not a new one, and the beat belongs to the ones that
            // have. Geometry is left out of that test on purpose: the preview is drawn at the cursor,
            // so touching a new piece first carries the piece being left behind to the new cursor and
            // only then hands over the one being read. Counting that carry as an arrival makes the
            // old item pop once more on the way to the new.
            final boolean again = params.alpha > 0.0f
                    && Arrays.equals(without_geometry(ops), without_geometry(nextOps))
                    && Arrays.equals(texts, nextTexts)
                    && Arrays.equals(names, nextNames);

            ops = nextOps;
            texts = nextTexts;
            names = nextNames;
            icons = buildIcons(nextPixels);

            // Every tile was cut from a bitmap this call threw away.
            tiles.clear();

            // Coming and going by strength alone: the frame never changes, so nothing is ever shown
            // stretched across a new one.
            params.alpha = ops.length == 0 ? 0.0f : 1.0f;
            windows.updateViewLayout(this, params);

            if (ops.length == 0) {
                // The beat belongs to the preview that is arriving, not the one that just left.
                animate().cancel();
                setAlpha(1.0f);
                setScaleX(1.0f);
                setScaleY(1.0f);
            }
            else if (!again) {
                pop();
            }

            invalidate();
        }

        /** A copy of the list with every coordinate taken out, so only what it says is left. */
        private static int[] without_geometry(int[] records) {
            final int[] content = records.clone();

            for (int record = 0; record + OP_WIDTH <= content.length; record += OP_WIDTH) {
                content[record + 4] = 0;
                content[record + 5] = 0;

                if (content[record] != OP_TEXT) {
                    // A lettered run keeps its colour and its size where the others keep a second
                    // corner; those two say what is drawn, not where, so they stay.
                    content[record + 6] = 0;
                    content[record + 7] = 0;
                }
            }

            return content;
        }

        /**
         * Grows the preview that just arrived out of the middle of the box it is about to fill. The
         * pivot is that box and not the view: the view is the whole surface, so scaling about its
         * centre would drag a preview parked in a corner towards the middle of the screen as it
         * grew.
         */
        private void pop() {
            final Rect box = bounds();

            if (!box.isEmpty()) {
                setPivotX((box.left + box.right) / 2.0f);
                setPivotY((box.top + box.bottom) / 2.0f);
            }

            animate().cancel();
            setAlpha(0.0f);
            setScaleX(POP_FROM);
            setScaleY(POP_FROM);
            animate().alpha(1.0f).scaleX(1.0f).scaleY(1.0f)
                    .setDuration(POP_MILLIS)
                    .setInterpolator(new OvershootInterpolator(POP_OVERSHOOT))
                    .start();
        }

        /** The outermost rectangle the list paints, in device pixels. */
        private Rect bounds() {
            final Rect box = new Rect(Integer.MAX_VALUE, Integer.MAX_VALUE,
                                      Integer.MIN_VALUE, Integer.MIN_VALUE);

            for (int record = 0; record + OP_WIDTH <= ops.length; record += OP_WIDTH) {
                box.left = Math.min(box.left, px(ops[record + 4]));
                box.top = Math.min(box.top, px(ops[record + 5]));

                if (ops[record] == OP_TEXT) {
                    // A lettered run keeps its colour and size where the others keep a second
                    // corner, so its pen is the only geometry it carries. The frame it is written
                    // onto is in the list regardless, and that is what bounds the preview.
                    continue;
                }

                box.right = Math.max(box.right, px(ops[record + 6]));
                box.bottom = Math.max(box.bottom, px(ops[record + 7]));
            }

            return box;
        }

        @Override
        protected void onDraw(Canvas canvas) {
            // Records are painted in the order the game emitted them, which is the order the GL pass
            // drew them in. That is not incidental: a frame writes some of its own pieces over
            // others, and a preview that tidied that up would put different art on top.
            for (int record = 0; record + OP_WIDTH <= ops.length; record += OP_WIDTH) {
                final int kind = ops[record];
                final int a = ops[record + 1];
                final int b = ops[record + 2];
                final int c = ops[record + 3];
                final int x1 = ops[record + 4];
                final int y1 = ops[record + 5];
                final int x2 = ops[record + 6];
                final int y2 = ops[record + 7];

                // A lettered run carries no rectangle at all: its last two fields are its colour and
                // its size, and its first two are the pen and the baseline. Reading it through the
                // rectangle below would call it an empty box - the colour of a lit pixel is a
                // negative int - and every word in the preview would go missing.
                if (kind == OP_TEXT) {
                    if (a >= 0 && a < texts.length) {
                        TEXT_PAINT.setColor(x2);
                        TEXT_PAINT.setTextSize(px(y2 >> SIZE_SHIFT));
                        TEXT_PAINT.setFakeBoldText((y2 & BOLD_BIT) != 0);

                        canvas.drawText(texts[a], px(x1), px(y1), TEXT_PAINT);
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
                    case OP_BLOCK:
                        BLOCK_PAINT.setColor(a);
                        canvas.drawRect(destination.left, destination.top, destination.right,
                                        destination.bottom, BLOCK_PAINT);
                        break;

                    case OP_PICTURE:
                        if (a >= 0 && a < names.length) {
                            drawInto(canvas, art(names[a]), FILTERED);
                        }
                        break;

                    case OP_ICON:
                        drawPixelArt(canvas, icon(a, b, c));
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
         * An icon, magnified by hand rather than by asking the canvas to. Icons are pixel work asked
         * for at two interface units to the source pixel and then magnified again onto the surface,
         * so one source pixel is nearly four device pixels wide; interpolating that dissolves the
         * art's own outline, which is why the scene pass uses GL_NEAREST for the same pixels. A
         * scaled draw ignores the paint's filtering flag here, so the tile is grown with the one
         * call that takes the choice as an argument and then laid onto the canvas at one to one,
         * where there is nothing left to average. The shared chrome keeps its filtered draw - it
         * comes at half this magnification and reads better smoothed than stair-stepped.
         */
        private void drawPixelArt(Canvas canvas, Bitmap bitmap) {
            if (bitmap == null || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) {
                return;
            }

            Bitmap tile = tiles.get(bitmap);

            if (tile == null || tile.getWidth() != destination.width()
                    || tile.getHeight() != destination.height()) {
                tile = Bitmap.createScaledBitmap(bitmap, destination.width(),
                                                 destination.height(), false);
                tiles.put(bitmap, tile);
            }

            canvas.drawBitmap(tile, destination.left, destination.top, null);
        }

        /**
         * The canvas cut from the payload starting at this offset. The size is carried along so a
         * record whose pixels did not survive the crossing draws nothing instead of part of another
         * icon.
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
                if (ops[record] == OP_ICON) {
                    icon_records++;
                }
            }

            final Bitmap[] result = new Bitmap[icon_records];
            final int[] offsets = new int[icon_records];

            int count = 0;

            for (int record = 0; record + OP_WIDTH <= ops.length; record += OP_WIDTH) {
                if (ops[record] != OP_ICON) {
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
    }

    /**
     * One of the shared canvases. The archives they come out of are read by the native side, so the
     * pieces a preview is drawn from are shipped as loose images beside this window; they are decoded
     * once and kept, because the same frame is needed by every preview in the session.
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
                // An absent canvas draws nothing rather than throwing the preview away: a frame with
                // a hole in it still tells the reader what the item is, and a null here would take
                // the whole preview down with it.
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
