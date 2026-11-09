package openMapleClient;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.function.Consumer;

/**
 * The message window built out of platform views instead of drawn into the game's glyph atlas.
 *
 * Text is what this window exists for, and it is the one thing the atlas cannot do well: the game
 * rasterizes glyphs at eleven units and magnifies the atlas afterwards, so speech and announcements
 * come out soft at the interface's ~2.4x. A TextView is laid out at the size it is finally drawn,
 * so CJK gets real shaping, real line breaking, a scroll view with momentum, and the user's own
 * system font. Lines come in one way - the game posts one and never waits for an answer - so no
 * part of the update loop has to become asynchronous. What is typed goes back the other way through
 * a queue the game drains on its own update, which keeps the socket on the thread that owns it.
 *
 * Views are only touched on the main thread; the render thread hands work over through {@link #UI},
 * which also keeps a line posted before the window is built from being dropped, since both land on
 * the same queue in order.
 *
 * This is a window of its own, not a view inside the activity. The activity's content is the game's
 * surface, which Android composites over that content's own view tree, so a child added to
 * android.R.id.content gets attached, measured and drawn and is then covered by the next frame of
 * the game. A panel window is a layer of its own, above the game's, and the touches that land on it
 * go to it instead of to the native input queue.
 */
public final class NativeChat {
    // Mirrors ms::NativeChat::Category. General speech has no tab of its own, which is how the
    // original window behaved: it only ever showed in "all".
    public static final int CAT_GENERAL = 0;
    public static final int CAT_BATTLE = 1;
    public static final int CAT_PARTY = 2;
    public static final int CAT_FRIEND = 3;
    public static final int CAT_GUILD = 4;
    public static final int CAT_ALLIANCE = 5;
    public static final int CAT_NOTICE = 6;

    private static final Handler UI = new Handler(Looper.getMainLooper());
    private static final String[] TABS = {
        "全部", "战斗", "组队", "好友", "公会", "联盟", "公告"
    };
    private static final int TAB_ALL = 0;

    // The palette the atlas window used: Color::DUSTYGRAY for an inactive tab, white for the one
    // selected. Line colours arrive already resolved from Color::Name, so they are not repeated here.
    private static final int TAB_DIM = 0xFF999999;
    private static final int TAB_LIT = 0xFFFFFFFF;
    // The room each tab's lettering is held off the tab's own edge by. This is also how far the
    // lettering - what the window measures the tab against when it decides how wide to be - sits from
    // where the tab's art starts.
    private static final float TAB_PAD_UNITS = 6.0f;
    // How far the window fades when it is out of the way. It is still readable at this strength,
    // because a window the reader is working around is not the same thing as a window they asked not
    // to see; it only has to stop being the first thing the eye comes to.
    private static final float SUNK_ALPHA = 0.45f;
    // How long the window takes to come and go. This is the screen's own fade, which the window is
    // asked to keep step with: the game dims the picture it draws into over this many milliseconds,
    // and this window is composited by android above that picture, so it has to be faded by itself or
    // it would sit at full strength over a black loading screen.
    private static final int FADE_MS = 320;

    // The window's art, shipped as loose images beside it. Most of it is the chat bar's own, exported
    // from UI/StatusBar3.img's chat/ingame subtree - the archive the deleted UIChatBar built the window
    // out of. The names below are those paths with the slashes turned into underscores, so any piece
    // can be traced back to the canvas it came from. The two handles are the exception: the chat bar
    // kept a bare "+" against a "-" in a pill, which are not one another's match, so this window takes
    // its pair from UI/Basic.img's BtMax and BtMin instead - the squares every dialog's title bar is
    // closed with, which are drawn to go together.
    private static final String ART_DIR = "chat/";
    private static final String ART_EXT = ".png";
    private static final String NORMAL_EXT = "_normal";
    private static final String PRESSED_EXT = "_pressed";
    // How much of a strip's end is held at its own size. The corners of these panels are five or six
    // art pixels across and everything between them is a flat run, so the middle is what stretches
    // when the window is wider or narrower than the art - which it always is, since the bar's own
    // frame was 574 units and this window is as wide as its tabs.
    private static final int PANEL_CAP_UNITS = 8;
    private static final int TAB_CAP_UNITS = 4;
    // The tab's art is its height: a tab drawn taller than this has its gradient pulled with it.
    private static final int TAB_HEIGHT_UNITS = 18;
    // The clear space around a handle's art, which is what makes a 10-unit pill something a thumb
    // can find. The art is drawn at the top-left of the space, so this is also how it is placed.
    private static final int HANDLE_PAD_UNITS = 9;
    // How far the handle's art sits in from the frame, the same distance down from its top edge as in
    // from its right. The two are read together as the margin of the window, and unequal ones look
    // like a button placed against one edge that forgot the other. The grip beside it shares this.
    private static final int HANDLE_INSET_UNITS = 7;
    // The height each of the three rows is asked to be, which is the height of the art they are drawn
    // out of: the plate's top strip with the handle's pill sitting in it, the bar's whole frame, and
    // the band the line is typed into. Each handle's own art is smaller than all three, and the rest
    // of the room is the clear space a thumb needs around it.
    private static final int HEADER_ROW_UNITS = 24;
    private static final int BAR_ROW_UNITS = 31;
    private static final int INPUT_ROW_UNITS = 28;

    private static final HashMap<String, Bitmap> artCache = new HashMap<>();
    private static final Paint SMOOTH = new Paint(Paint.FILTER_BITMAP_FLAG);
    private static final int[] PRESSED_STATE = {android.R.attr.state_pressed};
    private static final int[] ANY_STATE = {};

    private static final class Entry {
        final int category;
        final int color;
        final String text;

        Entry(int c, int col, String t) {
            category = c;
            color = col;
            text = t;
        }
    }

