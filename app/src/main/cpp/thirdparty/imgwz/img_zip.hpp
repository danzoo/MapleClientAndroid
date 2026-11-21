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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Random access to the release build's STORE-only data.zip. The loose Data tree and the
// zip hold the same relative paths, so lookups keep their loose-style absolute path and
// the prefix given at open() is stripped to reach the entry name.
namespace imgzip {
// Indexes the central directory; every entry must be STORED. Returns false (and reports)
// when the zip is absent or unusable, leaving the loose-tree path in charge.
bool open(std::string const& zip_path, std::string const& root_prefix);

bool active();

// The entry's payload offset and size inside the zip, for a loose-style absolute path.
bool lookup(std::string const& path, std::uint64_t& offset, std::uint32_t& size);

// One level of directory children (name, is_dir) for a loose-style absolute directory
// path, standing in for opendir/readdir which has nothing to read in zip mode.
bool list_children(std::string const& path, std::vector<std::pair<std::string, bool>>& out);

int fd();
}  // namespace imgzip