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

namespace ms {
namespace {
// How many packets will be held for a connect that has not come up. Past this the queue has stopped
// being a buffer and become a symptom: whoever is writing is doing it faster than a connection that
// does not exist could ever take the packets, so the surplus is dropped rather than remembered.
constexpr size_t OUTBOX_LIMIT = 64;
}  // namespace

Session::Session(std::unique_ptr<Forwarder> packet_forwarder) :
    packet_forwarder_(std::move(packet_forwarder)),
    length_(0),
    pos_(0),
    header_pos_(0),
    link_(Link::Idle),
    had_connection_(false) {}

Session::~Session() {
    socket_.close();
}

std::string Session::readHostIP(GLFMDisplay *pApp) {
    std::string filePath = std::string(((ANativeActivity*)glfmGetAndroidActivity(pApp))->externalDataPath) + "/hostip.txt";
    FILE* file = fopen(filePath.c_str(), "rb");
    std::string hostIP;

    if (file != nullptr) {
        char buffer[256]; // Buffer to store read data
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
    host_ = host;
    port_ = port;

    // Whatever reader state the last connection was left in belongs to that connection. A packet
    // that stopped halfway through it would otherwise be finished out of the new one's bytes.
    length_ = 0;
    pos_ = 0;
    header_pos_ = 0;
    outbox_.clear();

    // A deliberate new attempt, however it ends from here, is not a connection that was lost.
    had_connection_ = false;

    if (host[0] == '\0') {
        // No address to dial, which is an attempt that is over rather than one in progress: a
        // connect that never ends would leave the login screen holding its loading picture for ever,
        // and there is no answer for a picture like that to be waiting for.
        link_ = Link::Failed;
        return;
    }

    link_ = Link::Connecting;
    socket_.start_connect(host, port, Constants::CONNECT_TIMEOUT_MS);
    __android_log_print(ANDROID_LOG_INFO, "Session", "connecting %s:%s", host, port);
}

void Session::give_up(const char *why) {
    link_ = Link::Failed;
    outbox_.clear();
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

void Session::process(const int8_t *bytes, size_t available) {
    while (available > 0) {
        // A packet is only ever started from a fresh position, and only a complete header can say
        // how long it is.
        if (pos_ == 0 && !read_length(bytes, available)) {
            return;
        }

        size_t towrite = std::min(length_ - pos_, available);
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

        try {
            packet_forwarder_->forward(buffer_, length_);
        } catch (const PacketError &err) {
            std::cout << err.what() << std::endl;
        }

        pos_ = 0;
        length_ = 0;

        // A packet handler is allowed to point the session at another server, and that throws away
        // whatever this stream still had in it. The bytes left here belong to a connection that no
        // longer exists: reading a header out of them would be charged to the next connection, whose
        // keys are not even read yet, so the length comes out of nonsense and the fresh attempt gets
        // called off before it ever had a chance.
        if (link_ != Link::Connected) {
            return;
        }
    }
}

void Session::send(int8_t *bytes, size_t length) {
    int8_t header[HEADER_LENGTH];
    cryptography_.create_header(header, length);
    cryptography_.encrypt(bytes, length);

    if (!socket_.dispatch(header, HEADER_LENGTH)
        || !socket_.dispatch(bytes, length)) {
        // A send the socket refused means the other end is gone; there is no point reading on for a
        // server that has stopped taking packets.
        give_up("send refused");
    }
}

void Session::write(int8_t *packet_bytes, size_t packet_length) {
    if (link_ == Link::Connecting) {
        // Nothing can be sent yet, but this belongs to the attempt that is in flight: the login flow
        // points the session at a channel server and writes its next packet in the same update, which
        // is long before that dial can have finished. Held as the bytes they were given - the cipher
        // belongs to a connection, and its key is only known once the handshake has been read - and
        // sent in order the moment it is.
        if (outbox_.size() < OUTBOX_LIMIT) {
            outbox_.emplace_back(packet_bytes, packet_bytes + packet_length);
        }
        return;
    }

    if (link_ == Link::Connected) {
        send(packet_bytes, packet_length);
    }

    // Idle or Failed: there is no connection and no attempt that could make one, so this packet has
    // nowhere to go. Reconnect() is what starts an attempt; writing is not, or a page that sends one
    // packet a frame would redial a dead server once a frame.
}

void Session::flush_outbox() {
    while (!outbox_.empty()) {
        std::vector<int8_t> packet = std::move(outbox_.front());
        outbox_.pop_front();
        send(packet.data(), packet.size());
    }
}

void Session::read() {
    if (link_ == Link::Connecting) {
        // Advancing the attempt is the only thing a connect takes between frames, and it is called
        // from here so that it runs on the same thread as everything that reads the result.
        socket_.poll();

        const Connect state = socket_.connect_state();
        if (state == Connect::Pending) {
            return;
        }

        if (state == Connect::Ready) {
            cryptography_ = {socket_.handshake()};
            length_ = 0;
            pos_ = 0;
            header_pos_ = 0;
            link_ = Link::Connected;
            had_connection_ = true;
            __android_log_print(ANDROID_LOG_INFO, "Session", "connected %s:%s held=%zu",
                                host_.c_str(), port_.c_str(), outbox_.size());
            flush_outbox();
            return;
        }

        give_up("connect gave up");
        return;
    }

    if (link_ != Link::Connected) {
        return;
    }

    bool alive = true;
    size_t result = socket_.receive(&alive);

    if (!alive) {
        give_up("link dropped");
        return;
    }

    if (result > 0) {
        process(socket_.get_buffer(), result);
    }
}

bool Session::connect_failed() const {
    return link_ == Link::Failed;
}

bool Session::link_lost() const {
    return had_connection_ && link_ == Link::Failed;
}
}  // namespace ms