    private static Activity owner;
    private static WindowManager windows;
    // Two windows rather than one. The list and the one-line bar are two different heights, and a
    // window is not allowed to change height when the reader asks for it: the compositor keeps the
    // picture it already has and stretches it into the new bounds until the app has drawn the new
    // size, which is a squeeze lasting two or three frames and reads as an animation. Split apart,
    // each window holds one size for its whole life, so putting the window away is a matter of the
    // compositor's own blending - one frame, and no redraw of either form is even required.
    private static WindowManager.LayoutParams plateParams;
    private static WindowManager.LayoutParams barParams;
    private static Rect screen;
    // The position both windows share, kept in one place because a drag moves the pair of them.
    // Both forms are placed by their top edge, so one coordinate is the whole placement: putting the
    // list away leaves the bar along the top of where the list was.
    private static int positionX;
    private static int positionY;
    // Where the reader left the window, kept over the whole process rather than over one showing. A
    // map change destroys the game's chat bar, which takes these windows with it and brings fresh
    // ones back, so a placement that lived only in the views would be lost on every portal taken.
    private static int savedX = -1;
    private static int savedY = -1;
    // How tall the list is, remembered across showings for the same reason: a window parked at the
    // bottom edge as a bar has to be lifted back where the list fits, and the views handed over to
    // the window manager have no height to measure until they have been laid out once.
    private static int listHeight;
    private static LinearLayout plate;
    private static LinearLayout bar;
    private static TextView barLine;
    private static LinearLayout lines;
    private static ScrollView scroller;
    private static TextView[] tabViews;
    private static EditText chatInput;
    // Whether the keyboard has been seen on screen since it was last asked for. The window learns the
    // keyboard has gone from the same callback that first tells it the keyboard arrived, and until it
    // has arrived there is nothing for it to be gone from.
    private static boolean keyboardUp;
    private static final List<Entry> entries = new ArrayList<>();
    private static final List<View> rowViews = new ArrayList<>();
    // What the reader typed, waiting for the game to come and take it. The window cannot put a line
    // on the wire itself - the connection belongs to the render thread - so the two threads meet
    // over this queue instead of over a call in either direction.
    private static final ArrayDeque<String> OUTGOING = new ArrayDeque<>();
    // Whether a button was pressed since the game last came looking. The sound player belongs to the
    // render thread, so a tap here only marks itself and the click is heard when the game next visits
    // the window. One mark, not a tally: presses made while the game was busy elsewhere would all be
    // played in the same instant, and sounds started together sum into a clipped one.
    private static final Object CLICK_LOCK = new Object();
    private static boolean clickPending = false;

    private static int selectedTab = TAB_ALL;
    private static int maxEntries = 200;
    private static boolean followNewest = true;
    // Out of the way until it is asked for: the window exists to be glanced at while playing, and a
    // reader who wants the list opens it. The placement survives from one showing to the next; this
    // does not.
    private static boolean minimized = true;
    // Whether the window is the reader's or the game's. A window android owns is always composited
    // above the surface the game draws into, so it can never be sent under the interface for real -
    // what going under it comes to is standing out less and giving the finger back, which leaves the
    // game playable around it. The reader puts it back on top by tapping it, and while it is under,
    // the game decides who wins a touch that lands on it: an interface window above the plate keeps
    // the press, and only one the interface does not want reaches the plate.
    private static boolean floating = true;
    // How far the window has come into the world, between nothing and everything. The game's picture
    // is faded by the game; this window is above that picture as far as android is concerned, so the
    // same fade is run here for it - out before the screen goes black for a map, back as it lights up,
    // and up from nothing when the reader first reaches the game.
    private static float fade = 1.0f;
    private static ValueAnimator fader = null;
    // How far out of the way of the interface the window has come, between full strength and the
    // strength it is readable at from underneath. `floating` is where the reader asked it to be and
    // decides at once who wins a touch; this is the blend the eye is shown on the way there, so that
    // going under and coming back are the same kind of event as the window coming and going.
    private static float sink = 1.0f;
    private static ValueAnimator sinker = null;

    /**
     * The plate's frame: the chat bar's top strip, the plain run below it, and its bottom strip.
     *
     * The row a line is typed into is the lowest thing in the window and carries its own rounded band,
     * but the plate behind it used to run down to the bottom edge as a square corner, and that corner
     * showed through the band's own corner cut-offs - the top of the window rounded off and the bottom
     * did not. Held behind the band, the archive's bottom strip is only ever visible at those corners,
     * where it is transparent, which is what makes the plate round off at both ends.
     */
    private static Drawable plateFace() {
        return new Frame(art("view_max_top"), art("view_max_center"), art("view_max_bottom"),
                PANEL_CAP_UNITS);
    }

    /** The bar's frame, which is the same window with its list put away and so has a bottom edge. */
    private static Drawable barFace() {
        return new Frame(art("view_min_top"), art("view_min_center"), art("view_min_bottom"),
                PANEL_CAP_UNITS);
    }

    /** A tab: the face the game drew for the one being looked at, and the dimmed one for the rest. */
    private static Drawable tabFace(boolean selected) {
        StateListDrawable faces = new StateListDrawable();
        Bitmap open = art(selected ? "view_tab_normal" : "view_tab_disabled");
        Bitmap slice = art("view_tab_mouseOver");

        faces.addState(PRESSED_STATE, new Frame(slice, null, null, TAB_CAP_UNITS));
        faces.addState(ANY_STATE, new Frame(open, null, null, TAB_CAP_UNITS));

        return faces;
    }

    // Device pixels per interface unit, handed over by the game so this window sits at the same
    // apparent size as the art it replaces rather than at the system's own metrics.
    private static float unit = 1.0f;

    private NativeChat() {
    }

    /**
     * Creates the window, or moves the existing one. Called from the render thread; everything
     * below {@link #build} runs on the main thread.
     *
     * The frame size comes from the game rather than from the window metrics: this window is placed
     * in the same pixels the game draws into, and the metrics an activity reports are the display's
     * own orientation with the system bars taken off, which is neither that nor stable.
     */
    public static void show(Object activityObject, final float unitPixels, final int x,
                            final int y, final int width, final int height, final int maxLines,
                            final int screenWidth, final int screenHeight) {
        if (!(activityObject instanceof Activity)) {
            return;
        }

        owner = (Activity) activityObject;
        unit = Math.max(0.1f, unitPixels);
        maxEntries = Math.max(32, maxLines);
        screen = new Rect(0, 0, screenWidth, screenHeight);

        // The game's frame is where the window goes when nobody has moved it yet; after that the
        // reader's own placement wins, because the game asks for the same default on every map.
        final int placeX = savedX >= 0 ? savedX : x;
        final int placeY = savedY >= 0 ? savedY : y;
        final Activity target = owner;

        UI.post(() -> build(target, placeX, placeY, width, height));
    }

