// Reads the player-provided USA Wind Waker HD WUD/WUX without extracting it.
#pragma once
#include "disc_image.h"
#include "disc_crypto.h"
#include <optional>
#include <memory>
namespace wwhd {
struct WiiUDiscKeys {
    DiscKey disc{};
    std::optional<DiscKey> title;
    std::optional<DiscKey> common;
};
struct WiiUDiscEntry {
    std::string path;
    uint64_t offset = 0, size = 0;
    uint16_t cluster = 0, flags = 0;
};
class WiiUDisc {
  public:
    WiiUDisc(const std::string& path, const WiiUDiscKeys& keys);
    ~WiiUDisc();
    const std::vector<WiiUDiscEntry>& entries() const;
    Bytes read(const WiiUDiscEntry& entry);
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace wwhd
