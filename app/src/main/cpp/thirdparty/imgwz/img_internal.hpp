//  imgwz internals: the mapped .img file and its lazily materialized property arena,
//  the directory pseudo-nodes that stand in for the old archive roots, and the
//  string keystream tables.  None of this is part of the game-facing API.
#pragma once
#include "node.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace img
{
constexpr std::uint32_t none_index = 0xFFFFFFFFu;

//! Flags stored per property.
enum prop_flags : std::uint16_t {
    //! The children came out in byte-wise name order, which lets find() binary search.
    flag_sorted = 2,
    //! The children have already been appended to `props`.
    flag_parsed = 4,
};

//! One property record.  `props` is append-only and each level is appended in one
//! block, so a node's identity is (file, index) and survives deeper parses.
struct prop_data {
    std::uint32_t name = 0;    // offset into img_data::names
    std::uint32_t items = 0;   // first byte of the encoded children, 0 if none
    std::uint32_t items_end = 0;
    std::uint32_t first = none_index; // index of the first child in img_data::props
    std::uint64_t value = 0;   // integer, real bits, arena offset, vector pair, or blob index
    std::uint32_t count = 0;   // child count as stored by the file
    std::uint16_t name_len = 0;
    std::uint16_t type = 0;    // node::type
    std::uint16_t flags = 0;
};

//! Decoded canvas location.  These addresses are handed out as img::bitmap::id(),
//! which the texture atlas caches on, so they live in a deque and never move.
struct canvas_data {
    std::uint8_t const* payload = nullptr; // deflate stream, 2-byte zlib header included
    std::uint32_t csize = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t format = 0;              // 1 = 16-bit BGRA4444, 2 = 32-bit BGRA
};

//! The WaveFormat preamble plus the encoded bytes, contiguous in the file and
//! byte-for-byte what the nx archive used to hand out.
struct audio_data {
    std::uint8_t const* data = nullptr;
    std::uint32_t length = 0;
};

//! One mapped .img.  `props[0]` is the file itself, so a file behaves like the image
//! node it replaced inside an nx archive.
struct img_data {
    std::string path;
    std::string name; // file name, including the ".img" the game's lookups use
    img_dir const* category = nullptr; // the archive root this file lived under
    mutable std::uint8_t const* base = nullptr;
    mutable std::uint32_t size = 0;

    mutable std::vector<char> names;   // property names, UTF-8, length-prefixed
    mutable std::vector<char> values;  // string values, UTF-8, length-prefixed
    mutable std::vector<prop_data> props;
    mutable std::deque<canvas_data> canvases;
    mutable std::deque<audio_data> audios;
    mutable bool opened = false;
    mutable bool broken = false;

    //! Maps the file and reads the file record itself.
    void ensure() const;
    //! Materializes the children of one property.
    void ensure_children(std::uint32_t index) const;
    //! The index of the child named `name`, or none_index.
    std::uint32_t find(std::uint32_t index, std::string_view name) const;
    prop_data const* prop(std::uint32_t index) const;
    std::string_view stored_name(prop_data const& property) const;
    std::string_view stored_value(prop_data const& property) const;

    //! Raw field readers over the mapped buffer.  Every one returns the offset just
    //! past what it consumed and marks the file broken instead of running off the end.
    std::uint32_t read_string(std::uint32_t p, std::string& out) const;
    std::uint32_t read_name(std::uint32_t p, std::string& out) const;
    std::uint32_t read_canvas(std::uint32_t p, canvas_data& out) const;
    std::uint32_t read_sound(std::uint32_t p, audio_data& out) const;
    //! The file's compressed int: one byte, or 0x80 followed by i32.
    void read_field(std::uint32_t& p, std::int32_t& out) const;
    void read_count(std::uint32_t& p, std::uint32_t& out) const;
    //! Offset just past the one property record that starts at `p`.
    std::uint32_t skip_entry(std::uint32_t p) const;
    //! Offset just past `count` records starting at `p`.
    std::uint32_t skip_entries(std::uint32_t p, std::uint32_t count) const;
    //! Parses every record of one property list, appending to `props` from `first`.
    void walk(std::uint32_t parent, std::uint32_t p, std::uint32_t end, std::uint32_t first) const;
    //! Parses one property record into the arena and returns the next offset.
    std::uint32_t parse(std::uint32_t p) const;

    std::uint32_t store_name(std::string_view text) const;
    std::uint32_t store_value(std::string_view text) const;
    std::uint32_t store_canvas(canvas_data canvas) const;
    std::uint32_t store_audio(audio_data audio) const;
};

//! A directory of the Data tree.  Its entries are exactly the node names the nx
//! archive used to hold: nx directory nodes became directories, and nx image nodes
//! (whose names end in ".img") became files, so an entry's on-disk name is the node
//! name and lookups never have to guess a suffix.
struct img_dir {
    struct entry {
        std::string name;
        bool is_dir = false;
        mutable img_dir const* sub = nullptr;
        mutable img_data const* file = nullptr;
    };

    std::string path;
    std::string name;
    img_dir const* category = nullptr; // itself for a category root
    mutable std::vector<entry> entries;
    mutable bool listed = false;

    void ensure_listed() const;
    std::uint32_t find(std::string_view name) const;
    //! This directory as the node the nx archive root used to be.
    node as_node() const;
    //! The node an entry stands for.
    node entry_node(std::uint32_t index) const;
};

//! The registries.  Mappings and parsed trees are kept for the life of the process
//! because bitmap identity (the atlas cache key) and BASS' memory streams are
//! pointers into them, exactly as they were with mmapped nx archives.
img_dir const* img_dir_for(std::string path, img_dir const* category);
img_data const* img_file_for(std::string path, img_dir const* category);
std::string img_join(std::string const& dir, std::string const& name);

//! The file name nx2img wrote for an nx node name; on-disk names are this, so a
//! game-side lookup has to pass through the same transformation.
char const* img_illegal_chars();
std::vector<std::string> const& img_reserved_names();
std::string img_safe_name(std::string_view name);

//! Printed once per unreadable file; the game just sees empty nodes.
void img_report_error(char const* file, char const* message);

//! The string keystream for the region the tree was written with.  Every string
//! restarts the stream at offset 0, so decrypting is one XOR with a table.
struct cipher
{
    mutable std::vector<std::uint8_t> ascii;  // ks[i] ^ (0xAA + i)
    mutable std::vector<std::uint16_t> utf16; // (ks[2i] | ks[2i+1] << 8) ^ (0xAAAA + i)

    //! Extends both tables to cover `length` bytes (UTF-16 needs two per character).
    void ensure(std::size_t length) const;

    static cipher const& get();
};

//! The IV v209/img was generated with (`nx2img.py --iv gms`).
constexpr std::uint32_t img_iv = 0x4D23C72Bu;
//! The fixed preamble every wz Sound_DX8 property stores in front of the serialised
//! WAVEFORMATEXTENSIBLE; the byte after it is the format length.
constexpr std::uint32_t sound_header_len = 51;
} // namespace img
