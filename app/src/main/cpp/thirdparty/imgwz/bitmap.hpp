//  imgwz - canvas handle, same public shape as NoLifeNx's bitmap class.
#pragma once
#include "fwd.hpp"

#include <cstddef>
#include <cstdint>

namespace img
{
class bitmap
{
public:
    bitmap() = default;
    bitmap(bitmap const&) = default;
    //! `d` points at the canvas' decoded-pixel descriptor, see canvas_data.
    bitmap(void const*, std::uint16_t, std::uint16_t);

    bitmap& operator=(bitmap const&) = default;
    // Comparison operators, useful for containers
    bool operator==(bitmap const&) const;
    bool operator<(bitmap const&) const;
    //! Returns whether the bitmap is valid or merely null
    explicit operator bool() const;
    //! Inflates the canvas into this thread's own scratch buffer and returns a pointer into it.
    //! The buffer belongs to the calling thread, so another thread decoding at the same time cannot
    //! touch these bytes; within one thread the old rule still holds.
    //! Nullable
    void const* data() const;
    //! Inflates the canvas straight into `dst`, which has to hold length() bytes. Returns the number
    //! of bytes written, or 0 when the payload does not decode. Nothing is shared with the caller or
    //! with any other thread, so this is the form to use when the pixels are to be kept or handed
    //! across a thread boundary.
    std::size_t decode_into(void *dst, std::size_t dst_size) const;
    std::uint16_t width() const;
    std::uint16_t height() const;
    //! 4 * width * height
    std::uint32_t length() const;
    std::uint32_t compressed_length() const;
    //! Returns a unique ID, useful for keeping track of what bitmaps you've
    //! loaded.  Stable for the lifetime of the process, which is what the
    //! texture atlas caches on.
    std::size_t id() const;

private:
    void const* m_data = nullptr;
    std::uint16_t m_width = 0;
    std::uint16_t m_height = 0;
    friend node;
};
} // namespace img
