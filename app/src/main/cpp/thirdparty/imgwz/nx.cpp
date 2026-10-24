//  imgwz - the predefined category roots.  Each one used to be a whole .nx archive;
//  in a Data tree it is the directory of the same name, so nothing else has to know
//  that the container changed.
#include "nx.hpp"

#include "img_internal.hpp"

#include <string>

namespace nl
{
namespace nx
{
node base, character, effect, etc, item, map, mapPretty, mapLatest, map001, mob, morph,
    npc, quest, reactor, skill, sound, string, tamingmob, ui;

namespace
{
node category(std::string const& data_root, std::string const& name)
{
    auto const* directory = img_dir_for(img_join(data_root, name), nullptr);
    directory->ensure_listed();
    return directory->as_node();
}
} // namespace

void load_all(std::string const& data_root)
{
    std::string const root = data_root;
    auto const at = [&](char const* name) { return category(root, name); };

    base = at("Base");
    character = at("Character");
    effect = at("Effect");
    etc = at("Etc");
    item = at("Item");
    // The map archives the nx setup kept apart (Map, Map001, MapLatest, MapPretty) are
    // one merged directory, so all four names point at it.
    map = at("Map");
    mapPretty = map;
    mapLatest = map;
    map001 = map;
    mob = at("Mob");
    morph = at("Morph");
    npc = at("Npc");
    quest = at("Quest");
    reactor = at("Reactor");
    skill = at("Skill");
    sound = at("Sound");
    string = at("String");
    tamingmob = at("TamingMob");
    ui = at("UI");
}
} // namespace nx
} // namespace nl
