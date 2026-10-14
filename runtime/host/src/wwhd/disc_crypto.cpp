#include "disc_crypto.h"
#include "cancel.h"

#include <mbedtls/aes.h>
#include <mbedtls/sha1.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace wwhd {
namespace {
int hex_digit(unsigned char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
} // namespace

DiscKey read_disc_key(const std::string &path) {
  check_cancel();
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    throw FormatError("could not open the selected key file");
  const std::streamoff length = file.tellg();
  if (length < 0 || length > 1024)
    throw FormatError(
        "key files must contain 16 binary bytes or 32 hex digits");
  Bytes bytes(static_cast<size_t>(length));
  file.seekg(0);
  if (!bytes.empty())
    file.read(reinterpret_cast<char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!file)
    throw FormatError("could not read the selected key file");
  check_cancel();
  DiscKey key{};
  if (bytes.size() == key.size()) {
    std::copy(bytes.begin(), bytes.end(), key.begin());
    return key;
  }
  // Text key files commonly have a final newline or spaces between bytes.
  bytes.erase(std::remove_if(bytes.begin(), bytes.end(),
                             [](unsigned char c) { return std::isspace(c); }),
              bytes.end());
  if (bytes.size() != key.size() * 2)
    throw FormatError(
        "key files must contain 16 binary bytes or 32 hex digits");
  for (size_t i = 0; i < key.size(); ++i) {
    const int hi = hex_digit(bytes[2 * i]), lo = hex_digit(bytes[2 * i + 1]);
    if (hi < 0 || lo < 0)
      throw FormatError(
          "key files must contain 16 binary bytes or 32 hex digits");
    key[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return key;
}

Bytes disc_aes_cbc_decrypt(const DiscKey &key, const DiscKey &iv,
                           const Bytes &encrypted) {
  check_cancel();
  if (encrypted.size() > kMaxFile || encrypted.size() % 16 != 0)
    throw FormatError("invalid encrypted disc block size");
  if (encrypted.empty())
    return {};
  Bytes plain(encrypted.size());
  DiscKey working_iv = iv;
  mbedtls_aes_context context;
  mbedtls_aes_init(&context);
  struct Cleanup {
    mbedtls_aes_context* context;
    ~Cleanup() { mbedtls_aes_free(context); }
  } cleanup{&context};
  int error = mbedtls_aes_setkey_dec(&context, key.data(), 128);
  // CBC updates working_iv, preserving chaining across bounded chunks.
  for (size_t at = 0; error == 0 && at < encrypted.size(); at += 65536) {
    check_cancel();
    error = mbedtls_aes_crypt_cbc(&context, MBEDTLS_AES_DECRYPT,
                                  std::min(size_t{65536}, encrypted.size() - at), working_iv.data(),
                                  encrypted.data() + at, plain.data() + at);
  }
  if (error != 0)
    throw FormatError("could not decrypt the disc block");
  return plain;
}

DiscSha1 disc_sha1(const Bytes &bytes) {
  check_cancel();
  DiscSha1 digest{};
  mbedtls_sha1_context context;
  mbedtls_sha1_init(&context);
  struct Cleanup {
    mbedtls_sha1_context* context;
    ~Cleanup() { mbedtls_sha1_free(context); }
  } cleanup{&context};
  int error = mbedtls_sha1_starts_ret(&context);
  for (size_t at = 0; error == 0 && at < bytes.size(); at += 65536) {
    check_cancel();
    error = mbedtls_sha1_update_ret(&context, bytes.data() + at, std::min(size_t{65536}, bytes.size() - at));
  }
  if (error != 0 || mbedtls_sha1_finish_ret(&context, digest.data()) != 0)
    throw FormatError("could not check the disc block checksum");
  return digest;
}
} // namespace wwhd
