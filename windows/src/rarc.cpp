// See rarc.h. Layout: a 0x20-byte header (magic, size, header size, data
// offset after the header, data length, main-RAM, ARAM and DVD lengths), the
// info block (node count and offset, file entry count and offset, string table
// length and offset, all after the header), then 0x14-byte file entries (id,
// hash, type << 8 | name offset high bits, name offset, data offset, size).
#include "rarc.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace rarc {
namespace {

uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
    return static_cast<uint32_t>(b[at]) << 24 | static_cast<uint32_t>(b[at + 1]) << 16 |
           static_cast<uint32_t>(b[at + 2]) << 8 | b[at + 3];
}
uint16_t be16(const std::vector<uint8_t>& b, size_t at) { return static_cast<uint16_t>(b[at] << 8 | b[at + 1]); }
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = static_cast<uint8_t>(v >> 24);
    b[at + 1] = static_cast<uint8_t>(v >> 16);
    b[at + 2] = static_cast<uint8_t>(v >> 8);
    b[at + 3] = static_cast<uint8_t>(v);
}

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Layout {
    size_t entries = 0, strings = 0, data = 0;
    uint32_t count = 0, string_size = 0, data_length = 0;
};

bool layout(const std::vector<uint8_t>& b, Layout& l) {
    if (b.size() < 0x40 || be32(b, 0) != 0x52415243u /* RARC */ || be32(b, 8) != 0x20u)
        return false;
    l.data = 0x20u + be32(b, 0x0C);
    l.data_length = be32(b, 0x10);
    // Only archives kept whole in main RAM (no ARAM or DVD parts).
    if (be32(b, 0x14) != l.data_length || be32(b, 0x18) != 0u || be32(b, 0x1C) != 0u)
        return false;
    l.count = be32(b, 0x28);
    l.entries = 0x20u + be32(b, 0x2C);
    l.string_size = be32(b, 0x30);
    l.strings = 0x20u + be32(b, 0x34);
    return l.entries + static_cast<size_t>(l.count) * 0x14u <= b.size() && l.strings + l.string_size <= b.size() &&
           l.data + l.data_length <= b.size();
}

} // namespace

bool Parse(const std::vector<uint8_t>& arc, std::vector<File>& files) {
    Layout l;
    if (!layout(arc, l))
        return false;
    files.clear();
    for (uint32_t i = 0; i < l.count; ++i) {
        const size_t e = l.entries + i * 0x14u;
        const uint8_t type = arc[e + 4];
        if (type & 0x02u) // a folder
            continue;
        const uint16_t name_offset = be16(arc, e + 6);
        if (name_offset >= l.string_size)
            return false;
        File f;
        const char* name = reinterpret_cast<const char*>(&arc[l.strings + name_offset]);
        f.name.assign(name, strnlen(name, l.string_size - name_offset));
        f.entry = i;
        f.offset = be32(arc, e + 8);
        f.size = be32(arc, e + 12);
        if (static_cast<uint64_t>(f.offset) + f.size > l.data_length)
            return false;
        files.push_back(f);
    }
    return true;
}

bool ReplaceMembers(std::vector<uint8_t>& arc, const std::vector<uint8_t>& donor,
                    const std::function<bool(const std::string&)>& pick, int* replaced) {
    *replaced = 0;
    std::vector<File> mine, theirs;
    Layout l;
    if (!Parse(arc, mine) || !Parse(donor, theirs) || !layout(arc, l))
        return false;
    Layout dl;
    layout(donor, dl);
    std::map<std::string, const File*> by_name;
    for (const auto& f : theirs)
        by_name[lower(f.name)] = &f;
    // The data again, file by file in their data order, 32-byte aligned.
    std::sort(mine.begin(), mine.end(), [](const File& a, const File& b) { return a.offset < b.offset; });
    std::vector<uint8_t> data;
    for (auto& f : mine) {
        const uint8_t* bytes = &arc[l.data + f.offset];
        uint32_t size = f.size;
        if (pick(f.name)) {
            const auto it = by_name.find(lower(f.name));
            if (it != by_name.end()) {
                bytes = &donor[dl.data + it->second->offset];
                size = it->second->size;
                ++*replaced;
            }
        }
        f.offset = static_cast<uint32_t>(data.size());
        f.size = size;
        data.insert(data.end(), bytes, bytes + size);
        data.resize((data.size() + 31u) & ~size_t{31});
    }
    if (*replaced == 0)
        return true;
    std::vector<uint8_t> out(arc.begin(), arc.begin() + static_cast<std::ptrdiff_t>(l.data));
    out.insert(out.end(), data.begin(), data.end());
    for (const auto& f : mine) {
        const size_t e = l.entries + f.entry * 0x14u;
        put32(out, e + 8, f.offset);
        put32(out, e + 12, f.size);
    }
    const uint32_t length = static_cast<uint32_t>(data.size());
    put32(out, 0x04, static_cast<uint32_t>(out.size()));
    put32(out, 0x10, length);
    put32(out, 0x14, length);
    arc = std::move(out);
    return true;
}

