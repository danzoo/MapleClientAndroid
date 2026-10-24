//  imgwz - canvas handle, same public shape as NoLifeNx's nl::bitmap.
#pragma once
#include "nxfwd.hpp"

#include <cstddef>
#include <cstdint>

namespace nl
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
    //! This function inflates the canvas on the fly.
    //! Do not free the pointer returned by this method.
    //! Every time this function is called, any previous pointers returned
    //! by this method become invalid.
    //! As such, this function is very *not* threadsafe.
    //!
    //! Nullable
    void const* data() const;
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
} // namespace nl
