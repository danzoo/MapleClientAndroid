//  imgwz - sound handle.  A canvas' bytes are copied into an atlas as soon as they are
//  read, but BASS keeps reading an audio buffer for as long as the sample is loaded, so
//  the pointer handed out here has to stay valid; the file stays mapped for the life of
//  the process, which is what NoLifeNx's mmapped archive guaranteed.
#include "audio.hpp"

namespace nl
{
audio::audio(void const* d, std::uint32_t l) : m_data(d), m_length(l)
{
}

bool audio::operator<(audio const& o) const
{
    return m_data < o.m_data;
}

bool audio::operator==(audio const& o) const
{
    return m_data == o.m_data;
}

audio::operator bool() const
{
    return m_data != nullptr;
}

void const* audio::data() const
{
    return m_data;
}

std::uint32_t audio::length() const
{
    return m_length;
}

std::size_t audio::id() const
{
    return reinterpret_cast<std::size_t>(m_data);
}
} // namespace nl
