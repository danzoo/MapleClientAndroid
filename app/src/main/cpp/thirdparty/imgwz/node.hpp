//  imgwz - a reader for loose client .img trees, written as a drop-in replacement
//  for the NoLifeNx API this client used to link against.  Everything below keeps
//  NoLifeNx's exact public shape (null nodes, implicit casts, iteration protocol,
//  root()/resolve()) so no game file had to change when the container format did.
#pragma once
#include "nxfwd.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace nl
{
struct img_data;
struct img_dir;
struct prop_data;
typedef std::pair<std::int32_t, std::int32_t> vector;
class node
{
public:
    //! Type of node data.
    enum class type : std::uint16_t {
        none = 0,
        integer = 1,
        real = 2,
        string = 3,
        vector = 4,
        bitmap = 5,
        audio = 6,
    };

    // Constructors.
    node() = default;
    node(node const&) = default;
    node& operator=(node const&) = default;
    // These methods are primarily so nodes can be used as iterators and
    // iterated over.
    node begin() const;
    node end() const;
    node operator*() const;
    node& operator++();
    node operator++(int);
    bool operator==(node const&) const;
    bool operator!=(node const&) const;
    //! This checks whether or not the node points to an actual node.
    //! Even if the node ends up being a null node, you can still use it
    //! safely; it'll just fall back to returning default values and more null
    //! nodes.
    explicit operator bool() const;
    // Methods to access the children of the node by name.
    // Note that the versions taking integers convert the integer to a string.
    // They do not access the children by their integer index.
    // If you wish to do that, use `some_node.begin() + integer_index`.
    node operator[](std::uint16_t) const;
    node operator[](std::int16_t) const;
    node operator[](std::uint32_t) const;
    node operator[](std::int32_t) const;
    node operator[](std::uint64_t) const;
    node operator[](std::int64_t) const;
    node operator[](std::string_view) const;
    node operator[](const char*) const;
    //! This method uses the string value of the node, not the node's name
    node operator[](node const&) const;
    // Operators to easily cast a node to get the data.
    // Allows things like `std::string s = some_node;`.
    // Will automatically cast between data types as needed.
    operator unsigned char() const;
    operator signed char() const;
    operator unsigned short() const;
    operator signed short() const;
    operator unsigned int() const;
    operator signed int() const;
    operator unsigned long() const;
    operator signed long() const;
    operator unsigned long long() const;
    operator signed long long() const;
    operator float() const;
    operator double() const;
    operator long double() const;
    operator std::string() const;
    operator vector() const;
    operator bitmap() const;
    operator audio() const;
    // Explicitly called versions of all the operators.
    // When it takes a parameter, that is used as the default value
    // in the case that a suitable data value cannot be found in the node.
    std::int64_t get_integer(std::int64_t = 0) const;
    double get_real(double = 0) const;
    std::string get_string(std::string = "") const;
    vector get_vector(vector = {0, 0}) const;
    bitmap get_bitmap() const;
    audio get_audio() const;
    bool get_bool() const;
    bool get_bool(bool) const;
    //! Returns the x coordinate of the vector data value.
    std::int32_t x() const;
    //! Returns the y coordinate of the vector data value.
    std::int32_t y() const;
    //! The name of the node.
    std::string name() const;
    //! The number of children in the node.
    std::size_t size() const;
    //! Gets the type of data contained within the node.
    type data_type() const;
    //! Returns the root node of the category this node was derived from.
    node root() const;
    //! Takes a '/' separated string view, and resolves the given path.
    node resolve(std::string_view) const noexcept;

private:
    enum class space : std::uint8_t {
        null,       // nothing
        prop,       // one property of a parsed .img
        dir,        // a directory of the Data tree
        dir_child,  // the index-th child of a listed directory
    };
    static constexpr std::uint32_t self = 0xFFFFFFFFu;

    node(img_data const* file, std::uint32_t index);
    node(img_dir const* directory);
    node(img_dir const* directory, std::uint32_t index);

    // Every accessor funnels through this: a `dir_child` slot stands for the
    // node it holds, which is a property node or a directory of its own.
    node resolve_space() const;
    //! Resolves this node and, when it turns out to be a property of a mapped .img,
    //! hands back the file and the record.  Every value accessor starts with this.
    bool as_property(img_data const*& file, prop_data const*& pd) const;
    node get_child(std::string_view o) const;
    std::int64_t to_integer() const;
    double to_real() const;
    std::string to_string() const;
    vector to_vector() const;
    bitmap to_bitmap() const;
    audio to_audio() const;
    // Internal variables
    void const* m_data = nullptr;
    void const* m_dir = nullptr;
    std::uint32_t m_index = self;
    space m_space = space::null;
    friend struct img_data;
    friend struct img_dir;
};
// More convenience string concatenation operators
std::string operator+(std::string, node);
std::string operator+(char const*, node);
std::string operator+(node, std::string);
std::string operator+(node, char const*);
} // namespace nl
