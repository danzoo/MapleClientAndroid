//	This file is part of the continued Journey MMORPG client
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton
//
//	This program is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Affero General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Affero General Public License for more details.
//
//	You should have received a copy of the GNU Affero General Public License
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.
#include "img_zip.hpp"

#ifdef _WIN32
// The zip reader is an Android-release facility; host builds keep the loose tree.
namespace imgzip {
bool open(std::string const&, std::string const&) {
    return false;
}
bool active() {
    return false;
}
bool lookup(std::string const&, std::uint64_t&, std::uint32_t&) {
    return false;
}
bool list_children(std::string const&, std::vector<std::pair<std::string, bool>>&) {
    return false;
}
int fd() {
    return -1;
}
}  // namespace imgzip
#else
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "img_internal.hpp"

namespace {
struct Entry {
    std::uint64_t local_off;
    std::uint32_t size;
    std::uint16_t name_length;
};

int g_fd = -1;
std::uint64_t g_data_end = 0;
std::string g_prefix;
std::unordered_map<std::string, Entry> g_entries;

std::uint16_t u16(unsigned char const* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t u32(unsigned char const* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
           | (static_cast<std::uint32_t>(p[2]) << 16)
           | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool pread_all(int fd, void* buf, std::size_t n, std::uint64_t off) {
    auto* c = static_cast<unsigned char*>(buf);
    while (n > 0) {
        const ssize_t r = ::pread(fd, c, n, static_cast<off_t>(off));
        if (r <= 0) {
            return false;
        }
        c += r;
        off += static_cast<std::uint64_t>(r);
        n -= static_cast<std::size_t>(r);
    }
    return true;
}

void fail(char const* why) {
    if (g_fd >= 0) {
        ::close(g_fd);
        g_fd = -1;
    }
    g_entries.clear();
    img::img_report_error("data.zip", why);
}
}  // namespace

namespace imgzip {
bool open(std::string const& zip_path, std::string const& root_prefix) {
    if (g_fd >= 0) {
        ::close(g_fd);
        g_fd = -1;
    }
    g_entries.clear();
    g_prefix = root_prefix;

    g_fd = ::open(zip_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (g_fd < 0) {
        img::img_report_error(zip_path.c_str(), "zip open failed");
        return false;
    }
    const off_t end = ::lseek(g_fd, 0, SEEK_END);
    if (end < 22) {
        fail("zip too small");
        return false;
    }
    const std::size_t tail = static_cast<std::size_t>(std::min<off_t>(end, 65557));
    std::vector<unsigned char> buf(tail);
    if (!pread_all(g_fd, buf.data(), tail, static_cast<std::uint64_t>(end) - tail)) {
        fail("zip tail read failed");
        return false;
    }
    std::size_t eocd = tail;
    for (std::size_t i = tail - 22;; --i) {
        if (u32(buf.data() + i) == 0x06054b50u) {
            eocd = i;
            break;
        }
        if (i == 0) {
            break;
        }
    }
    if (eocd == tail) {
        fail("no end of central directory");
        return false;
    }
    const std::uint16_t count = u16(buf.data() + eocd + 10);
    const std::uint32_t cd_size = u32(buf.data() + eocd + 12);
    const std::uint32_t cd_off = u32(buf.data() + eocd + 16);
    if (count == 0xFFFFu || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) {
        fail("zip64 not supported");
        return false;
    }
    const std::uint64_t directory_end = static_cast<std::uint64_t>(end) - tail + eocd;
    if (cd_off > directory_end || cd_size > directory_end - cd_off) {
        fail("central directory outside archive");
        return false;
    }
    g_data_end = cd_off;
    std::vector<unsigned char> cd(cd_size);
    if (!pread_all(g_fd, cd.data(), cd_size, cd_off)) {
        fail("central directory read failed");
        return false;
    }
    std::size_t p = 0;
    for (std::uint16_t n = 0; n < count; ++n) {
        if (p + 46 > cd.size() || u32(cd.data() + p) != 0x02014b50u) {
            fail("bad central directory entry");
            return false;
        }
        const std::uint16_t method = u16(cd.data() + p + 10);
        const std::uint32_t comp = u32(cd.data() + p + 20);
        const std::uint32_t uncomp = u32(cd.data() + p + 24);
        const std::uint16_t nlen = u16(cd.data() + p + 28);
        const std::uint16_t elen = u16(cd.data() + p + 30);
        const std::uint16_t clen = u16(cd.data() + p + 32);
        const std::uint32_t loff = u32(cd.data() + p + 42);
        const std::size_t entry_size = 46u + nlen + elen + clen;
        if (nlen == 0 || entry_size > cd.size() - p) {
            fail("truncated central directory entry");
            return false;
        }
        // Directory entries carry no payload; indexing them would let a directory probe
        // map padding bytes as if they were a file.
        if (cd[p + 46 + nlen - 1] == '/') {
            p += 46 + nlen + elen + clen;
            continue;
        }
        if (method != 0 || comp != uncomp) {
            fail("entry is not STORED");
            return false;
        }
        // Only the central directory is parsed here; the local header (whose extra field
        // can differ in length from the central one) is read lazily in lookup(), once per
        // opened file. Reading all 16k local headers up front costs seconds on FUSE.
        g_entries.emplace(std::string(reinterpret_cast<char const*>(cd.data() + p + 46), nlen),
                          Entry{ loff, uncomp, nlen });
        p += 46 + nlen + elen + clen;
    }
    return true;
}

bool active() {
    return g_fd >= 0;
}

bool lookup(std::string const& path, std::uint64_t& offset, std::uint32_t& size) {
    if (g_fd < 0 || path.size() <= g_prefix.size()
        || path.compare(0, g_prefix.size(), g_prefix) != 0) {
        return false;
    }
    const auto it = g_entries.find(path.substr(g_prefix.size() + 1));
    if (it == g_entries.end()) {
        return false;
    }
    const Entry& entry = it->second;
    unsigned char local[30];
    if (entry.local_off > g_data_end || sizeof(local) > g_data_end - entry.local_off
        || !pread_all(g_fd, local, sizeof(local), entry.local_off)
        || u32(local) != 0x04034b50u || u16(local + 8) != 0
        || (u16(local + 6) & 1u) != 0 || u16(local + 26) != entry.name_length) {
        return false;
    }
    // Local extra fields can differ from the central directory. Validate their real
    // span and the entire payload before img_data maps it, otherwise mmap can succeed
    // beyond EOF and fault later when the image parser touches the mapped bytes.
    const std::uint64_t payload = entry.local_off + sizeof(local)
                                  + u16(local + 26) + u16(local + 28);
    if (payload > g_data_end || entry.size == 0 || entry.size > g_data_end - payload) {
        return false;
    }
    offset = payload;
    size = entry.size;
    return true;
}

bool list_children(std::string const& path, std::vector<std::pair<std::string, bool>>& out) {
    if (g_fd < 0 || path.size() < g_prefix.size()
        || path.compare(0, g_prefix.size(), g_prefix) != 0) {
        return false;
    }
    // The category root itself maps to the empty relative directory.
    const std::string rel = path.size() == g_prefix.size()
                                ? std::string()
                                : path.substr(g_prefix.size() + 1) + '/';
    std::unordered_map<std::string, bool> kids;
    for (const auto& [name, entry] : g_entries) {
        if (name.compare(0, rel.size(), rel) != 0) {
            continue;
        }
        const std::string rest = name.substr(rel.size());
        const std::size_t slash = rest.find('/');
        if (slash == std::string::npos) {
            kids.emplace(rest, false);
        } else {
            kids[rest.substr(0, slash)] = true;
        }
    }
    out.assign(kids.begin(), kids.end());
    return true;
}

int fd() {
    return g_fd;
}
}  // namespace imgzip
#endif  // _WIN32