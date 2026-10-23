#include "doc/fib.h"

#include <cstddef>
namespace doccraft::doc {
namespace {
uint16_t U16(std::span<const uint8_t> b, size_t p) {
  return uint16_t(b[p]) | uint16_t(b[p + 1]) << 8;
}
uint32_t U32(std::span<const uint8_t> b, size_t p) {
  return uint32_t(U16(b, p)) | uint32_t(U16(b, p + 2)) << 16;
}
}  // namespace
std::expected<Fib, Error> ParseFib(std::span<const uint8_t> word) {
  if (word.size() < 34) return std::unexpected(Error{"truncated FIB base"});
  if (U16(word, 0) != 0xA5EC)
    return std::unexpected(Error{"invalid WordDocument signature"});
  Fib f;
  f.n_fib = U16(word, 2);
  if (f.n_fib < 0x00C1 || f.n_fib > 0x011A)
    return std::unexpected(Error{"unsupported nFib version"});
  const uint16_t flags = U16(word, 10);
  f.complex = flags & 0x0004;
  f.encrypted = flags & 0x0100;
  f.which_table = flags & 0x0200;
  if (f.encrypted)
    return std::unexpected(Error{"encrypted document unsupported (M6)"});
  f.fc_min = U32(word, 24);
  f.fc_mac = U32(word, 28);
  const size_t lw_count_pos = 34u + size_t(U16(word, 32)) * 2;
  if (lw_count_pos + 2 > word.size())
    return std::unexpected(Error{"truncated FIB csw"});
  const uint16_t cslw = U16(word, lw_count_pos);
  const size_t lw = lw_count_pos + 2;
  if (size_t(cslw) * 4 > word.size() - lw || cslw < 11)
    return std::unexpected(Error{"truncated FIB cslw"});
  f.ccp_text = U32(word, lw + 12);
  f.ccp_ftn = U32(word, lw + 16);
  f.ccp_hdd = U32(word, lw + 20);
  f.ccp_atn = U32(word, lw + 28);
  f.ccp_edn = U32(word, lw + 32);
  f.ccp_txbx = U32(word, lw + 36);
  f.ccp_hdr_txbx = U32(word, lw + 40);
  const size_t pair_count_pos = lw + size_t(cslw) * 4;
  if (pair_count_pos + 2 > word.size())
    return std::unexpected(Error{"truncated FIB pairs"});
  const uint16_t pairs = U16(word, pair_count_pos);
  const size_t pair_start = pair_count_pos + 2;
  if (size_t(pairs) * 8 > word.size() - pair_start || pairs < 34)
    return std::unexpected(Error{"truncated FIB fc/lcb pairs"});
  f.fc_clx = U32(word, pair_start + 33 * 8);
  f.lcb_clx = U32(word, pair_start + 33 * 8 + 4);
  const size_t new_pos = pair_start + size_t(pairs) * 8;
  if (new_pos + 2 <= word.size()) {
    const size_t csw_new = U16(word, new_pos);
    if (csw_new && csw_new * 2 <= word.size() - new_pos - 2) {
      f.n_fib = U16(word, new_pos + 2);
      if (f.n_fib < 0x00C1 || f.n_fib > 0x011A)
        return std::unexpected(Error{"unsupported nFib version"});
    }
  }
  return f;
}
}  // namespace doccraft::doc
