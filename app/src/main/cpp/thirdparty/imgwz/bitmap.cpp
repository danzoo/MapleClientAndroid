//  imgwz - canvas decoding.  NoLifeNx kept one global scratch buffer that every
//  nl::bitmap::data() call inflated into, and the client depends on that (it copies
//  the pixels into an atlas immediately), so the same rule is kept here.
#include "bitmap.hpp"

#include "img_internal.hpp"

#include <cstring>
#include <vector>
#include <zlib.h>

namespace nl
{
namespace
{
//! The u16 pixel layout behind format 1: A, R, G, B, one nibble each, high to low.
void expand4444(std::uint8_t const* src, std::uint8_t* dst, std::size_t pixels)
{
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint16_t v = static_cast<std::uint16_t>(src[2 * i] | (src[2 * i + 1] << 8));
        auto const spread = [](std::uint16_t nibble) {
            return static_cast<std::uint8_t>((nibble << 4) | nibble);
        };
        dst[4 * i + 0] = spread(v & 0xF);           // B
        dst[4 * i + 1] = spread((v >> 4) & 0xF);    // G
        dst[4 * i + 2] = spread((v >> 8) & 0xF);    // R
        dst[4 * i + 3] = spread((v >> 12) & 0xF);   // A
    }
}
} // namespace

bitmap::bitmap(void const* d, std::uint16_t w, std::uint16_t h)
    : m_data(d), m_width(w), m_height(h)
{
}

bool bitmap::operator<(bitmap const& o) const
{
    return m_data < o.m_data;
}

bool bitmap::operator==(bitmap const& o) const
{
    return m_data == o.m_data;
}

bitmap::operator bool() const
{
    return m_data != nullptr;
}

std::vector<char> bitmap_buf;
std::vector<char> bitmap_raw;
void const* bitmap::data() const
{
    if (!m_data) {
        return nullptr;
    }
    auto const& canvas = *static_cast<canvas_data const*>(m_data);
    auto const pixels = static_cast<std::size_t>(m_width) * m_height;
    auto const total = length();
    if (!pixels || !canvas.csize) {
        return nullptr;
    }

    // MapleLib reads the payload the same way: the two zlib header bytes are dropped
    // and the rest is a raw deflate stream that has to produce exactly w*h*4.
    auto const expected = canvas.format == 1 ? pixels * 2 : total;
    if (canvas.format != 1 && canvas.format != 2) {
        img_report_error("canvas", "unsupported pixel format");
        return nullptr;
    }
    // Some editors (WvSMaps) save each canvas with a second all-zero layer appended
    // behind the real pixels, so the stream can legitimately be twice as long.
    // Inflate into room for that case plus headroom, so zlib always gets space to
    // reach the end-of-stream marker instead of stopping on a exactly-full buffer.
    auto const capacity = expected * 2;
    auto const room = capacity + 0x20;
    if (room > bitmap_raw.size()) {
        bitmap_raw.resize(room);
    }

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (::inflateInit2(&stream, -15) != Z_OK) {
        return nullptr;
    }
    stream.next_in = const_cast<Bytef*>(canvas.payload + 2);
    stream.avail_in = canvas.csize > 2 ? static_cast<uInt>(canvas.csize - 2) : 0;
    stream.next_out = reinterpret_cast<Bytef*>(bitmap_raw.data());
    stream.avail_out = static_cast<uInt>(room);
    int code = Z_OK;
    while (code == Z_OK) {
        code = ::inflate(&stream, Z_NO_FLUSH);
    }
    auto const produced = static_cast<std::size_t>(stream.total_out);
    auto const left_in = static_cast<std::size_t>(stream.avail_in);
    ::inflateEnd(&stream);
    // Editors do not always close the deflate stream with a final block, so "input
    // ran out" is accepted as complete. Z_DATA_ERROR still means a broken payload.
    bool const decoded = code == Z_STREAM_END || (code == Z_BUF_ERROR && left_in == 0);
    if (!decoded) {
        img_report_error("canvas", "payload did not decode");
        return nullptr;
    }
    if (produced == capacity) {
        auto const second = reinterpret_cast<std::uint8_t const*>(bitmap_raw.data()) + expected;
        for (std::size_t i = 0; i < expected; ++i) {
            if (second[i] != 0) {
                img_report_error("canvas", "double-layer payload is not empty");
                return nullptr;
            }
        }
    } else if (produced != expected) {
        img_report_error("canvas", "payload size mismatch");
        return nullptr;
    }

    if (total + 0x20 > bitmap_buf.size()) {
        bitmap_buf.resize(total + 0x20);
    }
    if (canvas.format == 1) {
        expand4444(reinterpret_cast<std::uint8_t const*>(bitmap_raw.data()),
                   reinterpret_cast<std::uint8_t*>(bitmap_buf.data()), pixels);
    } else {
        std::memcpy(bitmap_buf.data(), bitmap_raw.data(), total);
    }
    return bitmap_buf.data();
}

std::uint16_t bitmap::width() const
{
    return m_width;
}

std::uint16_t bitmap::height() const
{
    return m_height;
}

std::uint32_t bitmap::length() const
{
    return 4u * m_width * m_height;
}

std::uint32_t bitmap::compressed_length() const
{
    return m_data ? static_cast<canvas_data const*>(m_data)->csize : 0;
}

std::size_t bitmap::id() const
{
    return reinterpret_cast<std::size_t>(m_data);
}
} // namespace nl
