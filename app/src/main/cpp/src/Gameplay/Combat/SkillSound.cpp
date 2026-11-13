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
#include "SkillSound.h"

#include <imgwz/wz.hpp>

namespace ms {
SingleSkillSound::SingleSkillSound(const std::string &strid) {
    img::node soundsrc = img::wz::sound["Skill.img"][strid];

    use_sound_ = soundsrc["Use"];
    hit_sound_ = soundsrc["Hit"];
}

void SingleSkillSound::play_use() {
    use_sound_.play();
}

void SingleSkillSound::play_hit() {
    hit_sound_.play();
}
}  // namespace ms