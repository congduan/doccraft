#ifndef DOCCRAFT_DOC_FIB_H_
#define DOCCRAFT_DOC_FIB_H_
#include <cstdint>
#include <expected>
#include <span>
#include <string>
namespace doccraft::doc {
struct Error {
  std::string message;
};
struct Fib {
  uint16_t n_fib = 0;
  bool complex = false;
  bool encrypted = false;
  bool which_table = false;
  uint32_t ccp_text = 0, ccp_ftn = 0, ccp_hdd = 0, ccp_atn = 0;
  uint32_t ccp_edn = 0, ccp_txbx = 0, ccp_hdr_txbx = 0;
  uint32_t fc_clx = 0, lcb_clx = 0;
  std::string table_stream() const { return which_table ? "1Table" : "0Table"; }
};
std::expected<Fib, Error> ParseFib(std::span<const uint8_t> word);
}  // namespace doccraft::doc
#endif
