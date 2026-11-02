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
#include "Session.h"

#include <android/log.h>
#include <algorithm>
#include <cstring>

#include "../Configuration.h"
#include "../Constants.h"
#include "PacketError.h"
#include "PacketSwitch.h"
#include "Packets/CommonPackets.h"

namespace ms {
namespace {
// How many packets will be held for a connect that has not come up. Past this the queue has stopped
// being a buffer and become a symptom: whoever is writing is doing it faster than a connection that
// does not exist could ever take the packets, so the surplus is dropped rather than remembered.
constexpr size_t OUTBOX_LIMIT = 64;

// How far a render loop that stopped calling read() may fall behind the stream before the packets are
// no longer worth having. It is generous: a game that cannot take a thousand packets is not going to
// be rescued by a queue, and the alternative - keeping the server's whole output - is the thing that
// gets the process killed.
constexpr size_t INBOX_LIMIT = 1024;
}  // namespace

Session::Session(std::unique_ptr<Forwarder> packet_forwarder) :
    packet_forwarder_(std::move(packet_forwarder)),
    length_(0),
    pos_(0),
    header_pos_(0),
    link_(Link::Idle),
    had_connection_(false),
    epoch_(0),
    dropped_(0) {
    socket_.set_handlers(
        [this](const int8_t *handshake) { on_io_handshake(handshake); },
        [this](const int8_t *bytes, size_t length) { on_io_bytes(bytes, length); },
        [this](const char *why) { give_up(why); });

    // The io service is only ever run from here, which is what lets the socket, the cipher and the
    // framing go without a lock. The work guard holds the service open, so the thread waits on the
    // socket instead of returning the first time the stream goes quiet.
    net_thread_ = std::thread([this] { socket_.run(); });
}

Session::~Session() {
    // Stops the service and then waits for the thread, so that nothing owns the socket by the time
    // this object's members go away. A handler that is mid-flight is allowed to finish; one that has
    // not started never will.
    socket_.stop();
    if (net_thread_.joinable()) {
        net_thread_.join();
    }
}

std::string Session::readHostIP(GLFMDisplay *pApp) {
    std::string filePath = std::string(((ANativeActivity*)glfmGetAndroidActivity(pApp))->externalDataPath) + "/hostip.txt";
    FILE* file = fopen(filePath.c_str(), "rb");
    std::string hostIP;

    if (file != nullptr) {
        char buffer[256]; // Buffer to read data
        size_t bytesRead = fread(buffer, 1, sizeof(buffer), file);
        fclose(file);

        // Ensure the read operation succeeded
        if (bytesRead > 0) {
            // Convert the buffer to a string
            hostIP.assign(buffer, bytesRead);
        }
    }

    // The file is as likely to be written by hand as by the app, and a trailing newline would
    // otherwise become part of the address.
    while (!hostIP.empty() && static_cast<unsigned char>(hostIP.back()) <= ' ') {
        hostIP.pop_back();
    }

    return hostIP;
}

Error Session::init(GLFMDisplay *pApp) {
    const std::string host = readHostIP(pApp);
    const std::string port = Setting<ServerPort>::get().load();

    default_host_ = host;
    default_port_ = port;
    connect(host.c_str(), port.c_str());

    // Whether the server answers is not something startup can wait to find out, so it is not
    // something startup has an error for either: the login screen reports the attempt itself.
    return Error::NONE;
}

void Session::connect(const char *host, const char *port) {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        host_ = host;
        port_ = port;
    }

    // Whatever reader state the last connection was left in belongs to that connection, and so do the
    // packets still waiting for a frame to take them. The framing positions themselves are reset where
    // they are used, on the network thread, when this attempt's key arrives.
    reset_streams();

    // A deliberate new attempt, however it ends from here, is not a connection that was lost.
    had_connection_.store(false);

    if (host[0] == '\0') {
        // No address to dial, which is an attempt that is over rather than one in progress: a connect
        // that never ends would leave the login screen holding its loading picture for ever, and there
        // is no answer for a picture like that to be waiting for.
        link_.store(Link::Failed);
        __android_log_print(ANDROID_LOG_WARN, "Session", "no address to dial");
        return;
    }

