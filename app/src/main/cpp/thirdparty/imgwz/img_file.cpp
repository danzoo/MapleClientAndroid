//  The container half of imgwz: mapping one .img, walking its property tree lazily,
//  and listing the directories that stand in for the old archive roots.
//
//  The record layout mirrored here is the one F:/nxfiles/tools/imgdec/img2wz.py reads,
//  which is the layout Assets/nx2img.py writes: a file is a single image block
//  (0x73 + "Property" + u16 0 + one property list) and every name or extended-kind
//  reference counts from the start of the file.
#include "img_internal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#ifndef _WIN32
#    include <dirent.h>
#    include <fcntl.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <windows.h>
#endif
#ifdef __ANDROID__
#    include <android/log.h>
#endif

namespace img
{
namespace
{
std::int8_t s8(std::uint8_t const* b)
{
    return static_cast<std::int8_t>(*b);
}

std::uint16_t u16le(std::uint8_t const* b)
{
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::int32_t i32le(std::uint8_t const* b)
{
    return static_cast<std::int32_t>(
        std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16)
        | (std::uint32_t(b[3]) << 24));
}

std::int64_t i64le(std::uint8_t const* b)
{
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | b[i];
    }
    return static_cast<std::int64_t>(v);
}

float f32le(std::uint8_t const* b)
{
    const std::uint32_t raw = std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8)
                              | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
    float f;
    std::memcpy(&f, &raw, 4);
    return f;
}

double f64le(std::uint8_t const* b)
{
    std::uint64_t raw;
    std::memcpy(&raw, b, 8);
    double d;
    std::memcpy(&d, &raw, 8);
    return d;
}

//! Appends one UTF-16 code point to a UTF-8 string, the way the archive node names were.
void utf16_append(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<std::uint8_t>((cp >> 6) & 0x3F) | 0x80);
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string base_name(std::string const& path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
} // namespace

void img_data::ensure() const
{
    if (opened) {
        return;
    }
    opened = true;
#ifndef _WIN32
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        broken = true;
        img_report_error(path.c_str(), "open failed");
        return;
    }
    struct stat st;
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        broken = true;
        img_report_error(path.c_str(), "stat failed");
        return;
    }
    void* addr = ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ,
                        MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (addr == MAP_FAILED) {
        broken = true;
        img_report_error(path.c_str(), "mmap failed");
        return;
    }
    base = static_cast<std::uint8_t const*>(addr);
    size = static_cast<std::uint32_t>(st.st_size);
    ::madvise(const_cast<std::uint8_t*>(base), size, MADV_RANDOM);
#else
    HANDLE handle = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        broken = true;
        img_report_error(path.c_str(), "CreateFile failed");
        return;
    }
    LARGE_INTEGER length;
    if (!::GetFileSizeEx(handle, &length) || length.QuadPart <= 0) {
        ::CloseHandle(handle);
        broken = true;
        img_report_error(path.c_str(), "GetFileSize failed");
        return;
    }
    HANDLE mapping = ::CreateFileMappingA(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
    ::CloseHandle(handle);
    if (!mapping) {
        broken = true;
        img_report_error(path.c_str(), "CreateFileMapping failed");
        return;
    }
    void* addr = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    ::CloseHandle(mapping);
    if (!addr) {
        broken = true;
        img_report_error(path.c_str(), "MapViewOfFile failed");
        return;
    }
    base = static_cast<std::uint8_t const*>(addr);
    size = static_cast<std::uint32_t>(length.QuadPart);
#endif

    // header: 0x73, the inline string "Property", then a u16 that is always zero.
    if (size < 4 || base[0] != 0x73) {
        broken = true;
        img_report_error(path.c_str(), "not an image block");
        return;
    }
    std::string scratch;
    std::uint32_t p = read_string(1, scratch);
    if (broken || scratch != "Property" || p + 2 > size) {
        broken = true;
        img_report_error(path.c_str(), "header is not Property/0");
        return;
    }
    p += 2;

    prop_data root;
    root.type = static_cast<std::uint16_t>(node::type::none);
    read_count(p, root.count);
    root.items = p;
    root.items_end = size;
    root.name_len = static_cast<std::uint16_t>(std::min<std::size_t>(name.size(), 0xFFFF));
    root.name = store_name(name);
    props.push_back(root);
}

