//  imgwz - canvas decoding.  NoLifeNx kept one global scratch buffer that every
//  img::bitmap::data() call inflated into.  That buffer is per-thread now: a canvas is inflated
//  while its pixels are being uploaded, and warming a picture up ahead of that happens on a
//  thread of its own, so the two must not be able to inflate over each other's bytes.
//  Callers that keep the pixels ask for decode_into() instead, which shares nothing at all.
#include "bitmap.hpp"

#include "img_internal.hpp"

#include <cstring>
#include <vector>
#include <zlib.h>

namespace img
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

//! The inflate staging buffer, one per thread: the deflate stream comes out here before the
//! pixels are put where the caller asked for them, because the two formats need different work
//! and the double-layer check reads back over the whole inflated span.
thread_local std::vector<char> inflate_buf;
//! The buffer img::bitmap::data() hands out, one per thread.
thread_local std::vector<char> pixel_buf;
//! The payload of a canvas stored with its deflate stream listwz-framed comes out here,
//! one per thread.  Only the framed minority needs it.
thread_local std::vector<char> delist_buf;

//! The four two-byte openings a deflate stream is allowed to have.  An unframed payload
//! always starts with one, so its absence is what identifies the framing - nothing in the
//! file records which canvases carry it.
bool is_deflate_head(std::uint8_t const* p)
{
    auto const head = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    return head == 0x9C78 || head == 0xDA78 || head == 0x0178 || head == 0x5E78;
}

//! Takes the listwz framing off a canvas payload: a run of `[i32 block length][block
//! XORed with the keystream from offset zero]`, which is how editors that round-tripped a
//! wz through MapleLib store the deflate stream.  Each block restarts the keystream, the
//! same way every string in the file does.  Returns the bytes produced, or 0 when the
//! block lengths do not add up to the span handed in.
std::size_t delistwz(std::uint8_t const* src, std::size_t src_size, std::vector<char>& dst)
{
    auto const& cipher_ref = cipher::get();
    std::size_t produced = 0;
    std::size_t off = 0;
    while (off + 4 <= src_size) {
        std::int32_t block;
        std::memcpy(&block, src + off, 4);
        off += 4;
        if (block <= 0 || static_cast<std::size_t>(block) > src_size - off) {
            return 0;
        }
        auto const size = static_cast<std::size_t>(block);
        cipher_ref.ensure(size);
        if (dst.size() < produced + size) {
            dst.resize(produced + size);
        }
        for (std::size_t i = 0; i < size; ++i) {
            dst[produced + i] = static_cast<char>(src[off + i] ^ cipher_ref.stream[i]);
        }
        produced += size;
        off += size;
    }
    return off == src_size ? produced : 0;
}

//! Inflates one canvas into `dst`, which has to hold width*height*4 bytes.  Returns the number of
//! bytes written, or 0 when the payload does not decode.
std::size_t inflate_canvas(canvas_data const& canvas, std::uint16_t width, std::uint16_t height,
                           std::uint8_t* dst, std::size_t dst_size)
{
    auto const pixels = static_cast<std::size_t>(width) * height;
    auto const total = 4u * pixels;
    if (!pixels || !canvas.csize || dst_size < total) {
        return 0;
    }

    // MapleLib reads the payload the same way: the two zlib header bytes are dropped
    // and the rest is a raw deflate stream that has to produce exactly w*h*4.
    auto const expected = canvas.format == 1 ? pixels * 2 : total;
    if (canvas.format != 1 && canvas.format != 2) {
        img_report_error("canvas", "unsupported pixel format");
        return 0;
    }
    // Some editors (WvSMaps) save each canvas with a second all-zero layer appended
    // behind the real pixels, so the stream can legitimately be twice as long.
    // Inflate into room for that case plus headroom, so zlib always gets space to
    // reach the end-of-stream marker instead of stopping on a exactly-full buffer.
    auto const capacity = expected * 2;
    auto const room = capacity + 0x20;
    if (room > inflate_buf.size()) {
        inflate_buf.resize(room);
    }

    // Framing has to come off before zlib sees the bytes; inflating framed bytes is what
    // made a monster paint as a plain white block, because the atlas caches the failed
    // decode as an empty offset and the shader reads an empty offset as the solid colour.
    // Framing whose lengths do not add up stays framed, so a genuinely broken payload
    // still fails the way it used to rather than turning into a different byte run.
    std::uint8_t const* in = canvas.payload;
    std::size_t in_size = canvas.csize;
    if (in_size >= 2 && !is_deflate_head(in)) {
        if (auto const unframed = delistwz(in, in_size, delist_buf)) {
            in = reinterpret_cast<std::uint8_t const*>(delist_buf.data());
            in_size = unframed;
        }
    }

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (::inflateInit2(&stream, -15) != Z_OK) {
        return 0;
    }
    stream.next_in = const_cast<Bytef*>(in + 2);
    stream.avail_in = in_size > 2 ? static_cast<uInt>(in_size - 2) : 0;
    stream.next_out = reinterpret_cast<Bytef*>(inflate_buf.data());
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
        return 0;
    }
    if (produced == capacity) {
        auto const second = reinterpret_cast<std::uint8_t const*>(inflate_buf.data()) + expected;
        for (std::size_t i = 0; i < expected; ++i) {
            if (second[i] != 0) {
                img_report_error("canvas", "double-layer payload is not empty");
                return 0;
            }
        }
    } else if (produced != expected) {
        img_report_error("canvas", "payload size mismatch");
        return 0;
    }

    if (canvas.format == 1) {
        expand4444(reinterpret_cast<std::uint8_t const*>(inflate_buf.data()), dst, pixels);
    } else {
        std::memcpy(dst, inflate_buf.data(), total);
    }
    return total;
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

void const* bitmap::data() const
{
    if (!m_data) {
        return nullptr;
    }
    auto const& canvas = *static_cast<canvas_data const*>(m_data);

    // Handing out a buffer instead of a caller's is the whole difference between this and
    // decode_into(), and the slack is only there so that a run of nearly-sized sprites does not
    // reallocate. It still belongs to this thread, and still goes out of date on the next call.
    auto const total = length();
    if (total + 0x20 > pixel_buf.size()) {
        pixel_buf.resize(total + 0x20);
    }
    if (!inflate_canvas(canvas, m_width, m_height,
                        reinterpret_cast<std::uint8_t*>(pixel_buf.data()), pixel_buf.size())) {
        return nullptr;
    }
    return pixel_buf.data();
}

std::size_t bitmap::decode_into(void *dst, std::size_t dst_size) const
{
    if (!m_data || !dst) {
        return 0;
    }
    return inflate_canvas(*static_cast<canvas_data const*>(m_data), m_width, m_height,
                          static_cast<std::uint8_t*>(dst), dst_size);
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
} // namespace img