    // Asked for through post(), and with the state change inside that same handler, so that it is
    // queued behind whatever the previous attempt still has to report. A read or a deadline from the
    // old connection may be reaching give_up() on the network thread right now, and a Failed it
    // stores after this Connecting is a login screen told to give up on a connection that has not
    // even been tried. Link state therefore only ever changes value on that thread - except for the
    // two cases above, which have no attempt for anything to disagree with.
    const std::string dial_host = host;
    const std::string dial_port = port;
    socket_.post([this, dial_host, dial_port] {
        link_.store(Link::Connecting);
        socket_.start_connect(dial_host.c_str(), dial_port.c_str(), Constants::CONNECT_TIMEOUT_MS);
        __android_log_print(ANDROID_LOG_INFO, "Session", "connecting %s:%s", dial_host.c_str(),
                            dial_port.c_str());
    });
}

void Session::reset_streams() {
    // Retiring the connection number first: a batch already in the handlers' hands stops at the next
    // packet, and anything still in the queues belongs to a stream that is not coming back.
    epoch_.fetch_add(1);
    std::lock_guard<std::mutex> lock(data_mutex_);
    tx_.clear();
    rx_.clear();
}

void Session::give_up(const char *why) {
    link_.store(Link::Failed);
    reset_streams();

    std::lock_guard<std::mutex> lock(data_mutex_);
    __android_log_print(ANDROID_LOG_WARN, "Session", "%s: %s", why, host_.c_str());
}

void Session::reconnect() {
    reconnect(default_host_.c_str(), default_port_.c_str());
}

void Session::reconnect(const char *address, const char *port) {
    connect(address, port);
}

bool Session::read_length(const int8_t *&bytes, size_t &available) {
    // The four length bytes can arrive in pieces. Reading a length out of only some of them would
    // hand back a number made of whatever bytes happened to be in the stream, and the packet reader
    // would then walk that far past the end of what it was given.
    while (header_pos_ < HEADER_LENGTH) {
        if (available == 0) {
            return false;
        }

        header_[header_pos_++] = *bytes++;
        available--;
    }

    length_ = cryptography_.check_length(header_);
    header_pos_ = 0;

    // The length the header gives is the packet's own bytes, not counting the header, and it comes
    // back as a signed number - a header that is not a header at all can read as any size at all.
    if (length_ < OPCODE_LENGTH || length_ > MAX_PACKET_LENGTH) {
        // Not a packet boundary. There is no way to find the next one in a stream whose framing is
        // already wrong, so the connection is given up on rather than read further.
        give_up("framing lost");
        return false;
    }

    return true;
}

void Session::on_io_handshake(const int8_t *handshake) {
    cryptography_ = {handshake};
    length_ = 0;
    pos_ = 0;
    header_pos_ = 0;
    dropped_ = 0;
    link_.store(Link::Connected);
    had_connection_.store(true);

    size_t held = 0;
    std::string dial_host;
    std::string dial_port;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        held = tx_.size();
        dial_host = host_;
        dial_port = port_;
    }
    __android_log_print(ANDROID_LOG_INFO, "Session", "connected %s:%s held=%zu",
                        dial_host.c_str(), dial_port.c_str(), held);

    pump_send();
}

void Session::on_io_bytes(const int8_t *bytes, size_t available) {
    while (available > 0) {
        // A packet is only ever started from a fresh position, and only a complete header can say how
        // long it is.
        if (pos_ == 0 && !read_length(bytes, available)) {
            return;
        }

        const size_t towrite = std::min(length_ - pos_, available);
        std::memcpy(buffer_ + pos_, bytes, towrite);

        pos_ += towrite;
        bytes += towrite;
        available -= towrite;

        if (pos_ < length_) {
            // The rest of this packet has to come in a later read; the reader's position is what makes
            // it resume where it stopped.
            return;
        }

        cryptography_.decrypt(buffer_, length_);

        const uint16_t opcode = packet_opcode(buffer_, length_);
        const PacketWeight weight = packet_weight(opcode);

        // The handler that eventually takes this may point the session at another server, and that
        // throws away whatever this stream still had in it. Handing the packet over as a copy of its
        // own is what makes that safe: the bytes left here belong to a connection that no longer
        // exists, but they are never read as a header again, because the fresh attempt resets the
        // positions when it reads its key.
        std::vector<int8_t> packet(buffer_, buffer_ + length_);
        pos_ = 0;
        length_ = 0;

        if (weight == PacketWeight::Keepalive) {
            // The server is asking whether this client exists, and a queue is not an answer. This is
            // the one packet read from the stream that never joins the others.
            answer_keepalive();
            continue;
        }

        const size_t dropped_before = dropped_;
        const Room room = queue_incoming(std::move(packet), weight == PacketWeight::Droppable);
        if (room == Room::Full) {
            give_up("inbox full and nothing droppable");
            return;
        }
        if (dropped_ > dropped_before && (dropped_ == 1 || dropped_ % 64 == 0)) {
            // The first one and then every sixty-fourth: a full inbox is worth knowing about, but a
            // game that floods the log with every dropped effect stops being readable. The opcode
            // rides along because a packet dropped that should not have been is only ever visible
            // here - the game just fails to react to it somewhere else.
            __android_log_print(ANDROID_LOG_WARN, "Session",
                                "inbox full, %zu packets dropped, last opcode %hu", dropped_,
                                opcode);
        }
    }
}

