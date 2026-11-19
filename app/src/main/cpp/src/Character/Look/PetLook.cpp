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
#include "PetLook.h"

#include <imgwz/wz.hpp>
#include <utility>

#include "../../Gameplay/Physics/PhysicsParams.h"

namespace ms {
PetLook::PetLook(int32_t iid,
                 std::string nm,
                 int32_t uqid,
                 Point<int16_t> pos,
                 uint8_t st,
                 int32_t fhid) {
    item_id_ = iid;
    name_ = std::move(nm);
    unique_id_ = uqid;

    set_position(pos.x(), pos.y());
    phobj_.fhid = static_cast<uint16_t>(fhid);
    set_stance(st);

    name_label_ = Text(Text::Font::A13M,
                       Text::Alignment::CENTER,
                       Color::Name::WHITE,
                       Text::Background::NAMETAG,
                       name_);

    std::string strid = std::to_string(iid);
    img::node src = img::wz::item["Pet"][strid + ".img"];

    animations_[Stance::MOVE] = src["move"];
    animations_[Stance::STAND] = src["stand0"];
    animations_[Stance::JUMP] = src["jump"];
    animations_[Stance::ALERT] = src["alert"];
    animations_[Stance::PRONE] = src["prone"];
    animations_[Stance::FLY] = src["fly"];
    animations_[Stance::HANG] = src["hang"];

    img::node effsrc = img::wz::effect["PetEff.img"][strid];

    animations_[Stance::WARP] = effsrc["warp"];
}

PetLook::PetLook() {
    item_id_ = 0;
    name_ = "";
    unique_id_ = 0;
    stance_ = Stance::STAND;
    flip_ = false;
}

void PetLook::draw(double viewx, double viewy, float alpha) const {
    Point<int16_t> absp = phobj_.get_absolute(viewx, viewy, alpha);

    animations_[stance_].draw(DrawArgument(absp, flip_), alpha);
    name_label_.draw(absp);
}

void PetLook::update(const Physics &physics, Point<int16_t> charpos) {
    if (remote_) {
        animations_[stance_].update();
        return;
    }

    // The pet used to be given these two as pushes in pixels per tick squared. Its flight damping
    // has not changed, so that one only has to be restated as the acceleration it amounted to;
    // on the ground it is now driven by the original client's constant acceleration, scaled so
    // that its hard speed cap stays PET_WALK_SPEED.
    const double walkdrive = PhysicsParams::ground_drive(Constants::PET_WALK_SPEED);
    const double flydrive = Constants::PET_FLY_ACCEL;

    Point<int16_t> curpos = phobj_.get_position();

    switch (stance_) {
        case Stance::STAND:
        case Stance::MOVE:
            if (curpos.distance(charpos) > 150) {
                set_position(charpos.x(), charpos.y());
            } else {
                if (charpos.x() - curpos.x() > 50) {
                    phobj_.hforce = walkdrive;
                    flip_ = true;

                    set_stance(Stance::MOVE);
                } else if (charpos.x() - curpos.x() < -50) {
                    phobj_.hforce = -walkdrive;
                    flip_ = false;

                    set_stance(Stance::MOVE);
                } else {
                    phobj_.hforce = 0.0;

                    set_stance(Stance::STAND);
                }
            }

            phobj_.type = PhysicsObject::Type::NORMAL;
            phobj_.clear_flag(PhysicsObject::Flag::NO_GRAVITY);
            break;
        case Stance::HANG:
            set_position(charpos.x(), charpos.y());
            phobj_.set_flag(PhysicsObject::Flag::NO_GRAVITY);
            break;
        case Stance::FLY:
            if ((charpos - curpos).length() > 250) {
                set_position(charpos.x(), charpos.y());
            } else {
                if (charpos.x() - curpos.x() > 50) {
                    phobj_.hforce = flydrive;
                    flip_ = true;
                } else if (charpos.x() - curpos.x() < -50) {
                    phobj_.hforce = -flydrive;
                    flip_ = false;
                } else {
                    phobj_.hforce = 0.0f;
                }

                if (charpos.y() - curpos.y() > 50.0f) {
                    phobj_.vforce = flydrive;
                } else if (charpos.y() - curpos.y() < -50.0f) {
                    phobj_.vforce = -flydrive;
                } else {
                    phobj_.vforce = 0.0f;
                }
            }

            phobj_.type = PhysicsObject::Type::FLYING;
            phobj_.clear_flag(PhysicsObject::Flag::NO_GRAVITY);
            break;
    }

    physics.move_object(phobj_);

    animations_[stance_].update();
}

void PetLook::set_remote() {
    remote_ = true;
    reset_movement();
}

void PetLook::receive_movement(const std::vector<Movement> &movements) {
    if (!remote_ || !item_id_) {
        return;
    }

    for (const auto &movement : movements) {
        if (movement.newstate >= Stance::LENGTH * 2) {
            continue;
        }
        switch (movement.type) {
            case Movement::ABSOLUTE:
            case Movement::JUMPDOWN:
                set_position(movement.xpos, movement.ypos);
                phobj_.fhid = movement.fh;
                break;
            case Movement::CHAIR:
                // Teleport/chair fragments carry a position, but no foothold.
                set_position(movement.xpos, movement.ypos);
                break;
            case Movement::RELATIVE:
                // Relative x/y are velocities, not a new map position.
                break;
            default:
                continue;
        }
        set_stance(movement.newstate);
    }
}

void PetLook::set_position(int16_t x, int16_t y) {
    phobj_.clear_drop();
    phobj_.set_x(x);
    phobj_.set_y(y);
}

void PetLook::set_stance(Stance st) {
    // Char::update derives these overrides from the owner's ladder/swim state.
    if (!remote_ && stance_ != st) {
        stance_ = st;
        animations_[stance_].reset();
    }
}

void PetLook::set_stance(uint8_t stancebyte) {
    flip_ = stancebyte % 2 == 1;
    const Stance st = stancebyvalue(stancebyte);
    if (stance_ != st) {
        stance_ = st;
        animations_[stance_].reset();
    }
}

int32_t PetLook::get_itemid() const {
    return item_id_;
}

PetLook::Stance PetLook::get_stance() const {
    return stance_;
}

int32_t PetLook::get_uniqueid() const {
    return unique_id_;
}

Point<int16_t> PetLook::get_position() const {
    return phobj_.get_position();
}

std::optional<Movement> PetLook::take_movement(bool force) {
    if (remote_ || !item_id_ || !unique_id_) {
        return std::nullopt;
    }
    sync_elapsed_ = std::min(sync_elapsed_ + Constants::TIMESTEP, 1000);
    Movement movement(phobj_, static_cast<uint8_t>(stance_ * 2 + (flip_ ? 1 : 0)));
    // These two fields are speeds in the server's absolute movement format.
    movement.lastx = static_cast<int16_t>(std::clamp(std::round(phobj_.hspeed), -32768.0, 32767.0));
    movement.lasty = static_cast<int16_t>(std::clamp(std::round(phobj_.vspeed), -32768.0, 32767.0));
    movement.duration = static_cast<int16_t>(sync_elapsed_);
    const bool changed = !last_sent_move_ || last_sent_move_->hasmoved(movement)
                         || last_sent_move_->fh != movement.fh;
    if (!force && last_sent_move_ && (sync_elapsed_ < 100 || !changed)) {
        return std::nullopt;
    }
    last_sent_move_ = movement;
    sync_elapsed_ = 0;
    return movement;
}

void PetLook::reset_movement() {
    last_sent_move_.reset();
    sync_elapsed_ = 0;
}

void PetLook::set_excluded_items(std::vector<int32_t> items) {
    excluded_items_ = std::move(items);
}

const std::vector<int32_t> &PetLook::get_excluded_items() const {
    return excluded_items_;
}
}  // namespace ms