//  imgwz - the predefined category roots, the same set the archive reader exposed.
#pragma once
#include "fwd.hpp"

#include <string>

namespace img
{
namespace wz
{
//! Pre-defined nodes to access standard MapleStory-style data.
//! Make sure you called `load_all()` first.  The three map archives the old reader
//! had (Map, Map001, MapLatest, MapPretty) are one merged `Map` category in a
//! Data tree, so those four names are the same directory.
extern node base, character, effect, etc, item, map, mapPretty, mapLatest, map001, mob, morph, npc, quest, reactor, skill, sound, string, tamingmob, ui;
//! Points the roots at the category directories of `data_root`, a client-style
//! Data directory full of loose .img files.
//! Only call this function once.
void load_all(std::string const& data_root);
} // namespace wz
} // namespace img