bool Yaz0Expand(std::vector<uint8_t>& data) {
    if (data.size() < 16 || be32(data, 0) != 0x59617A30u /* Yaz0 */)
        return false;
    const uint32_t size = be32(data, 4);
    std::vector<uint8_t> out;
    out.reserve(size);
    size_t at = 16;
    while (out.size() < size) {
        if (at >= data.size())
            return false;
        const uint8_t codes = data[at++];
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            if (codes & (1u << bit)) {
                if (at >= data.size())
                    return false;
                out.push_back(data[at++]);
                continue;
            }
            if (at + 1 >= data.size())
                return false;
            const uint8_t b0 = data[at++], b1 = data[at++];
            const size_t distance = (static_cast<size_t>(b0 & 0x0Fu) << 8 | b1) + 1u;
            size_t count = b0 >> 4;
            if (count == 0) {
                if (at >= data.size())
                    return false;
                count = static_cast<size_t>(data[at++]) + 0x12u;
            } else {
                count += 2u;
            }
            if (distance > out.size())
                return false;
            for (size_t i = 0; i < count && out.size() < size; ++i)
                out.push_back(out[out.size() - distance]);
        }
    }
    data = std::move(out);
    return true;
}

bool Extract(const std::vector<uint8_t>& arc, const std::string& name, std::vector<uint8_t>& out) {
    std::vector<File> files;
    Layout l;
    if (!Parse(arc, files) || !layout(arc, l))
        return false;
    const std::string want = lower(name);
    for (const File& f : files)
        if (lower(f.name) == want) {
            out.assign(arc.begin() + static_cast<std::ptrdiff_t>(l.data + f.offset),
                       arc.begin() + static_cast<std::ptrdiff_t>(l.data + f.offset + f.size));
            return true;
        }
    return false;
}

bool ValidateLinkArc(const std::vector<uint8_t>& arc, std::vector<uint8_t>* body) {
    std::vector<uint8_t> bdl;
    if (arc.size() > 32u * 1024u * 1024u || !Extract(arc, "cl.bdl", bdl))
        return false;
    // "J3D2bdl3" / "J3D2bdl4", then the file's size, which must be the member's (or a little less: padding).
    if (bdl.size() < 0x40 || bdl[0] != 'J' || bdl[1] != '3' || bdl[2] != 'D' || bdl[3] != '2' || bdl[4] != 'b' ||
        bdl[5] != 'd' || bdl[6] != 'l' || (bdl[7] != '3' && bdl[7] != '4'))
        return false;
    const uint32_t size = be32(bdl, 8);
    if (size > bdl.size() || size + 64u < bdl.size() || size < 0x40)
        return false;
    // The J3D file: header (magic, size, chunk count at 0x0C), then that many chunks (tag, size) that must
    // exactly fill the file, each at least its own 8-byte header: what the game's loader walks.
    const uint32_t chunks = be32(bdl, 12);
    if (chunks == 0 || chunks > 32)
        return false;
    size_t at = 0x20;
    for (uint32_t i = 0; i < chunks; ++i) {
        if (at + 8 > size)
            return false;
        const uint32_t chunk = be32(bdl, at + 4);
        if (chunk < 8 || chunk > size - at)
            return false;
        at += chunk;
    }
    if (at != size)
        return false;
    if (body != nullptr)
        *body = std::move(bdl);
    return true;
}

} // namespace rarc