Session::Room Session::queue_incoming(std::vector<int8_t> &&packet, bool droppable) {
    std::lock_guard<std::mutex> lock(data_mutex_);

    if (rx_.size() < INBOX_LIMIT) {
        rx_.push_back({std::move(packet), droppable});
        return Room::Taken;
    }

    if (droppable) {
        // The inbox is full of what the game needs and this is what it does not.
        dropped_++;
        return Room::Dropped;
    }

    const auto victim = std::find_if(rx_.begin(), rx_.end(), [](const Incoming &incoming) {
        return incoming.droppable;
    });

    if (victim == rx_.end()) {
        return Room::Full;
    }

    // Erase and append rather than overwrite the victim where it stands: the newcomer arrived after
    // everything already in the queue, and a handler that reads a state update out of order is worse
    // than the cosmetic packet it cost.
    rx_.erase(victim);
    rx_.push_back({std::move(packet), droppable});
    dropped_++;

    return Room::Taken;
}

void Session::answer_keepalive() {
    // Built and written from here instead of dispatched like every other packet goes out:
    // OutPacket::dispatch() reads Configuration for its packet log, and that map is written by the
    // render thread.
    PongPacket pong;
    std::vector<int8_t> bytes = pong.build();

    write(bytes.data(), bytes.size());
}

void Session::pump_send() {
    if (link_.load() != Link::Connected) {
        // Still no key to frame these with. They leave from on_io_handshake instead.
        return;
    }

    std::deque<std::vector<int8_t>> held;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        held.swap(tx_);
    }

    while (!held.empty()) {
        std::vector<int8_t> plaintext = std::move(held.front());
        held.pop_front();

        int8_t header[HEADER_LENGTH];
        cryptography_.create_header(header, plaintext.size());
        cryptography_.encrypt(plaintext.data(), plaintext.size());

        // Header and body as one frame: the two separate writes this replaces could have left a
        // header on the wire with its body still to come.
        std::vector<int8_t> frame;
        frame.reserve(HEADER_LENGTH + plaintext.size());
        frame.insert(frame.end(), header, header + HEADER_LENGTH);
        frame.insert(frame.end(), plaintext.begin(), plaintext.end());

        socket_.write(std::move(frame));
    }
}

void Session::write(int8_t *packet_bytes, size_t packet_length) {
    const Link link = link_.load();

    if (link == Link::Connecting || link == Link::Connected) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        // Nothing can be sent before the key exists, but this belongs to the attempt in flight: the
        // login flow points the session at a channel server and writes its next packet in the same
        // update, which is long before that dial can have finished. Held as the bytes they were given,
        // and sent in order the moment it is.
        if (link == Link::Connecting && tx_.size() >= OUTBOX_LIMIT) {
            return;
        }
        tx_.emplace_back(packet_bytes, packet_bytes + packet_length);
    } else {
        // Idle or Failed: there is no connection and no attempt that could make one, so this packet
        // has nowhere to go. Reconnect() is what starts an attempt; writing is not, or a page that
        // sends one packet a frame would redial a dead server once a frame.
        return;
    }

    socket_.post([this] { pump_send(); });
}

void Session::read() {
    const uint32_t epoch = epoch_.load();

    std::deque<Incoming> batch;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        batch.swap(rx_);
    }

    while (!batch.empty()) {
        std::vector<int8_t> packet = std::move(batch.front().bytes);
        batch.pop_front();

        if (epoch_.load() != epoch) {
            // A handler above took the session somewhere else, or the network thread ended the
            // connection underneath it. The bytes still in this batch came out of a stream that is
            // gone: forwarding them would be charged to a connection whose keys are not even read
            // yet, and the login flow would then be answered by packets from the server it left.
            return;
        }

        try {
            packet_forwarder_->forward(packet.data(), packet.size());
        } catch (const PacketError &err) {
            __android_log_print(ANDROID_LOG_ERROR, "Session", "packet error: %s", err.what());
        }
    }
}

bool Session::connect_failed() const {
    return link_.load() == Link::Failed;
}

bool Session::link_lost() const {
    return had_connection_.load() && link_.load() == Link::Failed;
}
}  // namespace ms