    public static void hide() {
        UI.post(() -> {
            releaseKeyboard();

            // A fade still running would otherwise be left to write its next value onto windows that
            // are about to be built again for someone else.
            if (fader != null) {
                fader.cancel();
                fader = null;
            }

            if (sinker != null) {
                sinker.cancel();
                sinker = null;
            }

            fade = 1.0f;
            sink = 1.0f;

            if (windows != null) {
                if (plate != null) {
                    windows.removeView(plate);
                }

                if (bar != null) {
                    windows.removeView(bar);
                }
            }

            plate = null;
            bar = null;
            plateParams = null;
            barParams = null;
            windows = null;
            barLine = null;
            lines = null;
            scroller = null;
            tabViews = null;
            chatInput = null;
            rowViews.clear();
            entries.clear();
            selectedTab = TAB_ALL;
            followNewest = true;
            // Back to the way it comes into the world: the reader who opened the list on this map is
            // not necessarily the one who comes back to it.
            minimized = true;
            floating = true;
        });
    }

    /**
     * Whether the window is the game's to be looked at right now. The game fades the picture it draws
     * into when it changes maps and this window is above that picture as far as android is concerned,
     * so the game says here when the window is to go out with the screen and come back with it.
     */
    public static void setFaded(boolean faded) {
        UI.post(() -> fadeTo(faded ? 1.0f : 0.0f));
    }

    /**
     * One line of text in a category, coloured by the game. A line whose category is not the open
     * tab is kept but not displayed, so switching tabs shows what was missed instead of having
     * thrown it away.
     */
    public static void append(final int category, final int color, final String text) {
        if (text == null || text.isEmpty()) {
            return;
        }

        UI.post(() -> {
            Entry entry = new Entry(category, color, text);
            entries.add(entry);

            while (entries.size() > maxEntries) {
                entries.remove(0);
            }

            if (bar == null) {
                return;
            }

            if (minimized) {
                // Put away, the window is one line long, and the line it shows is the newest one the
                // open tab would have drawn. The list behind it keeps filling all the same, so
                // pressing the window back open leaves nothing to catch up on.
                refreshBarLine();
            }

            if (showsNow(category)) {
                addRow(entry);

                while (rowViews.size() > maxEntries) {
                    View oldest = rowViews.remove(0);
                    lines.removeView(oldest);
                }
            }

            if (followNewest) {
                scroller.post(() -> scroller.fullScroll(View.FOCUS_DOWN));
            }
        });
    }

    private static boolean showsNow(int category) {
        return selectedTab == TAB_ALL || category == selectedTab;
    }

    /**
     * How many typed lines are waiting to be put on the wire. Asked by the game once per update, on
     * the render thread, which is also the thread that owns the connection - so a line is sent by
     * the code that sends every other line rather than by whichever thread answered the keyboard.
     */
    public static int pending() {
        synchronized (OUTGOING) {
            return OUTGOING.size();
        }
    }

    /** The oldest typed line, taken out of the queue. Null when nothing is waiting. */
    public static String take() {
        synchronized (OUTGOING) {
            return OUTGOING.poll();
        }
    }

    /**
     * Whether a button has been pressed since this was last asked, taking the mark away with the
     * answer. Asked by the render thread once per update, which is the thread that owns the sounds.
     */
    public static boolean takeClick() {
        synchronized (CLICK_LOCK) {
            boolean pressed = clickPending;
            clickPending = false;

            return pressed;
        }
    }

    private static void click() {
        synchronized (CLICK_LOCK) {
            clickPending = true;
        }
    }

    /**
     * Whether the window sits on top of the interface or out of the way of it. Asked by the render
     * thread whenever the reader's hands go to the game, so the window is the last thing told.
     */
    public static void setFloating(final boolean value) {
        UI.post(() -> {
            if (floating == value) {
                return;
            }

            floating = value;

            if (plate == null || bar == null || windows == null) {
                return;
            }

            applyShown(plate, plateParams, !minimized);
            applyShown(bar, barParams, minimized);

            // The touch goes to the game at once and the strength eases out of the way behind it, so
            // the reader can play immediately without the window blinking off under their hands.
            sinkTo(value ? 1.0f : SUNK_ALPHA);

            if (!value) {
                // Out of the way means the game is being played, and a window that still holds the
                // focus keeps the keyboard up and the controller out of the game's own hands.
                releaseKeyboard();
            }
        });
    }

    /**
     * Whether a press at this position - in interface units, which is the space the game works in -
     * lands on the form the window is wearing. Only ever asked of a window that is out of the way:
     * while the window is on top it takes its own touches and the game is never told that they
     * happened.
     *
     * The placement is read off the main thread's copy of it. Being at most a frame behind costs
     * nothing here - the worst it can do is hand back a press the reader had already moved off of,
     * and the next one is decided from where the window really is.
     */
    public static boolean touched(final int uiX, final int uiY) {
        WindowManager.LayoutParams params = plateParams;
        boolean putAway = minimized;
        View worn = putAway ? bar : plate;

        if (params == null || worn == null) {
            return false;
        }

        int height = worn.getHeight();

        if (!putAway) {
            // The list is the taller of the two forms and, parked against an edge, the window manager
            // clips what it reports back. How much room it needs is the number moveTo already keeps.
            height = Math.max(height, listHeight);
        }

        if (height <= 0) {
            return false;
        }

        float x = uiX * unit - positionX;
        float y = uiY * unit - positionY;

        return x >= 0 && x < params.width && y >= 0 && y < height;
    }

    private static void queue(String text) {
        if (text == null) {
            return;
        }

        String line = text.trim();

        if (line.isEmpty()) {
            return;
        }

        synchronized (OUTGOING) {
            OUTGOING.add(line);
        }
    }