std::uint32_t img_data::read_string(std::uint32_t p, std::string& out) const
{
    out.clear();
    if (broken || p >= size) {
        return size;
    }
    const std::int8_t head = s8(base + p);
    ++p;
    std::uint32_t units = 0;
    bool wide = false;
    if (head > 0) {
        wide = true;
        if (head == 127) {
            if (p + 4 > size) {
                broken = true;
                return size;
            }
            units = static_cast<std::uint32_t>(i32le(base + p));
            p += 4;
        } else {
            units = static_cast<std::uint32_t>(head);
        }
    } else if (head < 0) {
        if (head == -128) {
            if (p + 4 > size) {
                broken = true;
                return size;
            }
            units = static_cast<std::uint32_t>(i32le(base + p));
            p += 4;
        } else {
            units = static_cast<std::uint32_t>(-head);
        }
    } else {
        return p;
    }

    auto const& cipher_ref = cipher::get();
    if (wide) {
        if (units > (size - p) / 2) {
            broken = true;
            return size;
        }
        const std::uint32_t bytes = units * 2;
        cipher_ref.ensure(bytes);
        std::u16string utf16;
        utf16.reserve(units);
        for (std::uint32_t i = 0; i < units; ++i) {
            const std::uint16_t word = u16le(base + p + 2 * i);
            utf16.push_back(static_cast<std::uint16_t>(word ^ cipher_ref.utf16[i]));
        }
        for (std::uint32_t i = 0; i < units; ++i) {
            std::uint32_t cp = utf16[i];
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < units && utf16[i + 1] >= 0xDC00
                && utf16[i + 1] < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (utf16[++i] - 0xDC00);
            }
            utf16_append(out, cp);
        }
        return p + bytes;
    }

    if (units > size - p) {
        broken = true;
        return size;
    }
    cipher_ref.ensure(units);
    out.resize(units);
    for (std::uint32_t i = 0; i < units; ++i) {
        out[i] = static_cast<char>(base[p + i] ^ cipher_ref.ascii[i]);
    }
    return p + units;
}

std::uint32_t img_data::read_name(std::uint32_t p, std::string& out) const
{
    out.clear();
    if (broken || p >= size) {
        return size;
    }
    const std::uint8_t code = base[p];
    ++p;
    switch (code) {
    case 0x00:
    case 0x73:
        return read_string(p, out);
    case 0x01:
    case 0x1B: {
        if (p + 4 > size) {
            broken = true;
            return size;
        }
        // References are offsets into this file, which is why `base` is always zero.
        // The record itself continues after the four offset bytes.
        read_string(static_cast<std::uint32_t>(i32le(base + p)), out);
        return p + 4;
    }
    default:
        broken = true;
        return size;
    }
}

std::uint32_t img_data::read_canvas(std::uint32_t p, canvas_data& out) const
{
    // w, h, then the format split over two compressed ints, then four padding bytes,
    // then the payload length (minus one) and one more padding byte.
    if (broken) {
        return size;
    }
    int w = 0, h = 0, lo = 0, hi = 0;
    for (int* slot : {&w, &h, &lo, &hi}) {
        read_field(p, *slot);
    }
    p += 4; // the reserved four bytes between the format and the payload length
    if (p + 9 > size) {
        broken = true;
        return size;
    }
    const std::uint32_t csize = static_cast<std::uint32_t>(i32le(base + p)) - 1;
    p += 5; // the four length bytes and the single padding byte in front of the payload
    if (csize > size - p) {
        broken = true;
        return size;
    }
    out.payload = base + p;
    out.csize = csize;
    out.width = static_cast<std::uint16_t>(w);
    out.height = static_cast<std::uint16_t>(h);
    out.format = static_cast<std::uint16_t>(lo + (hi << 8));
    return p + csize;
}

std::uint32_t img_data::read_sound(std::uint32_t p, audio_data& out) const
{
    // data length and playing time, then the WaveFormat header and the encoded bytes
    // sitting back to back exactly as the nx archive stored them.
    if (broken) {
        return size;
    }
    std::int32_t dlen = 0;
    std::int32_t ms = 0;
    read_field(p, dlen);
    read_field(p, ms);
    if (broken) {
        return size;
    }
    if (p + sound_header_len + 1 > size || dlen < 0) {
        broken = true;
        return size;
    }
    const std::uint32_t fmt_len = base[p + sound_header_len];
    const std::uint64_t total = std::uint64_t(sound_header_len + 1 + fmt_len) + dlen;
    if (total > size - p) {
        broken = true;
        return size;
    }
    out.data = base + p;
    out.length = static_cast<std::uint32_t>(total);
    return p + out.length;
}

