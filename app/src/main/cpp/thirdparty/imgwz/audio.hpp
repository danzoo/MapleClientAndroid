//  imgwz - sound handle, same public shape as NoLifeNx's nl::audio.
#pragma once
#include "nxfwd.hpp"

#include <cstddef>
#include <cstdint>

namespace nl
{
class audio
{
public:
    audio() = default;
    audio(audio const&) = default;
    audio& operator=(audio const&) = default;
    // Comparison operators, useful for containers
    bool operator==(audio const&) const;
    bool operator<(audio const&) const;
    //! Returns whether the audio is valid or merely null
    explicit operator bool() const;
    //! Does not do any sort of decompression; the container stores the
    //! WaveFormat preamble plus the encoded bytes verbatim.
    //! Do not free the pointer returned by this method.
    //! The pointer remains valid until the file this audio is part of is
    //! destroyed.
    void const* data() const;
    std::uint32_t length() const;
    //! Returns a unique ID, useful for keeping track of what audio you've
    //! loaded.
    std::size_t id() const;

private:
    audio(void const*, std::uint32_t);
    void const* m_data = nullptr;
    std::uint32_t m_length = 0;
    friend node;
};
} // namespace nl
