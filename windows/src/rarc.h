// A RARC archive's members, read and replaced in memory: what wwrando's model
// installer does to a custom Link.arc (customizer.py:313-340: bone animations
// reverted to the game's).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rarc {

struct File {
    std::string name;
    uint32_t entry = 0; // the file entry's index
    uint32_t offset = 0, size = 0; // in the data section
};

// The archive's files (not its folders); false when it is not a RARC this
// code can rewrite (its data must be all main-RAM, as Link.arc's is).
bool Parse(const std::vector<uint8_t>& arc, std::vector<File>& files);

// Each member of arc that pick accepts takes the bytes of the member of the
// same name (case ignored) in donor; the data section is laid out again.
// The count replaced goes to *replaced. False (arc unchanged) when either
// archive cannot be read.
bool ReplaceMembers(std::vector<uint8_t>& arc, const std::vector<uint8_t>& donor,
                    const std::function<bool(const std::string&)>& pick, int* replaced);

// Yaz0 (the disc's compression): data expanded in place; true when it was
// Yaz0 and expanded, false when it is not Yaz0 (unchanged) or is damaged.
bool Yaz0Expand(std::vector<uint8_t>& data);

// The bytes of one member (case ignored); false when the archive cannot be read or has no such file.
bool Extract(const std::vector<uint8_t>& arc, const std::string& name, std::vector<uint8_t>& out);

// A custom Link.arc, checked before anything uses it: a RARC this code can read whose body model
// (cl.bdl) is a J3D binary display list model (magic "J3D2bdl3" or "J3D2bdl4", sizes inside the file).
// On success the body model's bytes go to *body. Nothing in the archive is ever run.
bool ValidateLinkArc(const std::vector<uint8_t>& arc, std::vector<uint8_t>* body);

} // namespace rarc
