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
#include "PlayerStates.h"

namespace ms {
// Base class
void PlayerState::play_jumpsound() const {
    Sound(Sound::JUMP).play();
}

// Null state
void PlayerNullState::update_state(Player &player) const {
    Char::State state;

    if (player.get_phobj().onground) {
        if (player.is_key_down(KeyAction::Id::LEFT)) {
            state = Char::State::WALK;
            player.set_direction(false);
        } else if (player.is_key_down(KeyAction::Id::RIGHT)) {
            state = Char::State::WALK;
            player.set_direction(true);
        } else if (player.is_key_down(KeyAction::Id::DOWN)) {
            state = Char::State::PRONE;
        } else {
            state = Char::State::STAND;
        }
    } else {
        auto ladder = player.get_ladder();

        if (ladder) {
            state = ladder->get().is_ladder() ? Char::State::LADDER
                                              : Char::State::ROPE;
        } else {
            state = Char::State::FALL;
        }
    }

    player.get_phobj().clear_flags();

    player.set_state(state);
}

// Standing
void PlayerStandState::initialize(Player &player) const {
    player.get_phobj().type = PhysicsObject::Type::NORMAL;
}

void PlayerStandState::send_action(Player &player,
                                   KeyAction::Id ka,
                                   bool down) const {
    if (player.is_attacking()) {
        return;
    }

    if (down) {
        switch (ka) {
            case KeyAction::Id::LEFT:
                player.set_direction(false);
                player.set_state(Char::State::WALK);
                break;
            case KeyAction::Id::RIGHT:
                player.set_direction(true);
                player.set_state(Char::State::WALK);
                break;
            case KeyAction::Id::JUMP:
                play_jumpsound();
                player.get_phobj().vforce = -player.get_jumpforce();
                break;
            case KeyAction::Id::DOWN:
                player.set_state(Char::State::PRONE);
                break;
            default:
                break;
        }
    }
}

void PlayerStandState::update(Player &player) const {
    if (player.get_phobj().enablejd == false) {
        player.get_phobj().set_flag(PhysicsObject::Flag::CHECK_BELOW);
    }
}

void PlayerStandState::update_state(Player &player) const {
    if (!player.get_phobj().onground) {
        player.set_state(Char::State::FALL);
    }
}

// Walking
void PlayerWalkState::initialize(Player &player) const {
    player.get_phobj().type = PhysicsObject::Type::NORMAL;
}

void PlayerWalkState::send_action(Player &player,
                                  KeyAction::Id ka,
                                  bool down) const {
    if (player.is_attacking()) {
        return;
    }

    if (down) {
        switch (ka) {
            case KeyAction::Id::LEFT: player.set_direction(false); break;
            case KeyAction::Id::RIGHT: player.set_direction(true); break;
            case KeyAction::Id::JUMP:
                play_jumpsound();
                player.get_phobj().vforce = -player.get_jumpforce();
                break;
            case KeyAction::Id::DOWN:
                player.set_state(Char::State::PRONE);
                break;
            default:
                break;
        }
    }
}

bool PlayerWalkState::haswalkinput(const Player &player) const {
    return player.is_key_down(KeyAction::Id::LEFT)
           || player.is_key_down(KeyAction::Id::RIGHT);
}

void PlayerWalkState::update(Player &player) const {
    if (!player.is_attacking() && haswalkinput(player)) {
        player.get_phobj().hforce +=
            player.getflip() ? player.get_walkforce() : -player.get_walkforce();
    }

    if (player.get_phobj().enablejd == false) {
        player.get_phobj().set_flag(PhysicsObject::Flag::CHECK_BELOW);
    }
}

void PlayerWalkState::update_state(Player &player) const {
    if (player.get_phobj().onground) {
        if (!haswalkinput(player) || player.get_phobj().hspeed == 0.0f) {
            player.set_state(Char::State::STAND);
        }
    } else {
        player.set_state(Char::State::FALL);
    }
}

// Falling
void PlayerFallState::initialize(Player &player) const {
    player.get_phobj().type = PhysicsObject::Type::NORMAL;
}

void PlayerFallState::send_action(Player &player,
                                  KeyAction::Id ka,
                                  bool down) const {
    if (down) {
        switch (ka) {
            case KeyAction::Id::LEFT: player.set_direction(false); break;
            case KeyAction::Id::RIGHT: player.set_direction(true); break;
            default: break;
        }
    }
}

void PlayerFallState::update(Player &player) const {
    // While airborne the horizontal force is discarded by move_normal, but on the landing frame the
    // character is still in FALL when physics runs, so ground friction bleeds speed with no walk
    // force to offset it. Over repeated jump-while-walking cycles that accumulates into a visible
    // slowdown. Re-applying the walk force here cancels the friction on the touch-down frame, so
    // jump-moving holds the same steady speed as walking.
    if (!player.is_attacking()
        && (player.is_key_down(KeyAction::Id::LEFT)
            || player.is_key_down(KeyAction::Id::RIGHT))) {
        player.get_phobj().hforce +=
            player.getflip() ? player.get_walkforce() : -player.get_walkforce();
    }
}

void PlayerFallState::update_state(Player &player) const {
    if (player.get_phobj().onground) {
        if (player.is_key_down(KeyAction::Id::LEFT)) {
            player.set_direction(false);
            player.set_state(Char::State::WALK);
        } else if (player.is_key_down(KeyAction::Id::RIGHT)) {
            player.set_direction(true);
            player.set_state(Char::State::WALK);
        } else if (player.is_key_down(KeyAction::Id::DOWN)) {
            player.set_state(Char::State::PRONE);
        } else {
            player.set_state(Char::State::STAND);
        }

        // Holding the jump key generates a single press event, so the impulse has to be renewed
        // every time the character touches the ground or the jumping stops after one jump.
        if (!player.is_key_down(KeyAction::Id::DOWN)
            && player.is_key_down(KeyAction::Id::JUMP)
            && !player.is_attacking()) {
            play_jumpsound();
            player.get_phobj().vforce = -player.get_jumpforce();
        }
    } else if (player.is_underwater()) {
        player.set_state(Char::State::SWIM);
    }
}

// Prone
void PlayerProneState::send_action(Player &player,
                                   KeyAction::Id ka,
                                   bool down) const {
    if (down) {
        switch (ka) {
            case KeyAction::Id::JUMP:
                if (player.get_phobj().enablejd
                    && player.is_key_down(KeyAction::Id::DOWN)) {
                    play_jumpsound();
                    player.get_phobj().y = player.get_phobj().groundbelow;
                    player.set_state(Char::State::FALL);
                }

                break;
        }
    } else {
        switch (ka) {
            case KeyAction::Id::DOWN:
                player.set_state(Char::State::STAND);
                break;
            default: break;
        }
    }
}

void PlayerProneState::update(Player &player) const {
    if (player.get_phobj().enablejd == false) {
        player.get_phobj().set_flag(PhysicsObject::Flag::CHECK_BELOW);
    }
}

// Sitting
void PlayerSitState::send_action(Player &player,
                                 KeyAction::Id ka,
                                 bool down) const {
    if (down) {
        switch (ka) {
            case KeyAction::Id::LEFT:
                player.set_direction(false);
                player.set_state(Char::State::WALK);
                break;
            case KeyAction::Id::RIGHT:
                player.set_direction(true);
                player.set_state(Char::State::WALK);
                break;
            case KeyAction::Id::JUMP:
                play_jumpsound();
                player.set_state(Char::State::STAND);
                break;
            case KeyAction::Id::UP: player.set_state(Char::State::STAND); break;
        }
    }
}

// Flying
void PlayerFlyState::initialize(Player &player) const {
    player.get_phobj().type = player.is_underwater()
                                  ? PhysicsObject::Type::SWIMMING
                                  : PhysicsObject::Type::FLYING;
}

void PlayerFlyState::send_action(Player &player,
                                 KeyAction::Id ka,
                                 bool down) const {
    if (down) {
        switch (ka) {
            case KeyAction::Id::LEFT: player.set_direction(false); break;
            case KeyAction::Id::RIGHT: player.set_direction(true); break;
        }
    }
}

void PlayerFlyState::update(Player &player) const {
    if (player.is_attacking()) {
        return;
    }

    // A stroke and a flight are two different pushes in the archive, even though the numbers it
    // gives them happen to be the same; water damps and caps them differently again.
    const double drive = player.is_underwater() ? player.get_swimforce()
                                                 : player.get_flyforce();

    if (player.is_key_down(KeyAction::Id::LEFT)) {
        player.get_phobj().hforce = -drive;
    } else if (player.is_key_down(KeyAction::Id::RIGHT)) {
        player.get_phobj().hforce = drive;
    }

    if (player.is_key_down(KeyAction::Id::UP)) {
        player.get_phobj().vforce = -drive;
    } else if (player.is_key_down(KeyAction::Id::DOWN)) {
        player.get_phobj().vforce = drive;
    }
}

void PlayerFlyState::update_state(Player &player) const {
    if (player.get_phobj().onground && player.is_underwater()) {
        Char::State state;

        if (player.is_key_down(KeyAction::Id::LEFT)) {
            state = Char::State::WALK;
            player.set_direction(false);
        } else if (player.is_key_down(KeyAction::Id::RIGHT)) {
            state = Char::State::WALK;
            player.set_direction(true);
        } else if (player.is_key_down(KeyAction::Id::DOWN)) {
            state = Char::State::PRONE;
        } else {
            state = Char::State::STAND;
        }

        player.set_state(state);
    }
}

// Climbing
void PlayerClimbState::initialize(Player &player) const {
    player.get_phobj().type = PhysicsObject::Type::FIXATED;
}

void PlayerClimbState::send_action(Player &player,
                                   KeyAction::Id ka,
                                   bool down) const {
    if (down) {
        switch (ka) {
            case KeyAction::Id::JUMP:
                if (player.is_key_down(KeyAction::Id::LEFT)) {
                    play_jumpsound();
                    player.set_direction(false);
                    // Leaping off a ladder throws the character sideways at the speed it walks at,
                    // and two thirds of the way it jumps.
                    player.get_phobj().hspeed = -player.get_walkspeed();
                    player.get_phobj().vspeed = -player.get_jumpspeed() / 1.5;
                    cancel_ladder(player, true);
                } else if (player.is_key_down(KeyAction::Id::RIGHT)) {
                    play_jumpsound();
                    player.set_direction(true);
                    player.get_phobj().hspeed = player.get_walkspeed();
                    player.get_phobj().vspeed = -player.get_jumpspeed() / 1.5;
                    cancel_ladder(player, true);
                }

                break;
        }
    }
}

void PlayerClimbState::update(Player &player) const {
    if (player.is_key_down(KeyAction::Id::UP)) {
        player.get_phobj().vspeed = -player.get_climbspeed();
    } else if (player.is_key_down(KeyAction::Id::DOWN)) {
        player.get_phobj().vspeed = player.get_climbspeed();
    } else {
        player.get_phobj().vspeed = 0.0;
    }
}

void PlayerClimbState::update_state(Player &player) const {
    int16_t y = player.get_phobj().get_y();
    bool downwards = player.is_key_down(KeyAction::Id::DOWN);
    auto ladder = player.get_ladder();

    if (ladder && ladder->get().felloff(y, downwards)) {
        // Neither end of a rope needs the cooldown, because neither can take itself back on: coming
        // off over the top is the end of a climb, and the character has reached the platform it
        // climbed to, while coming off at the bottom leaves the body further below the rope than a
        // grab measured with the same margin as the release can reach. The rope stays one drag away
        // in either case, which is what a touch player expects from a rope it has just ridden. Only
        // a leap needs the cooldown, since that leaves along the rope instead of past its end.
        cancel_ladder(player, false);
    }
}

void PlayerClimbState::cancel_ladder(Player &player, bool cooldown) const {
    player.set_state(Char::State::FALL);
    player.set_ladder({});

    if (!cooldown) {
        return;
    }

    // Leaping off a rope is followed by a moment in which no rope can be grabbed. The key that held
    // this one is still down, and the ladder check now runs every frame it is held, so without this
    // the leap - which leaves along the rope rather than past an end of it - is undone by the next
    // frame's grab.
    player.set_climb_cooldown();
}
}  // namespace ms