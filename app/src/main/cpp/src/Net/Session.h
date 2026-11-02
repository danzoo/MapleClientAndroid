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

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../Error.h"
#include "../MSClient.h"
#include "Cryptography.h"
#include "Forwarder.h"

#ifdef USE_ASIO
#include "SocketAsio.h"
#include "glfm.h"

#else
#include "SocketWinsock.h"
#endif

namespace ms {
// The connection, on a thread of its own.
//
// Two threads meet here and the split is by ownership, not by locking everything:
//
//   the render thread - everything that was true before this refactor. It calls write() to hand out
//   a packet, read() to take delivery of the ones that arrived since the last frame, and init() and
//   reconnect() to point the session somewhere. Every packet handler, and therefore every change to
//   the UI, the stage and the game state, runs on this thread and inside read(), so none of that
//   needs to be thread-safe.
//
//   the network thread - the socket, the cipher, and the reading of the stream into packets. It owns
//   cryptography_, buffer_ and the framing positions, and touches nothing else. The two queues are
//   the only ground it shares with the render thread, and they are the only thing under the mutex.
//
// What that buys: a connect, a slow server, or a stalled frame no longer costs the other side. The
// attempt and its deadline run without a frame to carry them, so the link comes up while the game is
// backgrounded instead of waiting for the loop to return; and incoming packets are read and decrypted
// as they arrive rather than once per frame, which is what kept a frozen picture from ever noticing
// that the server had gone on talking.
class Session {
public:
    // Where the connection stands. A connection is opened without the loop waiting for it, so none
    // of these except Connected means there is a socket to send on.
    enum class Link {
        // No attempt in flight: nothing has asked for a connection yet, or one was closed.
        Idle,
        // Opening. Packets written now are held for it.
        Connecting,
        // Up, keys read, packets go straight out.
        Connected,
        // The attempt gave up, or a connection that was up died.
        Failed
    };

    Session(std::unique_ptr<Forwarder> packet_forwarder);
    ~Session();

    // Start connecting using the address in hostip.txt and return at once: the attempt runs on the
    // network thread, so this says nothing about whether it succeeds.
    Error init(GLFMDisplay *pApp);
    // Hand a packet to the network thread, or hold it if the connection is still being made. Either
    // thread may call it: the keepalive answer is written from the network thread, everything else
    // from the render thread.
    void write(int8_t *bytes, size_t length);
    // Take the packets that arrived since the last call and give them to the packet handlers.
    void read();
    // Closes the current connection and opens a new one with default connection
    // settings
    void reconnect();
    // Closes the current connection and opens a new one
    void reconnect(const char *address, const char *port);
    // The attempt in progress has given up. The login wait dialog holds on for this instead of for
    // its own timeout, which is much longer.
    bool connect_failed() const;
    // A connection that was up and then died, as distinct from one that never came up: the game can
    // sit out the second and has nothing left to draw through the first.
    bool link_lost() const;

    std::string readHostIP(GLFMDisplay *pApp);

private:
    void connect(const char *host, const char *port);
    // Called off an attempt that is not coming back, so that every path that ends a connection says
    // so once and in the same way. On the network thread.
    void give_up(const char *why);
    // Withdraws both queues and retires the connection number, so that nothing a dead stream already
    // delivered is ever handed to a handler after the attempt that follows it.
    void reset_streams();

    // The three things the network thread has to say. They are called from inside the io service, on
    // the network thread.
    void on_io_handshake(const int8_t *handshake);
    void on_io_bytes(const int8_t *bytes, size_t length);
    // The cipher belongs to a connection, so a packet cannot be framed for the wire before the key
    // arrives. Everything held behind that gate goes out in order when it does.
    void pump_send();

    // The length of the packet that starts here, or 0 if there is nothing to read yet. Consumes the
    // header, including a header that came in pieces. Network thread only.
    bool read_length(const int8_t *&bytes, size_t &available);

    enum class Room {
        // The packet is queued.
        Taken,
        // The packet was refused and something else was kept: either the newcomer itself, judged
        // droppable, or an older one to make room for it.
        Dropped,
        // The queue is full and holds nothing that can be dropped.
        Full
    };

    // Make room for one packet in the inbox, or say that there is none. Network thread only.
    //
    // A full inbox means the render thread has not asked for packets in a while, and the server has
    // not stopped talking. Which packet loses is decided by packet_weight(): a droppable one already
    // queued is the cheapest to forget, and if the incoming packet is itself droppable then it is the
    // one that goes. When nothing in the queue can be dropped and the newcomer must be kept, the
    // inbox is left exactly as it was and this says Full - a connection that cannot carry the packets
    // the game needs is worse than no connection, because it pretends to be working.
    Room queue_incoming(std::vector<int8_t> &&packet, bool droppable);
    // Say that the connection is still there, without waiting for a frame to say it for us.
    void answer_keepalive();

    Cryptography cryptography_;
    // Render thread only, and only from read().
    std::unique_ptr<Forwarder> packet_forwarder_;

    // Network thread only: the packet being assembled out of the stream.
    int8_t buffer_[MAX_PACKET_LENGTH];
    size_t length_;
    size_t pos_;
    // A packet whose four length bytes have not all arrived yet, held until they do.
    int8_t header_[HEADER_LENGTH];
    size_t header_pos_;

    // Read by the render thread, written by the network thread.
    std::atomic<Link> link_;
    // Set the first time a connection comes up and cleared by every deliberate new attempt, so that
    // losing a connection is distinguishable from still making one.
    std::atomic<bool> had_connection_;
    // Every attempt and every death of a connection numbers itself, so a batch being handed to the
    // handlers can tell that a packet in the middle of it pointed the session elsewhere and stop
    // before the rest of a stream that no longer exists is read as game state.
    std::atomic<uint32_t> epoch_;

    // The two queues, and the two strings logged from both sides.
    std::mutex data_mutex_;
    // Packets written before there was a key to frame them with, and packets written while the
    // network thread is busy, in the order they were written. Held unencrypted because the cipher
    // belongs to a connection and its key is only known once that connection's handshake has been
    // read.
    std::deque<std::vector<int8_t>> tx_;
    // Complete packets, decrypted, waiting for a frame to hand them to a handler. The weight is
    // decided when the packet is read, while the network thread still knows why it was dropped.
    struct Incoming {
        std::vector<int8_t> bytes;
        bool droppable;
    };

    std::deque<Incoming> rx_;

    // How many packets the inbox has thrown away since the last connection. Network thread only, and
    // only ever read from that thread's own logging.
    size_t dropped_;

    // The address hostip.txt gave at startup, and the address this session was last pointed at. A
    // bare reconnect() goes back to the first: after a hop to a channel server, that is still the
    // server the login flow belongs to.
    std::string default_host_;
    std::string default_port_;
    // Guarded by data_mutex_: the network thread logs them when an attempt ends.
    std::string host_;
    std::string port_;

#ifdef USE_ASIO
    SocketAsio socket_;
#else
    SocketWinsock socket_;
#endif
    std::thread net_thread_;
};
}  // namespace ms
