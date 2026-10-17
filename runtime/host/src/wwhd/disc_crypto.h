// Local-disc AES-128-CBC and SHA-1 helpers, using the pinned Mbed TLS sources.
#pragma once
#include "wwhd.h"
#include <array>
namespace wwhd {
using DiscKey = std::array<uint8_t, 16>;
using DiscSha1 = std::array<uint8_t, 20>;
DiscKey read_disc_key(const std::string &path);
Bytes disc_aes_cbc_decrypt(const DiscKey &key, const DiscKey &iv,
                           const Bytes &encrypted);
DiscSha1 disc_sha1(const Bytes &bytes);
} // namespace wwhd
