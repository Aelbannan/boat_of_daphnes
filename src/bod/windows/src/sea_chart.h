// The Great Sea's 49 squares as the Sea Chart names them: room 1 is A1 at the top left, rooms run along each row
// (room 44 is B7, Outset Island). The developers' stage-select names number them in hex instead ("Sector 2C").
#pragma once

#include <cstdio>
#include <string>

namespace sea_chart {

inline const char* Island(int room) {
    static const char* const kIslands[49] = {
        "Forsaken Fortress", "Star Island", "Northern Fairy Island", "Gale Isle", "Crescent Moon Island",
        "Seven-Star Isles", "Overlook Island",
        "Four-Eye Reef", "Mother and Child Isles", "Spectacle Island", "Windfall Island", "Pawprint Isle",
        "Dragon Roost Island", "Flight Control Platform",
        "Western Fairy Island", "Rock Spire Isle", "Tingle Island", "Northern Triangle Island", "Eastern Fairy Island",
        "Fire Mountain", "Star Belt Archipelago",
        "Three-Eye Reef", "Greatfish Isle", "Cyclops Reef", "Six-Eye Reef", "Tower of the Gods",
        "Eastern Triangle Island", "Thorned Fairy Island",
        "Needle Rock Isle", "Islet of Steel", "Stone Watcher Island", "Southern Triangle Island", "Private Oasis",
        "Bomb Island", "Bird's Peak Rock",
        "Diamond Steppe Island", "Five-Eye Reef", "Shark Island", "Southern Fairy Island", "Ice Ring Isle",
        "Forest Haven", "Cliff Plateau Isles",
        "Horseshoe Island", "Outset Island", "Headstone Island", "Two-Eye Reef", "Angular Isles", "Boating Course",
        "Five-Star Isles",
    };
    return room >= 1 && room <= 49 ? kIslands[room - 1] : nullptr;
}

// "B7" for room 44; "" outside the chart.
inline std::string Square(int room) {
    if (room < 1 || room > 49)
        return "";
    char s[8];
    std::snprintf(s, sizeof s, "%c%d", 'A' + (room - 1) % 7, (room - 1) / 7 + 1);
    return s;
}

// "Outset Island (B7)"; "The Great Sea" outside the chart.
inline std::string Name(int room) {
    const char* island = Island(room);
    return island != nullptr ? std::string(island) + " (" + Square(room) + ")" : std::string("The Great Sea");
}

} // namespace sea_chart
