//  img::node over a loose .img tree.  Every method mirrors NoLifeNx's node.cpp: a null
//  node is usable and returns defaults, iteration walks a contiguous run of children,
//  integer subscripts convert to decimal child names, and the casts convert between
//  stored types the same way the archive used to.
#include "node.hpp"

#include "audio.hpp"
#include "bitmap.hpp"
#include "img_internal.hpp"

#include <cstring>
#include <string>

namespace img
{
namespace
{
//! NoLifeNx parsed a number out of a string node with std::stoll/stod, which accept a
//! leading number and throw when there is none; the throw is turned back into the
//! caller's default here, because a crash was never the intent.
std::int64_t integer_from(std::string_view text, std::int64_t def)
{
    try {
        return std::stoll(std::string(text));
    } catch (std::exception const&) {
        return def;
    }
}

double real_from(std::string_view text, double def)
{
    try {
        return std::stod(std::string(text));
    } catch (std::exception const&) {
        return def;
    }
}
} // namespace

node::node(img_data const* file, std::uint32_t index)
    : m_data(file), m_index(index), m_space(space::prop)
{
}

node::node(img_dir const* directory)
    : m_dir(directory), m_index(self), m_space(space::dir)
{
}

node::node(img_dir const* directory, std::uint32_t index)
    : m_dir(directory), m_index(index), m_space(space::dir_child)
{
}

node node::resolve_space() const
{
    if (m_space == space::dir_child) {
        auto const* directory = static_cast<img_dir const*>(m_dir);
        directory->ensure_listed();
        if (m_index >= directory->entries.size()) {
            return node();
        }
        auto const& item = directory->entries[m_index];
        std::string const path = img_join(directory->path, item.name);
        if (item.is_dir) {
            if (!item.sub) {
                item.sub = img_dir_for(path, directory->category);
            }
            return node(item.sub);
        }
        if (!item.file) {
            item.file = img_file_for(path, directory->category);
        }
        return node(item.file, 0);
    }
    if (m_space == space::prop) {
        static_cast<img_data const*>(m_data)->ensure();
    }
    return *this;
}

bool node::as_property(img_data const*& file, prop_data const*& pd) const
{
    auto const self = resolve_space();
    if (self.m_space != space::prop) {
        return false;
    }
    file = static_cast<img_data const*>(self.m_data);
    pd = file->prop(self.m_index);
    return pd != nullptr;
}

node node::begin() const
{
    auto const self = resolve_space();
    switch (self.m_space) {
    case space::dir:
    {
        auto const* directory = static_cast<img_dir const*>(self.m_dir);
        directory->ensure_listed();
        return node(directory, 0);
    }
    case space::prop: {
        auto const* file = static_cast<img_data const*>(self.m_data);
        file->ensure_children(self.m_index);
        auto const* pd = file->prop(self.m_index);
        if (!pd || pd->first == none_index) {
            return self.end();
        }
        return node(file, pd->first);
    }
    default:
        return node();
    }
}

node node::end() const
{
    auto const self = resolve_space();
    switch (self.m_space) {
    case space::dir: {
        auto const* directory = static_cast<img_dir const*>(self.m_dir);
        directory->ensure_listed();
        return node(directory, static_cast<std::uint32_t>(directory->entries.size()));
    }
    case space::prop: {
        auto const* file = static_cast<img_data const*>(self.m_data);
        file->ensure_children(self.m_index);
        auto const* pd = file->prop(self.m_index);
        if (!pd || pd->first == none_index) {
            return node(file, self.m_index);
        }
        return node(file, pd->first + pd->count);
    }
    default:
        return node();
    }
}

node node::operator*() const
{
    return *this;
}

node& node::operator++()
{
    ++m_index;
    return *this;
}

node node::operator++(int)
{
    auto copy = *this;
    ++m_index;
    return copy;
}

bool node::operator==(node const& o) const
{
    return m_space == o.m_space && m_data == o.m_data && m_dir == o.m_dir
           && m_index == o.m_index;
}

bool node::operator!=(node const& o) const
{
    return !operator==(o);
}

node::operator bool() const
{
    return m_space != space::null;
}

node node::operator[](std::uint16_t n) const
{
    return operator[](std::to_string(n));
}

node node::operator[](std::int16_t n) const
{
    return operator[](std::to_string(n));
}

node node::operator[](std::uint32_t n) const
{
    return operator[](std::to_string(n));
}

node node::operator[](std::int32_t n) const
{
    return operator[](std::to_string(n));
}

node node::operator[](std::uint64_t n) const
{
    // 64-bit integers in string form have the possibility of not getting SSO,
    // so to ensure that no allocations occur we use `snprintf()`.
    char buf[21];
    auto len = std::snprintf(buf, 21, "%llu", static_cast<unsigned long long>(n));
    return operator[](std::string_view{buf, static_cast<std::string_view::size_type>(len)});
}

node node::operator[](std::int64_t n) const
{
    char buf[21];
    auto len = std::snprintf(buf, 21, "%lld", static_cast<long long>(n));
    return operator[](std::string_view{buf, static_cast<std::string_view::size_type>(len)});
}

node node::operator[](std::string_view o) const
{
    return get_child(o);
}

node node::operator[](char const* o) const
{
    return o ? get_child(std::string_view(o)) : node();
}

node node::operator[](node const& o) const
{
    return operator[](o.get_string());
}

node node::get_child(std::string_view o) const
{
    auto const self = resolve_space();
    switch (self.m_space) {
    case space::dir: {
        auto const* directory = static_cast<img_dir const*>(self.m_dir);
        auto const index = directory->find(o);
        return index == none_index ? node() : node(directory, index);
    }
    case space::prop: {
        auto const* file = static_cast<img_data const*>(self.m_data);
        auto const index = file->find(self.m_index, o);
        return index == none_index ? node() : node(file, index);
    }
    default:
        return node();
    }
}

node::operator unsigned char() const
{
    return static_cast<unsigned char>(get_integer());
}

node::operator signed char() const
{
    return static_cast<signed char>(get_integer());
}

node::operator unsigned short() const
{
    return static_cast<unsigned short>(get_integer());
}

node::operator signed short() const
{
    return static_cast<signed short>(get_integer());
}

node::operator unsigned int() const
{
    return static_cast<unsigned int>(get_integer());
}

node::operator signed int() const
{
    return static_cast<signed int>(get_integer());
}

node::operator unsigned long() const
{
    return static_cast<unsigned long>(get_integer());
}

node::operator signed long() const
{
    return static_cast<signed long>(get_integer());
}

node::operator unsigned long long() const
{
    return static_cast<unsigned long long>(get_integer());
}

node::operator signed long long() const
{
    return static_cast<signed long long>(get_integer());
}

node::operator float() const
{
    return static_cast<float>(get_real());
}

node::operator double() const
{
    return get_real();
}

node::operator long double() const
{
    return static_cast<long double>(get_real());
}

node::operator std::string() const
{
    return get_string();
}

node::operator vector() const
{
    return get_vector();
}

node::operator bitmap() const
{
    return get_bitmap();
}

node::operator audio() const
{
    return get_audio();
}

std::int64_t node::get_integer(std::int64_t def) const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    if (!as_property(file, pd)) {
        return def;
    }
    switch (static_cast<type>(pd->type)) {
    case type::integer:
        return static_cast<std::int64_t>(pd->value);
    case type::real: {
        double d = 0;
        std::memcpy(&d, &pd->value, 8);
        return static_cast<std::int64_t>(d);
    }
    case type::string:
        return integer_from(file->stored_value(*pd), def);
    default:
        return def;
    }
}

