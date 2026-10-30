//	This file is part of the continued Journey MMORPG client
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton
//
//	This program is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Affero General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Affero General Public License for more details.
//
//	You should have received a copy of the GNU Affero General Public License
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <cmath>
#include <cstdint>

#include "Template/Point.h"
#include "Template/Singleton.h"

namespace ms::Constants {
// Rate the picture is held to, in frames per real second. Rendering only: the simulation below runs
// at a rate of its own and each frame is drawn at the moment it belongs to, so nothing about this
// number changes how fast the game plays.
constexpr double FRAME_RATE = 60;

// Timestep, e.g. the granularity in which the game advances, in milliseconds of game time. This is
// the knob for how fast the game plays: every animation and every timer written in milliseconds
// consumes this much per update.
//
// It is set to 14 because that is the tick Map/Physics.img's own integrator advanced at - see the
// header of PhysicsParams.cpp. Matching the two is what lets the archive's fixed-point force and drag
// figures be read straight through tick_units instead of being converted between two tick lengths.
constexpr uint16_t TIMESTEP = 14;

// Fraction of real time the simulation advances at, so 1.0 is real time. Movement, jumps and
// animations are all driven by the timestep, so scaling it slows them together and keeps every
// trajectory intact; lowering walk speed or jump force directly would shorten the jumps instead.
constexpr double SIMULATION_SPEED = 1.0;

// How many real seconds one update of the simulation lasts. A frame spends as many of these as the
// time since the last frame bought, which is what lets the update rate stand on its own against
// FRAME_RATE. Speeds are in pixels per second and forces in pixels per second squared, so this is
// only ever multiplied against them - by the integrators, and by the archive's own fixed-point
// figures, which are quoted per tick and so come through tick_units.
constexpr double TICK_SECONDS = TIMESTEP * 0.001 / SIMULATION_SPEED;

// Milliseconds of animation a pixel of movement is worth. The walk cycle and everything drawn along
// with it are advanced by ground covered rather than by the clock, and this is the calibration those
// numbers were written against - the artwork is drawn to move one pixel for every 8ms of its own
// timeline - so a walk of 1.75 pixels a tick plays in real time at the current timestep.
//
// It is only that family: an animation advanced by a rate instead of by ground covered - standing,
// idle, an attack, any effect - spends Constants::TIMESTEP per update at rate one, which is what the
// reference client's timestep multiplier meant before the timestep moved off 8.
constexpr float ANIMATION_MS_PER_PIXEL = 8.0f;

// How long loot that a mob dropped stays hidden after its packet arrives, on top of however long
// that mob is still waiting for the blow that killed it. The server sends the loot before it sends
// the kill, so without a wait the items are already flying out of a mob that is still standing.
// Forty updates at the timestep this was written against.
constexpr int16_t DROP_DELAY = 320;

// Longest a mob flinches before the death animation starts, in milliseconds. Only reached if the
// flinch never reports that it has finished playing.
constexpr int16_t HIT_DEATH_WAIT = 320;

// How far short of the end of a platform a knocked-back mob is held. Mobs hang off the point they
// are stored at, so keeping this much clearance stops them from ending up over the gap they would
// fall into.
constexpr int16_t PLATFORM_EDGE_MARGIN = 8;

// Everything below is written for the second rather than for the tick, so that changing TIMESTEP
// re-samples these animations and re-times the physics without changing any of the lengths written
// here. Each one notes the count of updates it was when the tick was 8ms, which is where the
// artwork and the server's timings were calibrated; the millisecond value is that count times 8.

// How long an item's whole flight over level ground lasts, and the half of it spent going up. The
// fall is capped so that an item dropped from a high platform onto a low one falls rather than
// snapping across the gap. The two rises are how far above the ground an item is carried: the toss
// a mob bursts into and the bag throw share the higher one, and being picked up reads as handed
// over rather than thrown again, so it uses the lower one.
constexpr double DROP_FLIGHT_SECONDS = 0.608;
constexpr double DROP_RISE_SECONDS = DROP_FLIGHT_SECONDS / 2.0;
constexpr double DROP_MAX_FALL_SPEED = 375.0;
constexpr double DROP_TOSS_RISE = 56.0;
constexpr double PICKUP_RISE = 42.0;

// Degrees a second the falling icon spins (a fifth of a degree an update), and the idle bob's wave
// (a fortieth). Pixels a second the item is lifted as it leaves for the player's hand (five a tick);
// it is fixated, so that speed is walked at, never accelerated into.
constexpr float DROP_SPIN_DEGREES_PER_SECOND = 25.0f;
constexpr float DROP_BOB_DEGREES_PER_SECOND = 3.125f;
constexpr double PICKUP_LIFT_SPEED = 625.0;

// How long loot keeps the drop sound from re-triggering (fifteen updates), how long a portal waits
// before it can be taken again (forty-eight), and how long another character's buffered movement is
// allowed to lag behind the packets (fifty updates).
constexpr uint16_t DROP_SOUND_COOLDOWN_MS = 120;
constexpr uint16_t PORTAL_COOLDOWN_MS = 384;
constexpr uint16_t OTHERCHAR_BUFFER_MS = 400;

// How long a body that has just leapt off a rope or ladder is kept from grabbing one again. A leap
// leaves along the rope rather than past an end of it, and the key is still held, so without this the
// next frame undoes the jump. Reaching either end of a rope needs no such lock: the grab is measured
// with the same margin as the release, so the rope is out of reach on the frame it lets go. The
// reference client's own second.
constexpr uint16_t CLIMB_COOLDOWN_MS = 1000;

// How long the screen takes to go out and come back wherever the client fades between maps or
// leaves the game (forty updates of a fortieth of the opacity apiece), and how long a mob takes to
// fade in and out for the same reason.
constexpr uint16_t SCREEN_FADE_MS = 320;
constexpr float MOB_FADE_MS = 320.0f;

// How long a mob holds the stance it was given before it thinks of another one (two hundred
// updates), and how much of that wait a blow spends off the end of it (thirty rather than two
// hundred).
constexpr uint16_t MOB_WANDER_MS = 1600;
constexpr uint16_t MOB_HIT_GRACE_MS = 240;

// A bullet is given this long to reach its target, so a fast shot over a short distance and a slow
// one over a long distance both land on the same frame (thirty-two updates). Its speed is only the
// distance divided by that, clamped between these two so that neither a point-blank hit nor a
// cross-map shot reads wrong.
constexpr double BULLET_FLIGHT_MS = 256.0;
constexpr double BULLET_MIN_SPEED = 375.0;
constexpr double BULLET_MAX_SPEED = 750.0;

// How long the character-info window plays its loading picture before showing what it fetched (255
// updates), and how long the login wait holds before deciding the server is not going to answer.
constexpr uint16_t CHAR_INFO_LOADING_MS = 2040;
constexpr uint32_t LOGIN_WAIT_TIMEOUT_MS = 30000;

// How long the notice across the top of the screen stays up when the server asks for a banner but
// gives no length. The banner is no longer something the client holds on its own - it arrives with a
// packet and goes when its time is up - so a packet without a time still has to decide one. This is
// the time a short notice needs to cross the bar once at ScrollingNotice's own speed.
constexpr uint32_t SCROLL_NOTICE_DEFAULT_MS = 30000;

// The interface artwork and the world-map arrow are both fed a fraction of the tick rather than all
// of it, because their art was authored to play slower than the game runs: three quarters of a tick
// for the button and inventory shine and the skill effects, an eighth for the arrow crawling around
// the world map.
constexpr uint16_t REDUCED_ANIMATION_MS = TIMESTEP * 3 / 4;
constexpr uint16_t CRAWLING_ANIMATION_MS = TIMESTEP / 8;

// How much of its opacity a buff icon loses a second once its time is nearly up.
constexpr float BUFF_FADE_PER_SECOND = 6.25f;

// How much of its speed the ground takes off a body per tick. This is the one place the weight of
// the walking feel is set. The archive's walkDrag is 0.8, which is no slide at all: a character
// going full speed reaches it in one tick and, key released, is stopped in three - 14 ms up, 42 ms
// down. The reference client's ground friction (FRICTION 0.5 over GROUNDSLIP 3) sheds a fifth of a
// speed per tick of its own 8 ms, which is 0.35 per tick of fourteen ms - a tenth of a second and
// four pixels of floor at a walk. This runs a little lighter still than the reference, which is what
// reads right on the panel. The drive is rebuilt from walkSpeed under this damping rather than taken
// from walkForce, so the speed you can see stays the archive's and only the ramp that gets there is
// this client's.
constexpr double GROUND_FRICTION = 0.25;

// The shape of the hop a blow knocks the player into, as the two figures that are actually seen: how
// many pixels the body is lifted off the ground, and how many pixels back it travels over it. They
// are heights and distances rather than speeds because the speeds are worked out from them against
// the gravity the map is using - the lift is whatever speed gravity turns into that height, and the
// sideways speed is the distance divided by the airtime that same lift buys - so setting a height
// cannot shorten the hop and setting a distance cannot change the arc. The reference client's own
// figures (a pixel and a half a tick of shove, three and a half a tick squared of lift, at its 8 ms
// tick) are 48 pixels high and 82 across. The mob's knockback is the acceleration the flinch used to
// add every tick - a fifth of a pixel a tick on the ground, a tenth in the air, where nothing can be
// pushed off against. A mob's hop is delivered once, as a speed of five pixels a tick.
constexpr double PLAYER_KNOCKBACK_HEIGHT = 10.0;
constexpr double PLAYER_KNOCKBACK_DISTANCE = 40.0;
constexpr double MOB_KNOCKBACK_ACCEL = 3125.0;
constexpr double MOB_KNOCKBACK_ACCEL_AIR = 1562.5;
constexpr double MOB_HOP_SPEED = 625.0;

// Pixels a second a stat point is worth, for the figures the archives quote as a raw stat rather
// than as a speed. A mob's walk and flight stats are (speed + 100) scaled by these, the rope climb
// one pixel a tick per point of the character's SPEED, and the character-select list scrolls a pixel
// and a quarter an update.
constexpr float MOB_WALK_SPEED_PER_POINT = 0.9375f;
constexpr float MOB_FLY_SPEED_PER_POINT = 1.25f;
constexpr double CLIMB_SPEED_PER_POINT = 125.0;
constexpr double CHAR_SLOT_SCROLL_SPEED = 125.0;

// A pet's own walk and flight drives, and how fast a damage number leaves for the top of the screen
// (a quarter of a pixel a tick). The background scroll is per point of the map's own rx/ry speed.
constexpr double PET_WALK_SPEED = 328.125;
constexpr double PET_FLY_ACCEL = 3125.0;
constexpr double DAMAGE_NUMBER_RISE = 31.25;
constexpr double BACKGROUND_SCROLL_PER_POINT = 7.8125;

// Nothing that is not being pushed keeps going this slowly: below here an object has stopped. The
// archive's minFriction does the same job where a map sets its own friction, and this is the
// client's own restatement for when it does not - the tenth of a pixel a tick it was written at.
constexpr double STOP_SPEED = 12.5;

// How much bigger the interface is drawn than the world around it. Everything the UI lays out is
// measured in a screen that is this much smaller than the real one and then magnified, so raising
// it grows every window, button and piece of text together while the map stays the same size.
constexpr float UI_SCALE = 1.3f;

// The interface is laid out on a screen UI_SCALE smaller than the view, so a placement made
// against the view - a window's stored origin, for instance - has to be brought into that space
// before the interface can use it. Writing it back out goes the other way.
constexpr Point<int16_t> to_ui_space(const Point<int16_t> &viewpos) {
    return Point<int16_t>(static_cast<int16_t>(viewpos.x() / UI_SCALE),
                          static_cast<int16_t>(viewpos.y() / UI_SCALE));
}

constexpr Point<int16_t> to_view_space(const Point<int16_t> &uipos) {
    return Point<int16_t>(static_cast<int16_t>(uipos.x() * UI_SCALE),
                          static_cast<int16_t>(uipos.y() * UI_SCALE));
}

// The bands the game itself measures things in - how close you have to be for an action to take -
// as opposed to how any one input device is driven.
//
// Loot has to be right at the feet. Counted to the nearest edge of the item's picture rather than to
// the point it is stored at, which is what made the reach differ by side.
constexpr int16_t PICKUP_RANGE = 24;
constexpr int16_t TALK_RANGE = 350;

// How wide a band around a rope or ladder still counts as standing at it. The reference client's ten
// pixels are a mouse-width target; on a phone the position is aimed with a joystick, so the grab has
// to forgive noticeably more. Only the grab reads this - once it lands the character is snapped onto
// the rope's own x, so a wide window cannot make it drift.
constexpr int16_t CLIMB_RANGE_X = 16;

// The vertical margin a climb is measured with: both the grab downwards from a platform and the
// release test read this one number, and for those two it cannot be two numbers - a downward grab
// reaches further past the rope's bottom than a release tested with a narrower margin allows, so it
// would be cancelled on the frame it landed and, with the climb cooldown behind it, read as being
// dropped off the rope. A grab may use a narrower margin than this.
constexpr int16_t CLIMB_MARGIN_Y = 8;

// A character in mid-air that drags up past a rope is not aiming at it, and the window above is wide
// enough that every leap across a rope snatched the character onto it. Only that one approach -
// airborne, upwards - keeps the reference client's own bands; the grabs made from a platform, and the
// downward ones, stay as forgiving as a joystick needs. Nothing here may make the vertical reach
// exceed CLIMB_MARGIN_Y, which is what the release test is measured with.
constexpr int16_t CLIMB_RANGE_X_AIR = 10;
constexpr int16_t CLIMB_MARGIN_Y_AIR = 5;

// How far above a rope's top end a platform may sit and still count as the ledge that rope is
// installed on, which is what decides whether the end of an upward climb is the little release-hop
// onto a platform or a rope that stops in mid-air and holds the climber on it. The climb is let go
// one pixel past the reach of the release test, which is measured with CLIMB_MARGIN_Y, and a body
// released that high has to fall onto the platform rather than back down the rope - so the ledge may
// be at most as far above the rope's end as that release is. Measured in the shipped maps, every
// rope there hangs exactly two pixels under the platform it belongs to, well inside this.
constexpr int16_t CLIMB_TOP_MOUNT_Y = CLIMB_MARGIN_Y + 1;

// The size the login and character selection screens are designed around.
constexpr int16_t LOGIN_WIDTH = 800;
constexpr int16_t LOGIN_HEIGHT = 600;

// The shortcut strip along the bottom of the status bar is a single piece of art holding a grid of
// empty cells, and the quick slots are laid into that grid rather than drawn over it. So the bar's
// own art decides where a slot is: the frame lines sit on a 35px pitch both ways and enclose 32x32
// cells, and measured against the point the art is drawn at the first cell begins 12 in from the
// left and 3 down.
//
// Every cell of the art is a slot: 16 columns by 2 rows, and every one of them stands for a virtual
// key id (QuickSlots::keycode) rather than one of the keyboard's function keys. Folded, the strip
// shows only the leftmost ten columns; the fold button opens it to reach the rest, exactly as the
// original bar works.
// The distance from one cell's top-left to the next, horizontally and vertically: a 32x32 picture
// inside a 3px frame line. Hit testing uses this rather than the picture, so a finger on a border
// still belongs to a slot instead of falling between two of them.
constexpr int16_t QUICKSLOT_PITCH = 35;
constexpr int16_t QUICKSLOT_COLS = 16;
// From the point the art is drawn at to the top-left of the first cell.
constexpr Point<int16_t> QUICKSLOT_ART_ORIGIN(12, 3);

// How a controller is read. These are not the game's own bands but one device's translation of a
// player's hand into them, and every number here was measured off the device rather than taken from
// a specification.
//
// How far from its centre a thumbstick has to be travelled, as a fraction of its own range, before
// it does anything at all. Travel rather than the two axes separately, so pushing diagonally asks
// for the same effort as pushing straight.
//
// It is high because the axis is not a straight reading of the stick: the driver applies its own
// curve on the way, and on the device this was measured on half the axis range was already spent by
// the time the stick had moved a fifth of its physical throw. A threshold that reads as a proper
// deadzone on one axis is therefore crossed before the hand has noticed anything on another. At rest
// the axes read a thousandth, and full deflection reads one, so the whole range is usable.
constexpr double STICK_DEADZONE = 0.75;

// A direction being held doesn't need the stick to stay out at the deadzone edge, since a stick held
// still reports nothing and axes jitter by a percent or two: it is kept down to here, but only while
// the stick still points the same way. Half the engage point rather than a number of its own, so the
// band a direction carries through stays the same share of the stick's travel as the deadzone grows.
constexpr double STICK_RETURN = STICK_DEADZONE * 0.5;

// Around the diagonal the axis that already owns the direction keeps owning it, so a stick parked
// near forty-five degrees doesn't flicker between the two keys as the axes jitter.
constexpr double STICK_AXIS_MARGIN = 0.06;

// No disconnect event reaches the app, so a controller is forgotten once it stays quiet. A stick or
// button held at a constant reading reports nothing either, which is why holding on past this point
// isn't taken for the controller having gone away while it is still being used.
constexpr uint16_t GAMEPAD_CONNECT_TIMEOUT_MS = 5000;

// A controller gives one press for a held input, and the client has no key repeat of its own, so
// anything that has to go on while something is held is renewed on these clocks instead. A keyboard
// arrow hides the same hole because Android repeats it.
//
// Walking is renewed because a press that lands while the character can't take it - mid attack,
// before the map has gone active - would otherwise be lost until the stick was centred and pushed
// again. Attacking because it is what a controller is actually held down for. Picking up is fast
// because loot is picked one item at a time and a pile is worked through. Talking is slow because an
// answer arrives from the server and a second request while the first is outstanding reads as a
// stutter.
constexpr uint16_t GAMEPAD_WALK_REPEAT_MS = 100;
constexpr uint16_t GAMEPAD_ATTACK_REPEAT_MS = 250;
constexpr uint16_t GAMEPAD_PICKUP_REPEAT_MS = 100;
constexpr uint16_t GAMEPAD_TALK_REPEAT_MS = 500;

class Constants : public Singleton<Constants> {
public:
    Constants() : VIEWWIDTH(1280), VIEWHEIGHT(720) {}

    ~Constants() override = default;

    int16_t get_viewwidth() { return VIEWWIDTH; }

    void set_viewwidth(int16_t width) { VIEWWIDTH = width; }

    int16_t get_viewheight() { return VIEWHEIGHT; }

    void set_viewheight(int16_t height) { VIEWHEIGHT = height; }

    // The size of the screen the interface is laid out on. Positions and sizes given in this
    // space are magnified by UI_SCALE on their way to the frame, so a window that has to reach an
    // edge has to be anchored in here rather than in the view size. Rounded up so that an element
    // which is made to fill this screen still covers the view once it is magnified.
    int16_t get_ui_viewwidth() { return static_cast<int16_t>(std::ceil(VIEWWIDTH / UI_SCALE)); }

    int16_t get_ui_viewheight() { return static_cast<int16_t>(std::ceil(VIEWHEIGHT / UI_SCALE)); }

private:
    // Window and screen width.
    int16_t VIEWWIDTH;
    // Window and screen height.
    int16_t VIEWHEIGHT;
};
}  // namespace ms::Constants