std::uint32_t img_data::skip_entry(std::uint32_t p) const
{
    std::string scratch;
    p = read_name(p, scratch);
    if (broken || p >= size) {
        return size;
    }
    const std::uint8_t type = base[p];
    ++p;
    switch (type) {
    case 0:
        break;
    case 2:
    case 11:
        p += 2;
        break;
    case 3:
    case 19:
        if (p < size && s8(base + p) == -128) {
            p += 5;
        } else {
            p += 1;
        }
        break;
    case 20:
        if (p < size && s8(base + p) == -128) {
            p += 9;
        } else {
            p += 1;
        }
        break;
    case 4:
        p += (p < size && base[p] == 0x80) ? 5 : 1;
        break;
    case 5:
        p += 8;
        break;
    case 8:
        p = read_name(p, scratch);
        break;
    case 9: {
        if (p + 4 > size) {
            broken = true;
            return size;
        }
        p += 4 + std::uint32_t(i32le(base + p));
        break;
    }
    default:
        broken = true;
        return size;
    }
    return p;
}

std::uint32_t img_data::skip_entries(std::uint32_t p, std::uint32_t count) const
{
    for (std::uint32_t i = 0; i < count && !broken; ++i) {
        p = skip_entry(p);
    }
    return p;
}

std::uint32_t img_data::store_name(std::string_view text) const
{
    const std::uint32_t offset = static_cast<std::uint32_t>(names.size());
    names.insert(names.end(), text.begin(), text.end());
    return offset;
}

std::uint32_t img_data::store_value(std::string_view text) const
{
    const std::uint32_t offset = static_cast<std::uint32_t>(values.size());
    const std::uint32_t length = static_cast<std::uint32_t>(text.size());
    for (int i = 0; i < 4; ++i) {
        values.push_back(static_cast<char>((length >> (8 * i)) & 0xFF));
    }
    values.insert(values.end(), text.begin(), text.end());
    return offset;
}

