// What a player-model mod replaces, decided at the RARC entry level: Link (the body: wwrando's own list
// of the models that belong to the player, customizer.py revert_item_models_in_arc_to_original), Equipment
// (every other model, texture and animation in Link.arc: sword, shield, bow, boomerang, ...) and Voice (the
// sound\ files). Pure data code with no game or UI dependencies, so tests/player_parts_test.cpp builds it alone.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace playerparts {

enum Part : unsigned { kLink = 1, kEquipment = 2, kVoice = 4 };

// A member of Link.arc (case ignored). Bone animations (.bck) are neither: the game's own are always kept
// (customizer.py:313-320).
bool IsLinkMember(const std::string& name);
bool IsEquipmentMember(const std::string& name);
// The models a puppet draws as equipment: the sword's grip and blade, the Master Sword's, both shields.
bool IsPuppetEquipmentMember(const std::string& name);
const std::vector<std::string>& PuppetEquipmentMembers(); // swgripa.bdl, swgripms.bdl, sha.bdl, shms.bdl, swa.bdl, swms.bdl

// Which of Link and Equipment a mod's Link.arc (Yaz0 already expanded) holds: the parts where at least one member
// differs from the vanilla archive's. *readable is false when either is not a RARC this code can read.
unsigned DetectArc(const std::vector<uint8_t>& mod_arc, const std::vector<uint8_t>& vanilla_arc, bool* readable);

// base (a Link.arc) with the Link members of link_donor and the Equipment members of equip_donor put in; a donor
// may be null. False when an archive cannot be read.
bool Compose(std::vector<uint8_t>& base, const std::vector<uint8_t>* link_donor, const std::vector<uint8_t>* equip_donor);

// "Link + Equipment + Voice" for the menu.
std::string Describe(unsigned parts);

} // namespace playerparts