double node::get_real(double def) const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    if (!as_property(file, pd)) {
        return def;
    }
    switch (static_cast<type>(pd->type)) {
    case type::integer:
        return static_cast<double>(static_cast<std::int64_t>(pd->value));
    case type::real: {
        double d = 0;
        std::memcpy(&d, &pd->value, 8);
        return d;
    }
    case type::string:
        return real_from(file->stored_value(*pd), def);
    default:
        return def;
    }
}

std::string node::get_string(std::string def) const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    if (!as_property(file, pd)) {
        return def;
    }
    switch (static_cast<type>(pd->type)) {
    case type::string:
        return std::string(file->stored_value(*pd));
    case type::integer:
        return std::to_string(static_cast<std::int64_t>(pd->value));
    case type::real: {
        double d = 0;
        std::memcpy(&d, &pd->value, 8);
        return std::to_string(d);
    }
    default:
        return def;
    }
}

vector node::get_vector(vector def) const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    if (!as_property(file, pd) || static_cast<type>(pd->type) != type::vector) {
        return def;
    }
    return {static_cast<std::int32_t>(pd->value),
            static_cast<std::int32_t>(pd->value >> 32)};
}

bitmap node::get_bitmap() const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    if (!as_property(file, pd) || static_cast<type>(pd->type) != type::bitmap) {
        return {nullptr, 0, 0};
    }
    auto const index = static_cast<std::size_t>(pd->value);
    if (index >= file->canvases.size()) {
        return {nullptr, 0, 0};
    }
    auto const& canvas = file->canvases[index];
    return {&canvas, canvas.width, canvas.height};
}