std::uint32_t img_data::parse(std::uint32_t p) const
{
    std::string text;
    p = read_name(p, text);
    if (broken || p >= size) {
        return size;
    }

    prop_data out;
    out.name_len = static_cast<std::uint16_t>(std::min<std::size_t>(text.size(), 0xFFFF));
    out.name = store_name(text);
    out.type = static_cast<std::uint16_t>(node::type::none);

    const std::uint8_t type = base[p];
    ++p;
    auto integer = [&](std::int64_t v) {
        out.type = static_cast<std::uint16_t>(node::type::integer);
        out.value = static_cast<std::uint64_t>(v);
    };
    auto real = [&](double v) {
        out.type = static_cast<std::uint16_t>(node::type::real);
        std::memcpy(&out.value, &v, 8);
    };
    auto string = [&](std::string_view v) {
        out.type = static_cast<std::uint16_t>(node::type::string);
        out.value = store_value(v);
    };

    switch (type) {
    case 0:
        break;
    case 2:
    case 11:
        if (p + 2 > size) {
            broken = true;
        } else {
            integer(static_cast<std::int16_t>(u16le(base + p)));
            p += 2;
        }
        break;
    case 3:
    case 19:
    case 20: {
        if (p >= size) {
            broken = true;
            break;
        }
        const std::int8_t head = s8(base + p);
        ++p;
        if (head == -128) {
            if (p + 4 > size) {
                broken = true;
                break;
            }
            // Type 20 is the 64-bit form; the others stay 32-bit even when padded.
            if (type == 20) {
                if (p + 8 > size) {
                    broken = true;
                    break;
                }
                integer(i64le(base + p));
                p += 8;
            } else {
                integer(i32le(base + p));
                p += 4;
            }
        } else {
            integer(head);
        }
        break;
    }
    case 4:
        if (p >= size) {
            broken = true;
            break;
        }
        if (base[p] == 0x80) {
            ++p;
            if (p + 4 > size) {
                broken = true;
            } else {
                real(f32le(base + p));
                p += 4;
            }
        } else {
            ++p;
            real(0.0);
        }
        break;
    case 5:
        if (p + 8 > size) {
            broken = true;
        } else {
            real(f64le(base + p));
            p += 8;
        }
        break;
    case 8:
        if (broken) {
            break;
        }
        p = read_name(p, text);
        string(text);
        break;
    case 9: {
        if (p + 4 > size) {
            broken = true;
            break;
        }
        const std::uint32_t end = p + 4 + std::uint32_t(i32le(base + p));
        p += 4;
        if (p >= size) {
            broken = true;
            break;
        }
        const std::uint8_t code = base[p];
        ++p;
        std::string kind;
        if (code == 0x00 || code == 0x73) {
            p = read_string(p, kind);
        } else if (code == 0x01 || code == 0x1B) {
            if (p + 4 > size) {
                broken = true;
                break;
            }
            read_string(static_cast<std::uint32_t>(i32le(base + p)), kind);
            p += 4;
        } else {
            broken = true;
            break;
        }
        if (broken) {
            break;
        }

        if (kind == "Property") {
            p += 2;
            read_count(p, out.count);
            out.items = p;
            out.items_end = end;
        } else if (kind == "Canvas") {
            p += 1;
            const bool has_children = p < size && base[p] == 1;
            ++p;
            if (has_children) {
                if (p + 2 > size) {
                    broken = true;
                    break;
                }
                p += 2;
                read_count(p, out.count);
                out.items = p;
                p = skip_entries(p, out.count);
                out.items_end = p;
            }
            canvas_data canvas;
            p = read_canvas(p, canvas);
            out.type = static_cast<std::uint16_t>(node::type::bitmap);
            out.value = static_cast<std::uint64_t>(store_canvas(canvas));
        } else if (kind == "Shape2D#Vector2D") {
            std::int32_t x = 0;
            std::int32_t y = 0;
            read_field(p, x);
            read_field(p, y);
            out.type = static_cast<std::uint16_t>(node::type::vector);
            out.value = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32)
                        | static_cast<std::uint32_t>(x);
        } else if (kind == "Shape2D#Convex2D") {
            // NoLifeNx has no convex type and nx2img only ever emits the kinds above,
            // so a body like this means the tree did not come from this converter.
            out.count = 0;
            img_report_error(path.c_str(), "unexpected Shape2D#Convex2D body");
        } else if (kind == "Sound_DX8") {
            p += 1;
            audio_data audio;
            p = read_sound(p, audio);
            out.type = static_cast<std::uint16_t>(node::type::audio);
            out.value = static_cast<std::uint64_t>(store_audio(audio));
        } else if (kind == "UOL") {
            // NoLifeNx has no uol type, so nx2img never emits one; reading the path as
            // a string keeps an unexpected tree from breaking the whole file.
            p += 1;
            p = read_name(p, text);
            string(text);
        } else {
            broken = true;
            img_report_error(path.c_str(), "unknown extended property");
            break;
        }
        p = end;
        break;
    }
    default:
        broken = true;
        img_report_error(path.c_str(), "unknown property type");
        break;
    }

    props.push_back(out);
    return p;
}

std::uint32_t img_data::store_canvas(canvas_data canvas) const
{
    canvases.push_back(canvas);
    return static_cast<std::uint32_t>(canvases.size() - 1);
}

std::uint32_t img_data::store_audio(audio_data audio) const
{
    audios.push_back(audio);
    return static_cast<std::uint32_t>(audios.size() - 1);
}