    private static void build(Activity target, int x, int y, int width, int height) {
        if (plate != null) {
            moveTo(x, y);

            return;
        }

        positionX = clamp(x, screen.width() - width);
        positionY = clamp(y, screen.height() - listHeight);

        // A window that has just come into the world is the reader's, not something to be worked
        // around; the game has the same state written down for it, because both are set as it opens.
        // The strength is set with the standing, or a window built while the last one was still on its
        // way under would come in already faded.
        floating = true;
        sink = 1.0f;

        plate = new LinearLayout(target);
        plate.setOrientation(LinearLayout.VERTICAL);
        plate.setBackground(plateFace());
        // The newest line sat flush against the bottom edge of the plate, where the border and the
        // descenders of the lettering ran into each other. The row a line is typed into is the bottom
        // edge now, and its own art carries the border, so the list no longer needs the gap.
        plate.setPadding(0, 0, 0, 0);

        plate.addView(header(target), rowParams(ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout tabStrip = new LinearLayout(target);
        tabStrip.setOrientation(LinearLayout.HORIZONTAL);
        tabViews = new TextView[TABS.length];

        for (int i = 0; i < TABS.length; ++i) {
            TextView label = new TextView(target);
            label.setText(TABS[i]);
            label.setTextSize(TypedValue.COMPLEX_UNIT_PX, pxF(11));
            label.setIncludeFontPadding(false);
            label.setGravity(Gravity.CENTER);
            label.setMinHeight(px(TAB_HEIGHT_UNITS));
            label.setPadding(px(TAB_PAD_UNITS), px(3), px(TAB_PAD_UNITS), px(3));
            label.setTextColor(i == selectedTab ? TAB_LIT : TAB_DIM);
            label.setBackground(tabFace(i == selectedTab));

            final int index = i;

            label.setOnClickListener(v -> selectTab(index));
            tabViews[i] = label;
            tabStrip.addView(label, rowParams(ViewGroup.LayoutParams.WRAP_CONTENT));
        }

        plate.addView(tabStrip, rowParams(ViewGroup.LayoutParams.WRAP_CONTENT));

        lines = new LinearLayout(target);
        lines.setOrientation(LinearLayout.VERTICAL);

        scroller = new ScrollView(target);
        scroller.addView(lines, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        // Touching the list means the reader is looking back, which is exactly when a new line must
        // not yank the view away; returning to the bottom picks the follow back up.
        scroller.setOnScrollChangeListener((v, scrollX, scrollY, oldScrollX, oldScrollY) -> {
            int bottom = lines.getBottom() - v.getHeight();

            followNewest = scrollY >= bottom - px(2);
        });

        LinearLayout.LayoutParams scrollParams = rowParams(ViewGroup.LayoutParams.MATCH_PARENT);
        scrollParams.height = height;
        plate.addView(scroller, scrollParams);

        // What the game's chat bar used to be for: the line the reader sends, typed here so the
        // system's own keyboard, its own spell-checking and its own CJK input method come with it.
        plate.addView(inputRow(target), rowParams(ViewGroup.LayoutParams.MATCH_PARENT));

        // The keyboard put away by hand says nothing to this window: neither the IME's own hide arrow
        // nor the back key reaches the field, so it keeps the caret and the window keeps the focus -
        // and keys, a gamepad among them, belong to whichever window holds the focus, which leaves a
        // game that cannot be steered. The one thing that does change is the window's insets, so the
        // insets are watched and the focus handed back as soon as the keyboard is out of the picture.
        plate.setOnApplyWindowInsetsListener((v, insets) -> {
            if (insets.isVisible(WindowInsets.Type.ime())) {
                keyboardUp = true;
            }
            else if (keyboardUp) {
                keyboardUp = false;
                releaseKeyboard();
            }

            return insets;
        });

        // What the window looks like once it is out of the way: one line of the newest message, as
        // wide as the list itself, with the same two handles on its right.
        bar = buildBar(target);

        // The strip decides where the window ends: the plate is as wide as the tabs, so the frame
        // stands flush with the outer edge of the last tab exactly as it stands flush with the first
        // one on the left. A CJK glyph is as wide as the system font says it is, so the strip is
        // measured rather than worked out from the number of tabs or the size of the lettering.
        tabStrip.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        int formWidth = Math.min(width, tabStrip.getMeasuredWidth());

        windows = target.getWindowManager();
        // Comes in from nothing. The reader reaches the game through the screen's own fade, and a
        // window already at full strength on the other side of it is the one thing that did not fade.
        fade = 0.0f;
        plateParams = formParams(formWidth, !minimized);
        barParams = formParams(formWidth, minimized);
        windows.addView(plate, plateParams);
        windows.addView(bar, barParams);
        fadeTo(1.0f);

        rebuildRows();
        // A line posted before the window existed was only recorded, so both of the two things that
        // show lines have to be brought up to date once they exist: the list by the pass above, and
        // the one-line bar by this one. Without it the bar sits empty over a welcome line the game
        // had already sent, until something else thinks to ask it what the newest line is.
        refreshBarLine();

        // The frame the game asks for is where its own chat bar sat, which is not far enough up for
        // a window this tall: until the first layout there is no height to measure against, so the
        // placement is checked again as soon as the list knows how tall it is.
        plate.post(() -> moveTo(positionX, positionY));
    }

    /**
     * The parameters of one of the two windows. Both are placed at the same point and as wide as the
     * list, so the bar reads as the same window with its list put away rather than as a second one.
     */
    private static WindowManager.LayoutParams formParams(int width, boolean shown) {
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                width,
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);

        params.gravity = Gravity.TOP | Gravity.START;
        params.x = positionX;
        params.y = positionY;
        style(params, shown);

        return params;
    }

    /**
     * How much of this window the reader is meant to be looking at, and whether a finger may reach it.
     * The form that is not worn is blended to nothing and is as far out of the way of a finger as it
     * can be; the one that is worn is either at full strength, in which case it keeps the touches that
     * land on it, or faded, in which case it gives them to the game underneath - which is the only way
     * a window of this kind has ever gone under the interface, since android composites it above the
     * game's surface no matter what the game draws there.
     *
     * The window's own coming and going rides on top of that: a window still on its way in or out is
     * not something to be pressed, whatever its standing relative to the interface is.
     */
    private static void style(WindowManager.LayoutParams params, boolean worn) {
        boolean reachable = worn && floating && fade >= 1.0f;

        params.alpha = worn ? fade * sink : 0.0f;

        if (reachable) {
            params.flags &= ~WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        } else {
            params.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
        }
    }

    /**
     * The grip the chat bar drew in its corner. It is six units of art and does nothing on its own;
     * what it marks is the strip a window is picked up by, and the whole row it sits in takes the
     * press for it.
     */
    private static ImageView grip(Activity target, Runnable tap) {
        ImageView grip = new ImageView(target);
        grip.setScaleType(ImageView.ScaleType.MATRIX);
        grip.setImageDrawable(new Art(art("view_drag")));
        grip.setOnTouchListener(new Handle(tap));

        return grip;
    }

    /**
     * The row across the top: the grip the chat bar had at its corner, which is what a finger picks
     * the window up with, and the button that puts it away.
     *
     * The bar drew no name for this window, so neither does this one - the grip in the corner is the
     * whole of its label, and it says what a reader needs to know about the top of a window: that it
     * moves. The grip is only the width of its own art, but the row it sits in runs the width of the
     * window and takes the press for all of it, which is the room the name used to hold.
     */
    private static LinearLayout header(Activity target) {
        LinearLayout row = new LinearLayout(target);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        // Nothing on the right: that edge is the handle's to place itself against, at the inset the
        // top edge uses too.
        row.setPadding(px(4), 0, 0, 0);

        ImageView handle = grip(target, null);
        LinearLayout.LayoutParams gripParams = rowParams(ViewGroup.LayoutParams.WRAP_CONTENT);
        gripParams.weight = 1;
        gripParams.gravity = Gravity.TOP;
        gripParams.topMargin = px(HANDLE_INSET_UNITS);
        row.addView(handle, gripParams);

        // One button does both jobs a window is picked up for: a finger that travels moves it and a
        // finger that lifts is the press that puts the list away. A separate handle for moving was a
        // second thing to aim at for the same two gestures.
        ImageView collapse = handleButton(target, "BtMin", HEADER_ROW_UNITS);

        collapse.setOnTouchListener(new Handle(() -> setMinimized(true)));
        row.addView(collapse, buttonParams());

        return row;
    }

    /**
     * The window put away. One line, which is what a reader glancing at it while playing can take
     * in, and the one handle it still needs: the button that brings the rest back, which is also
     * where a finger grabs to move it.
     */
    private static LinearLayout buildBar(Activity target) {
        LinearLayout row = new LinearLayout(target);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setBackground(barFace());
        row.setPadding(px(4), 0, 0, 0);

        ImageView handle = grip(target, () -> setMinimized(false));
        LinearLayout.LayoutParams gripParams = rowParams(ViewGroup.LayoutParams.WRAP_CONTENT);
        gripParams.gravity = Gravity.TOP;
        gripParams.topMargin = px(HANDLE_INSET_UNITS);
        row.addView(handle, gripParams);

        barLine = new TextView(target);
        barLine.setTextSize(TypedValue.COMPLEX_UNIT_PX, pxF(11));
        barLine.setIncludeFontPadding(false);
        barLine.setSingleLine(true);
        barLine.setEllipsize(TextUtils.TruncateAt.END);
        barLine.setGravity(Gravity.CENTER_VERTICAL);
        barLine.setPadding(px(6), 0, px(6), 0);
        // The line is the widest thing on the bar and pressing it is what the button beside it does;
        // a finger that travels instead moves the window, which is the other thing one comes for.
        barLine.setOnTouchListener(new Handle(() -> setMinimized(false)));

        LinearLayout.LayoutParams textParams = rowParams(ViewGroup.LayoutParams.WRAP_CONTENT);
        textParams.weight = 1;
        row.addView(barLine, textParams);

        ImageView expand = handleButton(target, "BtMax", BAR_ROW_UNITS);

        expand.setOnTouchListener(new Handle(() -> setMinimized(false)));
        row.addView(expand, buttonParams());

        return row;
    }

    /**
     * The row a line is typed into, along the bottom of the list.
     *
     * A window that takes no focus cannot raise the keyboard, and a window that does takes the
     * keyboard away from the game underneath it - so the flag is flipped for as long as the field
     * holds the caret and put back the moment it lets go. The field asks for it on the touch that
     * lands on it, which is the one moment before the window is asked for focus that there is to ask
     * at.
     */
    private static LinearLayout inputRow(Activity target) {
        LinearLayout row = new LinearLayout(target);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        // The band the bar typed into, which is also the bottom edge of the plate: the frame above it
        // runs down to it and stops, because this art carries its own border and its own corners.
        row.setBackground(new Frame(art("input_layer_backgrnd"), null, null, PANEL_CAP_UNITS));
        row.setPadding(px(3), 0, px(3), 0);

        chatInput = new EditText(target);
        chatInput.setHint("说点什么…");
        chatInput.setTextColor(TAB_LIT);
        chatInput.setHintTextColor(TAB_DIM);
        chatInput.setTextSize(TypedValue.COMPLEX_UNIT_PX, pxF(11));
        chatInput.setIncludeFontPadding(false);
        chatInput.setSingleLine(true);
        chatInput.setInputType(InputType.TYPE_CLASS_TEXT);
        chatInput.setImeOptions(EditorInfo.IME_ACTION_SEND);
        chatInput.setGravity(Gravity.CENTER_VERTICAL);
        chatInput.setMinHeight(px(art("input_layer_chatEnter").getHeight()));
        chatInput.setBackground(new Frame(art("input_layer_chatEnter"), null, null,
                PANEL_CAP_UNITS));
        chatInput.setPadding(px(6), 0, px(6), 0);

        LinearLayout.LayoutParams fieldParams = rowParams(ViewGroup.LayoutParams.WRAP_CONTENT);
        fieldParams.weight = 1;
        row.addView(chatInput, fieldParams);

        // The bubble the bar sent a line with, in place of the word for it: a button this size reads
        // better as a shape than as lettering, and this is the shape the window already used.
        ImageView send = handleButton(target, "input_button_chat", INPUT_ROW_UNITS);

        send.setOnClickListener(v -> submit());

        LinearLayout.LayoutParams sendParams = rowParams(ViewGroup.LayoutParams.WRAP_CONTENT);
        sendParams.setMarginStart(px(3));
        row.addView(send, sendParams);

        chatInput.setOnTouchListener((v, event) -> {
            if (event.getAction() == MotionEvent.ACTION_DOWN) {
                acceptKeyboard();
            }

            return false;
        });

        chatInput.setOnEditorActionListener((v, actionId, event) -> {
            submit();

            return true;
        });

        return row;
    }

    private static void submit() {
        if (chatInput == null) {
            return;
        }

        click();
        queue(chatInput.getText().toString());
        chatInput.setText("");

        // The keyboard goes back down with the line. While this window holds the focus, key events -
        // a controller among them - belong to the text field rather than to the game, so a field left
        // open is a game that cannot be steered; tapping it again is all it takes for the next line.
        releaseKeyboard();
    }

    /**
     * Lets the field the reader is typing into keep the keyboard. The list pans up rather than
     * resizing: a window that changes height is stretched by the compositor out of its last picture
     * until the app draws a new one, which is the squeeze this window was split in two to avoid.
     */
    private static void acceptKeyboard() {
        if (plate == null || plateParams == null || windows == null) {
            return;
        }

        if ((plateParams.flags & WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE) == 0) {
            // The window already holds the focus, so there is nothing left for it to do on this
            // touch and the keyboard has to be asked for here instead.
            plate.post(() -> keyboard().showSoftInput(chatInput, InputMethodManager.SHOW_IMPLICIT));

            return;
        }

        plateParams.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        // Asking for the keyboard here rather than only from the field: the window has just become
        // focusable, so the input connection is not up yet and a show request made in this same turn
        // is one the IME has nothing to answer. The window manager is the one that knows when the
        // window has actually taken focus, so it is told to raise the keyboard at that moment, and
        // the request below is repeated a turn later for the case where the window already had it.
        //
        // Always, not merely when the reader has not put the keyboard away: a keyboard dismissed by
        // hand stays dismissed against a plain request, which leaves a field that holds the caret, a
        // window that holds the focus, and no way to type in it.
        plateParams.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN
                                    | WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE;
        // From this layout onward the keyboard has not yet been seen as up, so the watcher cannot
        // mistake the window manager's answer to this very request for the reader putting it away.
        keyboardUp = false;
        windows.updateViewLayout(plate, plateParams);

        chatInput.requestFocus();

        plate.post(() -> keyboard().showSoftInput(chatInput, InputMethodManager.SHOW_IMPLICIT));
    }

    private static void releaseKeyboard() {
        keyboardUp = false;

        if (chatInput != null) {
            keyboard().hideSoftInputFromWindow(chatInput.getWindowToken(), 0);
            chatInput.clearFocus();
        }

        if (plate == null || plateParams == null || windows == null) {
            return;
        }

        if ((plateParams.flags & WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE) != 0) {
            return;
        }

        plateParams.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        // Out of the picture along with the list, so the next time the field is touched the keyboard
        // has to be asked for again rather than drifting back on its own.
        plateParams.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN
                                    | WindowManager.LayoutParams.SOFT_INPUT_STATE_HIDDEN;
        windows.updateViewLayout(plate, plateParams);
    }

    private static InputMethodManager keyboard() {
        return (InputMethodManager) owner.getSystemService(Activity.INPUT_METHOD_SERVICE);
    }

    /**
     * One of the handles. The button is also the drag handle, which is why it is padded out to
     * something a thumb can find on a screen meant for a thumb: the art is set into the room the row
     * has for it, as near the top of the frame as the bar kept it and no nearer than that.
     *
     * The two forms of the window share their top edge, and the row each handle sits in is as high as
     * the art behind it, so the room below is the only thing that differs between them. The art is
     * therefore held at the same distance from the top in both, the way the grip beside it is: centring
     * it in the row instead would move the "-" and the "+" a unit apart from one another, and the eye
     * catches the handle jumping when the window is put away and brought back.
     *
     * The row's own right padding is nothing and the button's margin nothing, because the space to the
     * right of the art is the inset this window keeps from its edge, and it has to come out the same
     * number as the space above it. Two of the three are enough to place it exactly.
     */
    private static ImageView handleButton(Activity target, String name, int rowUnits) {
        ImageView button = new ImageView(target);
        Bitmap face = art(name + NORMAL_EXT);
        int slack = Math.max(0, rowUnits - face.getHeight());
        int above = Math.min(HANDLE_INSET_UNITS, slack);

        button.setScaleType(ImageView.ScaleType.MATRIX);
        button.setImageDrawable(faces(name));
        button.setPadding(px(HANDLE_PAD_UNITS), px(above), px(HANDLE_INSET_UNITS),
                px(slack - above));

        return button;
    }

    /** A handle is the last thing in its row, at its own width. */
    private static LinearLayout.LayoutParams buttonParams() {
        return rowParams(ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    /**
     * One of the chat bar's own canvases. The archives it came out of are read by the native side, so
     * the pieces this window is drawn from are shipped as loose images beside it; they are decoded
     * once and kept, because a map change rebuilds these windows and the art does not change with it.
     */
    private static Bitmap art(String name) {
        Bitmap canvas = artCache.get(name);

        if (canvas == null) {
            try (InputStream file = owner.getAssets().open(ART_DIR + name + ART_EXT)) {
                canvas = BitmapFactory.decodeStream(file);
            } catch (IOException unreadable) {
                canvas = null;
            }

            if (canvas == null) {
                // An absent canvas draws nothing rather than throwing the window away: a message
                // window with no frame is still a message window, and a null here would take the
                // reader's chat down with it.
                canvas = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888);
            }

            artCache.put(name, canvas);
        }

        return canvas;
    }

    /**
     * One of the game's handles - the pill that puts the list away, the plus that brings it back, the
     * bubble that sends the line - drawn at the size the interface is magnified to and held to the
     * top-left of the room the view gives it, so the padding around it is what places it.
     */
    private static final class Art extends Drawable {
        private final Bitmap image;
        private final Rect target = new Rect();

        Art(Bitmap canvas) {
            image = canvas;
        }

        @Override
        public void draw(Canvas canvas) {
            Rect bounds = getBounds();

            target.set(bounds.left, bounds.top, bounds.left + getIntrinsicWidth(),
                    bounds.top + getIntrinsicHeight());

            canvas.drawBitmap(image, null, target, SMOOTH);
        }

        @Override
        public void setAlpha(int alpha) {
        }

        @Override
        public void setColorFilter(ColorFilter colorFilter) {
        }

        @Override
        public int getOpacity() {
            return PixelFormat.TRANSLUCENT;
        }

        @Override
        public int getIntrinsicWidth() {
            return px(image.getWidth());
        }

        @Override
        public int getIntrinsicHeight() {
            return px(image.getHeight());
        }
    }

    /**
     * A frame drawn out of the chat bar's own strips. Each strip keeps its ends - the rounded corner
     * and the border line run down the last few canvases of it - and the flat run between those ends
     * takes whatever width the window has, which is how the original made one 574-unit-wide archive
     * entry serve a bar of any width. Vertically, the strips above and below keep their own heights
     * and the middle one is the only thing that grows.
     *
     * A frame with one strip and no middle is a plain horizontal slice stretched to the height it is
     * given, which is what a tab and the row a line is typed into are.
     */
    private static final class Frame extends Drawable {
        private final Bitmap top;
        private final Bitmap middle;
        private final Bitmap bottom;
        private final int capUnits;
        private final Rect source = new Rect();
        private final Rect target = new Rect();

        Frame(Bitmap above, Bitmap run, Bitmap below, int cap) {
            top = above;
            middle = run;
            bottom = below;
            capUnits = cap;
        }

        @Override
        public void draw(Canvas canvas) {
            Rect bounds = getBounds();
            int height = bounds.height();

            if (middle == null) {
                strip(canvas, top, bounds, 0, height);

                return;
            }

            int above = px(top.getHeight());
            int below = bottom == null ? 0 : px(bottom.getHeight());
            int run = height - above - below;

            strip(canvas, top, bounds, 0, above);

            if (run > 0) {
                strip(canvas, middle, bounds, above, run);
            }

            if (bottom != null) {
                strip(canvas, bottom, bounds, height - below, below);
            }
        }

        /** One strip, its two ends at their own size and everything between them pulled to fit. */
        private void strip(Canvas canvas, Bitmap strip, Rect bounds, int offset, int height) {
            if (height <= 0) {
                return;
            }

            int cap = Math.min(capUnits, strip.getWidth() / 2);
            int end = px(cap);
            int top = bounds.top + offset;

            source.set(0, 0, cap, strip.getHeight());
            target.set(bounds.left, top, bounds.left + end, top + height);
            canvas.drawBitmap(strip, source, target, SMOOTH);

            source.set(cap, 0, strip.getWidth() - cap, strip.getHeight());
            target.set(bounds.left + end, top, bounds.right - end, top + height);
            canvas.drawBitmap(strip, source, target, SMOOTH);

            source.set(strip.getWidth() - cap, 0, strip.getWidth(), strip.getHeight());
            target.set(bounds.right - end, top, bounds.right, top + height);
            canvas.drawBitmap(strip, source, target, SMOOTH);
        }

        @Override
        public void setAlpha(int alpha) {
        }

        @Override
        public void setColorFilter(ColorFilter colorFilter) {
        }

        @Override
        public int getOpacity() {
            return PixelFormat.TRANSLUCENT;
        }
    }

    /** A handle, wearing the face the game drew for the state it is in. */
    private static Drawable faces(String stem) {
        StateListDrawable drawable = new StateListDrawable();

        drawable.addState(PRESSED_STATE, new Art(art(stem + PRESSED_EXT)));
        drawable.addState(ANY_STATE, new Art(art(stem + NORMAL_EXT)));

        return drawable;
    }

    /**
     * Out of the way means the list is gone and one line of it is left. Neither window changes
     * shape and neither has to draw again: the compositor is told which of the pair to blend in and
     * which to keep clear of a finger, and both halves of that arrive in the same composition, so
     * the switch is one frame with nothing in between.
     */
    private static void setMinimized(boolean value) {
        // Left for the turn after the press that asked for it. The two forms are the same window with
        // its list put away, so the handle being pressed sits at the very point the other form's own
        // handle is; making that form reachable in the middle of dispatching the release that asked
        // for it lets the rest of the same gesture be handed to the button underneath the finger,
        // which is the press that opened the list closing it again before it has been drawn.
        UI.post(() -> {
            if (plate == null || bar == null || minimized == value) {
                return;
            }

            minimized = value;

            applyShown(plate, plateParams, !value);
            applyShown(bar, barParams, value);

            if (value) {
                // The field the keyboard was raised for is going out of the picture with the list.
                releaseKeyboard();
                refreshBarLine();
            } else {
                // The list needs room under it that the bar did not, so the pair is placed again for
                // the taller form. Putting it away does not run this in reverse: the bar stays along
                // the top of where the list was, which is the one position both forms agree on.
                moveTo(positionX, positionY);
            }
        });
    }

    /**
     * Bring the window to or from nothing. The two forms are moved together because they are one
     * window: only the worn one is ever drawn, and the other is already blended to nothing, but both
     * are told so that whichever the reader comes back to is at the strength the moment asks for.
     */
    private static void fadeTo(float target) {
        if (plate == null || bar == null || windows == null || fade == target) {
            return;
        }

        if (fader != null) {
            fader.cancel();
        }

        fader = blend(fade, target, value -> fade = value);
        fader.start();
    }

    /** Ease the window between full strength and the strength it sits out of the way at. */
    private static void sinkTo(float target) {
        if (plate == null || bar == null || windows == null || sink == target) {
            return;
        }

        if (sinker != null) {
            sinker.cancel();
        }

        sinker = blend(sink, target, value -> sink = value);
        sinker.start();
    }

    /**
     * One blend of this window's strength, written by whichever of the two asked for it. What is left
     * of the way is what the rest of it takes, so an order partway through a blend turns around
     * without either slowing down or jumping the remaining distance at once. The two run on their own
     * clocks because they mean different things - one is the window coming and going, the other its
     * way under and back out of the interface - and a reader can order either while the other is still
     * moving.
     */
    private static ValueAnimator blend(float from, float target, Consumer<Float> write) {
        ValueAnimator animator = ValueAnimator.ofFloat(from, target);
        animator.setDuration(Math.round(FADE_MS * Math.abs(target - from)));
        animator.addUpdateListener(animation -> {
            write.accept((float) animation.getAnimatedValue());

            applyShown(plate, plateParams, !minimized);
            applyShown(bar, barParams, minimized);
        });

        return animator;
    }

    /**
     * Whether this window is the form being worn. Both halves of how it should look and behave follow
     * from that and from whether the window is on top of the interface or out of its way, and both
     * arrive in the same composition, so neither one of the pair has to draw again.
     */
    private static void applyShown(View view, WindowManager.LayoutParams params, boolean worn) {
        style(params, worn);

        windows.updateViewLayout(view, params);
    }

    private static void refreshBarLine() {
        if (barLine == null) {
            return;
        }

        for (int i = entries.size() - 1; i >= 0; --i) {
            Entry entry = entries.get(i);

            if (showsNow(entry.category)) {
                barLine.setText(entry.text);
                barLine.setTextColor(entry.color);

                return;
            }
        }

        barLine.setText("");
    }

    /**
     * A window is placed by its layout parameters rather than by the view holding it, so moving
     * means handing the window manager the same parameters back with a new position - for both
     * forms at once, which is what keeps the bar exactly where the list it came out of was.
     */
    private static void moveTo(int x, int y) {
        if (plate == null || plateParams == null || windows == null) {
            return;
        }

        // The form being looked at decides how far down the pair may go, so that the bar can be put
        // against the bottom edge of the screen and the list can be put where it needs no room under
        // it. Opening out runs this again for the list, which is what keeps the list whole.
        LinearLayout shown = minimized ? bar : plate;
        int height = shown.getHeight();

        if (!minimized) {
            // Asked how tall the list is, a clipped window answers with what it was allowed rather
            // than what it needs: parked too low, the window manager cuts it off at the edge of the
            // screen and its height comes back short by exactly the amount that was hanging off.
            // That is the number which would have lifted it back into the picture, so measure the
            // view unconstrained instead and the list stops shrinking itself into the floor.
            //
            // Only the height is left unconstrained. Measuring with no width either, a line that has
            // to wrap is measured as though it had room to run on unbroken, so the window comes out
            // shorter than the list inside it - and the narrower the window, the more often that is.
            plate.measure(View.MeasureSpec.makeMeasureSpec(plateParams.width,
                            View.MeasureSpec.EXACTLY),
                    View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
            height = Math.max(height, plate.getMeasuredHeight());
            listHeight = height;
        }

        positionX = clamp(x, screen.width() - shown.getWidth());
        positionY = clamp(y, screen.height() - height);
        savedX = positionX;
        savedY = positionY;

        plateParams.x = positionX;
        plateParams.y = positionY;
        barParams.x = positionX;
        barParams.y = positionY;

        windows.updateViewLayout(plate, plateParams);
        windows.updateViewLayout(bar, barParams);
    }

    private static int clamp(int value, int high) {
        return Math.max(0, Math.min(value, high));
    }

    /**
     * Whether touches that land outside the window still belong to it. Only ever flipped while a
     * drag has the finger, and only on the form being dragged - the other one takes no touches at
     * all while it is put away.
     */
    private static void setTouchModal(boolean modal) {
        View view = minimized ? bar : plate;
        WindowManager.LayoutParams params = minimized ? barParams : plateParams;

        if (view == null || params == null || windows == null) {
            return;
        }

        if (modal) {
            params.flags &= ~WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
        } else {
            params.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
        }

        windows.updateViewLayout(view, params);
    }

    /**
     * The handle for a drag: a press that travels moves the window, and one that never left the
     * button is the button's own action instead.
     */
    private static final class Handle implements View.OnTouchListener {
        private final Runnable tap;
        // A press has to be allowed to wander this much before it counts as a drag, which is what
        // keeps a finger that slips while tapping from sliding the window away.
        private final float slop = 6 * unit;
        private float downX;
        private float downY;
        private int originX;
        private int originY;
        private boolean dragging;
        // Whether this handle is the one the finger came down on. The other form of the window is
        // sitting under the same point the whole time, and the moment the press puts this form away
        // the window under the finger changes: the rest of that gesture can then be handed to the
        // handle on the other form, which would undo the press that started it.
        private boolean pressed;

        Handle(Runnable tap) {
            this.tap = tap;
        }

        @Override
        public boolean onTouch(View v, MotionEvent event) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    pressed = true;
                    downX = event.getRawX();
                    downY = event.getRawY();
                    originX = positionX;
                    originY = positionY;
                    dragging = false;

                    // The window chases the finger a frame or two behind it, so a grab near an edge
                    // slides off the window almost at once, which Android reads as the gesture
                    // leaving and answers with ACTION_CANCEL. Holding the rest of the screen for the
                    // length of the drag keeps the stream coming; the finger lifting gives it back.
                    setTouchModal(true);

                    return true;
                case MotionEvent.ACTION_MOVE: {
                    if (!pressed) {
                        // Same as below: a move the finger never started here has nothing to be
                        // measured from, and the numbers it would be measured against are a press
                        // this handle never saw.
                        return false;
                    }

                    float dx = event.getRawX() - downX;
                    float dy = event.getRawY() - downY;

                    if (!dragging && Math.abs(dx) < slop && Math.abs(dy) < slop) {
                        return true;
                    }

                    dragging = true;
                    moveTo(Math.round(originX + dx), Math.round(originY + dy));

                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    if (!pressed) {
                        // The finger never came down here, so whatever it was doing belongs to the
                        // other form of the window - which is the form this very press put away.
                        return false;
                    }

                    pressed = false;
                    setTouchModal(false);

                    if (!dragging && tap != null) {
                        click();
                        tap.run();
                    }

                    return true;
                default:
                    return false;
            }
        }
    }

    private static void selectTab(int index) {
        // Before the early return: a tab pressed while it is already the one being read still took a
        // press, and a button that answers only sometimes is one the reader cannot trust.
        click();

        if (index == selectedTab) {
            return;
        }

        selectedTab = index;

        for (int i = 0; i < tabViews.length; ++i) {
            tabViews[i].setTextColor(i == index ? TAB_LIT : TAB_DIM);
            tabViews[i].setBackground(tabFace(i == index));
        }

        followNewest = true;
        rebuildRows();
        refreshBarLine();
    }

    private static void rebuildRows() {
        if (lines == null) {
            return;
        }

        lines.removeAllViews();
        rowViews.clear();

        for (Entry entry : entries) {
            if (showsNow(entry.category)) {
                addRow(entry);
            }
        }

        if (!rowViews.isEmpty()) {
            scroller.post(() -> scroller.fullScroll(View.FOCUS_DOWN));
        }
    }

    private static void addRow(Entry entry) {
        TextView row = new TextView(plate.getContext());
        row.setText(entry.text);
        row.setTextColor(entry.color);
        row.setTextSize(TypedValue.COMPLEX_UNIT_PX, pxF(11));
        row.setIncludeFontPadding(false);
        row.setLineSpacing(px(2), 1.0f);
        row.setPadding(px(8), 0, px(8), 0);

        lines.addView(row, rowParams(ViewGroup.LayoutParams.MATCH_PARENT));
        rowViews.add(row);
    }

    private static LinearLayout.LayoutParams rowParams(int width) {
        return new LinearLayout.LayoutParams(width, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private static int px(float units) {
        return Math.round(units * unit);
    }

    private static float pxF(float units) {
        return units * unit;
    }
}
