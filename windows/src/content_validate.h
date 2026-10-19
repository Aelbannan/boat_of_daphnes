// Checks on custom player content received from other players (through the Anchor server, so untrusted): a
// manifest, RARC archives (Link.arc), the J3D / texture files inside them, and the voice files. Every check is
// structural and bounded; nothing here writes a file or runs anything. Pure data code (rarc.cpp and picojson only),
// so tests/content_validate_test.cpp builds it alone. The threat model is docs/fork/anchor-content-security.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace content_validate {

// Limits (the server's, server/anchor/src/content.js, are the same numbers).
constexpr size_t kMaxFile = 20u * 1024 * 1024;     // one blob
constexpr size_t kMaxPack = 20u * 1024 * 1024;     // all the files of one manifest
constexpr size_t kMaxManifest = 64u * 1024;
constexpr size_t kMaxManifestFiles = 64;
constexpr size_t kMaxArchive = 32u * 1024 * 1024;  // a RARC after Yaz0
constexpr size_t kMaxMember = 8u * 1024 * 1024;    // one file inside a RARC
constexpr size_t kMaxMembers = 512;
constexpr size_t kMaxNodes = 64;
constexpr size_t kMaxStrings = 64u * 1024;
constexpr size_t kMaxVoiceInit = 2u * 1024 * 1024; // JaiInit.aaf
constexpr size_t kMaxVoiceWave = 8u * 1024 * 1024; // a .aw wave bank

// One file a manifest lists.
struct File {
    std::string path, role, sha256;
    size_t size = 0;
};

// What a player advertises. `parts` is absent in manifests of older clients: those hold only a Link.arc, of which
// only the body is used.
struct Manifest {
    std::string name;
    std::vector<File> files;
    bool has_parts = false;
    bool link = true;          // the body comes from the "model" file
    std::string equipment;     // "", "model" (the model file's) or "equipment" (the equipment file's)
    bool voice = false;
};

// The paths a file may have in a pack (the only ones ever downloaded or used) and the role each goes with.
bool AllowedPath(const std::string& path, const std::string& role);
// A relative path that stays inside its pack: no "..", no leading '/', no '\\' or drive colon, no control characters.
bool SafeRelPath(const std::string& path);
bool IsHex64(const std::string& s); // lower-case hex SHA-256

// Strict parse. Entries with a path or role that is not allowed are an error (a peer cannot make us look at them).
bool ParseManifest(const std::string& text, Manifest& out, std::string* error);

// Yaz0 or plain RARC bytes -> a plain RARC (size limits and the decompression bomb check in rarc::Yaz0Expand), then
// ValidateRarc. The bytes are replaced by the expanded archive.
bool ExpandArchive(std::vector<uint8_t>& data, std::string* error);
// A RARC: header, node and file tables, string table and every member's place inside the bounds; member names from a
// safe character set with an allowed extension; every member a well-formed J3D (bdl, bmd, bck, brk, btk) or BTI.
bool ValidateRarc(const std::vector<uint8_t>& arc, std::string* error);
// A J3D file ("J3D2bdl3", ...): the magic for the extension, the size, and chunks that fill it exactly.
bool ValidateJ3d(const std::vector<uint8_t>& file, const std::string& ext, std::string* error);
bool ValidateBti(const std::vector<uint8_t>& file, std::string* error);

// The body of a validated Link.arc: cl.bdl (42 joints are checked by the game side).
bool ExtractBody(const std::vector<uint8_t>& arc, std::vector<uint8_t>& bdl, std::string* error);
// The hands and the hair of the Outset clothes of a validated Link.arc (hands.bdl, katsura.bdl), those it has.
bool ExtractBodyParts(const std::vector<uint8_t>& arc, std::vector<uint8_t>& hands, std::vector<uint8_t>& hair);
// The puppet's equipment models of a validated Link.arc, by member name (those present).
bool ExtractEquipment(const std::vector<uint8_t>& arc, std::map<std::string, std::vector<uint8_t>>& models, std::string* error);

// JaiInit.aaf: the record stream the game's audio init walks, every offset and size inside the file. A wave bank
// (.aw) has no header at all (ADPCM frames the DSP decodes; any bytes decode): size and alignment only.
bool ValidateAaf(const std::vector<uint8_t>& file, std::string* error);
bool ValidateWave(const std::vector<uint8_t>& file, std::string* error);

} // namespace content_validate
