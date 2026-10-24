//  imgwz - the predefined category roots, same names as NoLifeNx's nl::nx.
#pragma once
#include "nxfwd.hpp"

#include <string>

namespace nl
{
namespace nx
{
//! Pre-defined nodes to access standard MapleStory-style data.
//! Make sure you called `load_all()` first.  The three map archives the nx setup
//! had (Map, Map001, MapLatest, MapPretty) are one merged `Map` category in a
//! Data tree, so those four names are the same directory.
extern node base, character, effect, etc, item, map, mapPretty, mapLatest, map001, mob, morph, npc, quest, reactor, skill, sound, string, tamingmob, ui;
//! Points the roots at the category directories of `data_root`, a client-style
//! Data directory full of loose .img files.
//! Only call this function once.
void load_all(std::string const& data_root);
} // namespace nx
} // namespace nl
