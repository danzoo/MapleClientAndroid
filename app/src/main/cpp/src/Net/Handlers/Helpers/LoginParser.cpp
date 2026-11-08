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
#include "LoginParser.h"

namespace ms {
Account LoginParser::parse_account(InPacket &recv) {
    Account account;

    recv.skip_short();

    account.accid = recv.read_int();
    account.female = recv.read_byte();
    account.admin = recv.read_bool();

    recv.skip_byte();  // Admin
    recv.skip_byte();  // Country Code

    account.name = recv.read_string();

    recv.skip_byte();

    account.muted = recv.read_bool();

    recv.skip_long();  // muted until
    recv.skip_long();  // creation date

    recv.skip_int();  // Remove "Select the world you want to play in"

    account.pin = recv.read_bool();  // 0 - Enabled, 1 - Disabled
    account.pic = recv.read_byte();  // 0 - Register, 1 - Ask, 2 - Disabled

    return account;
}

World LoginParser::parse_world(InPacket &recv) {
    int8_t wid = recv.read_byte();

    if (wid == -1) {
        return { {}, {}, {}, 0, 0, wid };
    }

    std::string name = recv.read_string();
    uint8_t flag = recv.read_ubyte();
    std::string message = recv.read_string();

    recv.skip(5);

    std::vector<int32_t> chloads;
    uint8_t channelcount = recv.read_ubyte();

    for (int i = 0; i < channelcount; ++i) {
        recv.read_string();  // channel name

        chloads.push_back(recv.read_int());

        recv.skip(1);
        recv.skip(2);
    }

    recv.skip(2);  // balloon size
    // recv.skip(10);  // balloon size 1 and msg "test"

    return { name, message, chloads, channelcount, flag, wid };
}

RecommendedWorld LoginParser::parse_recommended_world(InPacket &recv) {
    int32_t wid = recv.read_int();

    if (wid == -1) {
        return { {}, wid };
    }

    std::string message = recv.read_string();

    return { message, wid };
}

CharEntry LoginParser::parse_charentry(InPacket &recv) {
    int32_t cid = recv.read_int();
    StatsEntry stats = parse_stats(recv);
    LookEntry look = parse_look(recv);

    recv.read_bool();  // 'rankinfo' bool

    if (recv.read_bool()) {
        int32_t currank = recv.read_int();
        int32_t rankmv = recv.read_int();
        int32_t curjobrank = recv.read_int();
        int32_t jobrankmv = recv.read_int();
        int8_t rankmc = (rankmv > 0) ? '+' : (rankmv < 0) ? '-' : '=';
        int8_t jobrankmc = (jobrankmv > 0) ? '+' : (jobrankmv < 0) ? '-' : '=';

        stats.rank = std::make_pair(currank, rankmc);
        stats.jobrank = std::make_pair(curjobrank, jobrankmc);
    }

    return { stats, look, cid };
}

StatsEntry LoginParser::parse_stats(InPacket &recv) {
    // TODO: This is similar to CashShopParser.cpp, try and merge these.
    StatsEntry statsentry;

    statsentry.name = recv.read_padded_string(13);
    statsentry.female = recv.read_bool();

    recv.read_byte();  // skin
    recv.read_int();   // face
    recv.read_int();   // hair

    for (int i = 0; i < 3; i++) {
        statsentry.petids.push_back(recv.read_long());
    }

    statsentry.stats[MapleStat::Id::LEVEL] =
        recv.read_ubyte();  // TODO: Change to recv.read_short(); to increase
                            // level cap
    statsentry.stats[MapleStat::Id::JOB] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::STR] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::DEX] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::INT] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::LUK] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::HP] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::MAXHP] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::MP] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::MAXMP] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::AP] = recv.read_ushort();
    statsentry.stats[MapleStat::Id::SP] = recv.read_ushort();
    statsentry.exp = recv.read_uint();
    statsentry.stats[MapleStat::Id::FAME] = recv.read_short();

    recv.skip(4);  // gachaexp

    statsentry.mapid = recv.read_int();
    statsentry.portal = recv.read_ubyte();

    recv.skip(4);  // timestamp

    return statsentry;
}

LookEntry LoginParser::parse_look(InPacket &recv) {
    LookEntry look;

    look.female = recv.read_bool();
    look.skin = recv.read_ubyte();
    look.faceid = recv.read_int();

    recv.read_bool();  // megaphone

    look.hairid = recv.read_int();

    uint8_t eqslot = recv.read_ubyte();

    while (eqslot != 0xFF) {
        look.equips[eqslot] = recv.read_int();
        eqslot = recv.read_ubyte();
    }

    uint8_t mskeqslot = recv.read_ubyte();

    while (mskeqslot != 0xFF) {
        look.maskedequips[mskeqslot] = recv.read_int();
        mskeqslot = recv.read_ubyte();
    }

    look.maskedequips[-111] = recv.read_int();

    for (int i = 0; i < 3; i++) {
        look.petids.push_back(recv.read_int());
    }

    return look;
}

std::pair<std::string, std::string> LoginParser::parse_login(InPacket &recv) {
    recv.skip_byte();

    // v83 reserves four bytes for a raw IPv4 address. The server uses 0.0.0.0 as an escape marker
    // when the channel endpoint is IPv6 (an address that is never a usable redirect target) and
    // follows it with a length-prefixed host string. Reading the four bytes first keeps the IPv4
    // wire format exactly as it was, so an older server still works with this client.
    uint8_t octets[4];

    for (auto &octet : octets) {
        octet = static_cast<uint8_t>(recv.read_byte());
    }

    const bool is_ipv6 = (octets[0] | octets[1] | octets[2] | octets[3]) == 0;

    std::string addrstr;

    if (is_ipv6) {
        addrstr = recv.read_string();
    } else {
        for (size_t i = 0; i < 4; i++) {
            addrstr.append(std::to_string(octets[i]));

            if (i < 3) {
                addrstr.push_back('.');
            }
        }
    }

    // The port is an unsigned 16-bit little-endian value on the wire. Reading it as a signed short
    // used to turn any port at or above 32768 into a negative (and therefore unconnectable) string.
    std::string portstr = std::to_string(recv.read_ushort());

    return { addrstr.c_str(), portstr.c_str() };
}
}  // namespace ms