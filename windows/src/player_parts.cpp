// See player_parts.h.
#include "player_parts.h"

#include "rarc.h"

#include <algorithm>
#include <cctype>

namespace playerparts {
namespace {

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "cl.bdl" -> "cl", ".bdl"
void split_ext(const std::string& low, std::string& base, std::string& ext) {
    const size_t dot = low.rfind('.');
    base = dot == std::string::npos ? low : low.substr(0, dot);
    ext = dot == std::string::npos ? "" : low.substr(dot);
}

} // namespace

bool IsLinkMember(const std::string& name) {
    std::string base, ext;
    split_ext(lower(name), base, ext);
    if (ext == ".bdl")
        return base == "cl" || base == "katsura" || base == "hands" || base == "yamu" || base == "pring" ||
               base == "hboots" || base == "ymgcs00";
    if (ext == ".bti")
        return base == "linktexbci4";
    if (ext == ".brk")
        return base == "ymgcs00_ms" || base == "ymgcs00_ts";
    if (ext == ".btk")
        return base == "ymgcs00";
    return false;
}

bool IsEquipmentMember(const std::string& name) {
    std::string base, ext;
    split_ext(lower(name), base, ext);
    if (ext != ".bdl" && ext != ".bti" && ext != ".brk" && ext != ".btk")
        return false;
    return !IsLinkMember(name);
}

const std::vector<std::string>& PuppetEquipmentMembers() {
    static const std::vector<std::string> k = {"swgripa.bdl", "swgripms.bdl", "sha.bdl", "shms.bdl", "swa.bdl", "swms.bdl"};
    return k;
}

bool IsPuppetEquipmentMember(const std::string& name) {
    const std::string low = lower(name);
    const auto& k = PuppetEquipmentMembers();
    return std::find(k.begin(), k.end(), low) != k.end();
}

unsigned DetectArc(const std::vector<uint8_t>& mod_arc, const std::vector<uint8_t>& vanilla_arc, bool* readable) {
    std::vector<rarc::File> mine, theirs;
    if (readable != nullptr)
        *readable = false;
    if (!rarc::Parse(mod_arc, mine) || !rarc::Parse(vanilla_arc, theirs))
        return 0;
    if (readable != nullptr)
        *readable = true;
    unsigned parts = 0;
    for (const rarc::File& f : mine) {
        const bool link = IsLinkMember(f.name), equip = IsEquipmentMember(f.name);
        if (!link && !equip)
            continue;
        if ((parts & kLink || !link) && (parts & kEquipment || !equip))
            continue; // already known
        std::vector<uint8_t> a, b;
        const bool have_b = rarc::Extract(vanilla_arc, f.name, b);
        if (!rarc::Extract(mod_arc, f.name, a))
            continue;
        if (!have_b || a != b)
            parts |= link ? kLink : kEquipment;
    }
    return parts;
}

bool Compose(std::vector<uint8_t>& base, const std::vector<uint8_t>* link_donor, const std::vector<uint8_t>* equip_donor) {
    int n = 0;
    if (link_donor != nullptr && !rarc::ReplaceMembers(base, *link_donor, IsLinkMember, &n))
        return false;
    if (equip_donor != nullptr && !rarc::ReplaceMembers(base, *equip_donor, IsEquipmentMember, &n))
        return false;
    return true;
}

std::string Describe(unsigned parts) {
    std::string s;
    const auto add = [&](const char* w) { s += (s.empty() ? "" : " + ") + std::string(w); };
    if (parts & kLink)
        add("Link");
    if (parts & kEquipment)
        add("Equipment");
    if (parts & kVoice)
        add("Voice");
    return s.empty() ? "Nothing new" : s;
}

} // namespace playerparts