void img_data::walk(std::uint32_t parent, std::uint32_t p, std::uint32_t end, std::uint32_t first) const
{
    std::uint32_t previous = none_index;
    bool sorted = true;
    while (p < end && !broken) {
        const std::uint32_t next = parse(p);
        if (next <= p) {
            break;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(props.size() - 1);
        // Children of a child are appended only when something asks for them, so one
        // level always ends up contiguous in the arena.
        if (previous != none_index
            && stored_name(props[previous]) > stored_name(props[index])) {
            sorted = false;
        }
        previous = index;
        p = next;
    }
    if (sorted) {
        // flag_sorted means "my children are name-sorted, find() may binary
        // search them", so it belongs on the parent node, not on the children.
        props[parent].flags |= flag_sorted;
    }
}

void img_data::ensure_children(std::uint32_t index) const
{
    ensure();
    if (index >= props.size()) {
        return;
    }
    prop_data& parent = props[index];
    if (parent.flags & flag_parsed) {
        return;
    }
    parent.flags |= flag_parsed;
    if (!parent.count || broken || parent.items >= parent.items_end) {
        parent.first = none_index;
        return;
    }
    const std::uint32_t first = static_cast<std::uint32_t>(props.size());
    parent.first = first;
    // The parent may move during walk's appends, so walk works from the index
    // and a copy of the byte range.
    const std::uint32_t items = parent.items;
    const std::uint32_t items_end = parent.items_end;
    walk(index, items, items_end, first);
    prop_data& fresh = props[index];
    if (fresh.first + fresh.count > props.size()) {
        fresh.count = static_cast<std::uint32_t>(props.size() - fresh.first);
    }
}

std::uint32_t img_data::find(std::uint32_t index, std::string_view name) const
{
    ensure_children(index);
    if (index >= props.size()) {
        return none_index;
    }
    prop_data const& parent = props[index];
    if (parent.first == none_index) {
        return none_index;
    }
    std::uint32_t lo = parent.first;
    std::uint32_t hi = parent.first + parent.count;
    if (parent.flags & flag_sorted) {
        while (lo < hi) {
            const std::uint32_t mid = lo + (hi - lo) / 2;
            if (stored_name(props[mid]) < name) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo < parent.first + parent.count && stored_name(props[lo]) == name ? lo : none_index;
    }
    for (std::uint32_t i = lo; i < hi; ++i) {
        if (stored_name(props[i]) == name) {
            return i;
        }
    }
    return none_index;
}

prop_data const* img_data::prop(std::uint32_t index) const
{
    ensure();
    if (index >= props.size()) {
        return nullptr;
    }
    return &props[index];
}

std::string_view img_data::stored_name(prop_data const& p) const
{
    if (broken) {
        return {};
    }
    if (p.name_len > names.size() || p.name > names.size() - p.name_len) {
        return {};
    }
    return {&names[p.name], p.name_len};
}

std::string_view img_data::stored_value(prop_data const& p) const
{
    if (broken) {
        return {};
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(p.value);
    if (offset + 4 > values.size()) {
        return {};
    }
    std::uint32_t length = 0;
    for (int i = 0; i < 4; ++i) {
        length |= static_cast<std::uint32_t>(std::uint8_t(values[offset + i])) << (8 * i);
    }
    if (length > values.size() - offset - 4) {
        return {};
    }
    return {&values[offset + 4], length};
}

void img_data::read_field(std::uint32_t& p, std::int32_t& out) const
{
    if (broken || p >= size) {
        out = 0;
        p = size;
        return;
    }
    const std::int8_t head = s8(base + p);
    ++p;
    if (head != -128) {
        out = head;
        return;
    }
    if (p + 4 > size) {
        broken = true;
        out = 0;
        p = size;
        return;
    }
    out = i32le(base + p);
    p += 4;
}

void img_data::read_count(std::uint32_t& p, std::uint32_t& out) const
{
    std::int32_t value = 0;
    read_field(p, value);
    out = static_cast<std::uint32_t>(value < 0 ? 0 : value);
}

// ------------------------------------------------------------------ directories

void img_dir::ensure_listed() const
{
    if (listed) {
        return;
    }
    listed = true;
#ifndef _WIN32
    DIR* dir = ::opendir(path.c_str());
    if (!dir) {
        img_report_error(path.c_str(), "directory not found");
        return;
    }
    while (dirent const* item = ::readdir(dir)) {
        std::string const entry_name = item->d_name;
        if (entry_name == "." || entry_name == "..") {
            continue;
        }
        bool is_dir = item->d_type == DT_DIR;
        if (item->d_type == DT_UNKNOWN) {
            struct stat st;
            if (::stat(img_join(path, entry_name).c_str(), &st) == 0) {
                is_dir = S_ISDIR(st.st_mode);
            }
        }
        entries.push_back({entry_name, is_dir, nullptr, nullptr});
    }
    ::closedir(dir);
#else
    WIN32_FIND_DATAA found;
    std::string const pattern = img_join(path, "*");
    HANDLE handle = ::FindFirstFileA(pattern.c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        img_report_error(path.c_str(), "directory not found");
        return;
    }
    do {
        std::string const entry_name = found.cFileName;
        if (entry_name == "." || entry_name == "..") {
            continue;
        }
        entries.push_back({entry_name, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
                           nullptr, nullptr});
    } while (::FindNextFileA(handle, &found));
    ::FindClose(handle);
#endif
    std::sort(entries.begin(), entries.end(),
              [](img_dir::entry const& a, img_dir::entry const& b) {
                  return a.name < b.name;
              });
}

std::uint32_t img_dir::find(std::string_view name) const
{
    ensure_listed();
    auto const exact = std::lower_bound(entries.begin(), entries.end(), name,
                                        [](entry const& e, std::string_view key) {
                                            return std::string_view(e.name) < key;
                                        });
    if (exact != entries.end() && exact->name == name) {
        return static_cast<std::uint32_t>(exact - entries.begin());
    }
    // nx2img could not write every nx node name verbatim as a file name, so a lookup
    // has to pass through the same sanitising it used.
    std::string const safe = img_safe_name(name);
    auto const mapped = std::lower_bound(entries.begin(), entries.end(), safe,
                                         [](entry const& e, std::string_view key) {
                                             return std::string_view(e.name) < key;
                                         });
    if (mapped != entries.end() && mapped->name == safe) {
        return static_cast<std::uint32_t>(mapped - entries.begin());
    }
    // A directory miss means whole art is absent, unlike an optional property probe,
    // so it is worth one log line per name on a device.
    img_report_error((path + "/" + std::string(name)).c_str(), "no such entry");
    return none_index;
}

node img_dir::as_node() const
{
    return node(this);
}

node img_dir::entry_node(std::uint32_t index) const
{
    return node(this, index);
}

// -------------------------------------------------------------------- registries

namespace
{
std::mutex img_registry_mutex;
std::unordered_map<std::string, std::unique_ptr<img_dir>> img_dirs;
std::unordered_map<std::string, std::unique_ptr<img_data>> img_files;
} // namespace

img_dir const* img_dir_for(std::string path, img_dir const* category)
{
    std::lock_guard<std::mutex> lock(img_registry_mutex);
    auto const found = img_dirs.find(path);
    if (found != img_dirs.end()) {
        return found->second.get();
    }
    auto dir = std::make_unique<img_dir>();
    dir->path = path;
    dir->name = base_name(path);
    dir->category = category ? category : dir.get();
    auto* raw = dir.get();
    img_dirs.emplace(std::move(path), std::move(dir));
    return raw;
}

img_data const* img_file_for(std::string path, img_dir const* category)
{
    std::lock_guard<std::mutex> lock(img_registry_mutex);
    auto const found = img_files.find(path);
    if (found != img_files.end()) {
        return found->second.get();
    }
    auto file = std::make_unique<img_data>();
    file->path = std::move(path);
    file->name = base_name(file->path);
    file->category = category;
    auto* raw = file.get();
    img_files.emplace(std::string(raw->path), std::move(file));
    return raw;
}

std::string img_join(std::string const& dir, std::string const& name)
{
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        return dir + "/" + name;
    }
    return dir + name;
}

char const* img_illegal_chars()
{
    return "<>:\"/\\|?*";
}

std::vector<std::string> const& img_reserved_names()
{
    static std::vector<std::string> const names = [] {
        std::vector<std::string> out = {"con", "prn", "aux", "nul"};
        for (int i = 1; i < 10; ++i) {
            out.push_back("com" + std::to_string(i));
            out.push_back("lpt" + std::to_string(i));
        }
        return out;
    }();
    return names;
}

std::string img_safe_name(std::string_view name)
{
    std::string out;
    out.reserve(name.size());
    for (char const c : name) {
        out.push_back(c < 32 || std::strchr(img_illegal_chars(), c) ? '_' : c);
    }
    while (!out.empty() && out.back() == '.') {
        out.pop_back();
    }
    if (out.empty()) {
        return "_";
    }
    std::string stem = out;
    std::string suffix;
    if (out.size() > 4 && std::equal(out.end() - 4, out.end(), ".img")) {
        stem = out.substr(0, out.size() - 4);
        suffix = ".img";
    }
    std::string head = stem;
    for (auto& c : head) {
        c = static_cast<char>(std::tolower(static_cast<std::uint8_t>(c)));
    }
    auto const dot = head.find('.');
    if (std::find(img_reserved_names().begin(), img_reserved_names().end(),
                  dot == std::string::npos ? head : head.substr(0, dot))
        != img_reserved_names().end()) {
        stem = "_" + stem;
    }
    return stem + suffix;
}

void img_report_error(char const* file, char const* message)
{
    static std::mutex report_mutex;
    static std::unordered_map<std::string, int> reported;
    std::lock_guard<std::mutex> lock(report_mutex);
    if (++reported[file] == 1) {
#ifdef __ANDROID__
        // stderr is not visible in an Android logcat dump, which is the only way to
        // tell a missing resource from a wrong one on a device.
        __android_log_print(ANDROID_LOG_WARN, "imgwz", "%s: %s", message, file);
#else
        std::fprintf(stderr, "[imgwz] %s: %s\n", message, file);
        std::fflush(stderr);
#endif
    }
}
} // namespace img
