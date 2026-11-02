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

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "Forwarder.h"
#include "PacketHandler.h"

namespace ms {
// How much an incoming packet matters, judged from its opcode alone - the only thing the network
// thread is allowed to look at before a handler has run.
enum class PacketWeight : uint8_t {
    // Carries no game state and is answered where it is read, so it never joins the queue the render
    // thread drains. A keepalive that has to wait for a frame is the client telling the server it is
    // not there.
    Keepalive,
    // Cosmetic and self-contained: losing one leaves the world looking stale for a moment rather
    // than wrong. This is a whitelist, deliberately short - a spawn dropped is a hole in the map
    // until the next map change, a removal dropped is a ghost that never leaves, and a removal whose
    // spawn was dropped is a lookup that finds nothing at all. Nothing that carries text belongs
    // here, however throwaway the packet looks: a dropped message is a fact the player never gets.
    Droppable,
    // Everything else, including every opcode nobody has thought about yet.
    Critical
};

// The opcode a decrypted packet carries, or 0xFFFF when it is too short to have one.
uint16_t packet_opcode(const int8_t *bytes, size_t length);
// How much a packet with this opcode matters.
PacketWeight packet_weight(uint16_t opcode);

// Class which contains the array of handler classes to use
class PacketSwitch : public Forwarder {
public:
    // Register all handlers
    PacketSwitch();

    // Forward a packet to the correct handler
    void forward(int8_t *bytes, size_t length) const override;

private:
    // Print a warning
    void warn(std::string_view message, size_t opcode) const;

    // Opcodes for which handlers can be registered
    enum Opcode : uint16_t;

    // Message when an unhandled packet is received
    static constexpr std::string_view MSG_UNHANDLED =
        "Unhandled packet detected";
    // Message when a packet with a larger opcode than the array size is
    // received
    static constexpr std::string_view MSG_OUT_OF_BOUNDS =
        "Large opcode detected";
    // Message when a packet with a larger opcode than the array size is
    // received
    static constexpr std::string_view MSG_REREGISTER =
        "Handler was registered twice";
    // Maximum number of handlers needed
    static constexpr size_t NUM_HANDLERS = 500;

    std::array<std::unique_ptr<PacketHandler>, NUM_HANDLERS> handlers_;

    /**
     * @brief Register a handler for the specified opcode
     *
     * @tparam O opcode
     * @tparam T packet handler
     * @tparam Args types
     * @param args parameter pack
     */
    template<size_t O, typename T, typename... Args>
    void emplace(Args &&... args) {
        static_assert(O < NUM_HANDLERS,
                      "PacketSwitch::emplace - Opcode out of array bounds");
        static_assert(std::is_base_of<PacketHandler, T>::value,
                      "Error: Packet handlers must derive from PacketHandler");

        if (handlers_[O]) {
            warn(MSG_REREGISTER, O);
        }

        handlers_[O] = std::make_unique<T>(std::forward<Args>(args)...);
    }
};
}  // namespace ms