audio node::get_audio() const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    if (!as_property(file, pd) || static_cast<type>(pd->type) != type::audio) {
        return audio();
    }
    auto const index = static_cast<std::size_t>(pd->value);
    if (index >= file->audios.size()) {
        return audio();
    }
    auto const& sound = file->audios[index];
    return {sound.data, sound.length};
}

bool node::get_bool() const
{
    return get_bool(false);
}

bool node::get_bool(bool def) const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    if (!as_property(file, pd)) {
        return def;
    }
    return static_cast<type>(pd->type) == type::integer && pd->value != 0;
}

std::int32_t node::x() const
{
    return get_vector().first;
}

std::int32_t node::y() const
{
    return get_vector().second;
}

std::string node::name() const
{
    switch (m_space) {
    case space::dir_child: {
        auto const* directory = static_cast<img_dir const*>(m_dir);
        directory->ensure_listed();
        if (m_index < directory->entries.size()) {
            return directory->entries[m_index].name;
        }
        return {};
    }
    case space::dir:
        return static_cast<img_dir const*>(m_dir)->name;
    case space::prop: {
        img_data const* file = nullptr;
        prop_data const* pd = nullptr;
        if (!as_property(file, pd)) {
            return {};
        }
        return std::string(file->stored_name(*pd));
    }
    default:
        return {};
    }
}

std::size_t node::size() const
{
    auto const self = resolve_space();
    if (self.m_space == space::dir) {
        auto const* directory = static_cast<img_dir const*>(self.m_dir);
        // A directory is only listed once it is asked for, and callers reach size()
        // without iterating, unlike begin()/end().
        directory->ensure_listed();
        return directory->entries.size();
    }
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    return self.as_property(file, pd) ? pd->count : 0;
}

node::type node::data_type() const
{
    img_data const* file = nullptr;
    prop_data const* pd = nullptr;
    return as_property(file, pd) ? static_cast<type>(pd->type) : type::none;
}

node node::root() const
{
    switch (m_space) {
    case space::prop:
        return node(static_cast<img_data const*>(m_data)->category);
    case space::dir: {
        auto const* directory = static_cast<img_dir const*>(m_dir);
        return node(directory->category ? directory->category : directory);
    }
    case space::dir_child:
        return resolve_space().root();
    default:
        return node();
    }
}

node node::resolve(std::string_view path) const noexcept
{
    std::size_t i = 0;
    std::size_t last_i = 0;
    node n = *this;
    while (i < path.length()) {
        if (path[i] == '/') {
            n = n[path.substr(last_i, i - last_i)];

            ++i;
            last_i = i;
        } else {
            ++i;
        }
    }
    n = n[path.substr(last_i, i - last_i)];

    return n;
}

std::string operator+(std::string s, node n)
{
    return s + n.get_string();
}

std::string operator+(char const* s, node n)
{
    return s + n.get_string();
}

std::string operator+(node n, std::string s)
{
    return n.get_string() + s;
}

std::string operator+(node n, char const* s)
{
    return n.get_string() + s;
}
} // namespace img
