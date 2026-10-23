// M1 placeholders — implementation lands with Codex task M1 (TDD: the tests
// under tests/ define the contract; these stubs keep the build green).

#include "cfb/cfb_reader.h"

namespace doccraft::cfb {

std::expected<CfbReader, Error> CfbReader::Open(
    std::span<const uint8_t> /*bytes*/) {
  return std::unexpected(
      Error{ErrorCode::kNotCompoundFile, "not implemented (M1)"});
}

std::expected<std::vector<uint8_t>, Error> CfbReader::ReadStream(
    std::string_view /*name*/) const {
  return std::unexpected(
      Error{ErrorCode::kStreamNotFound, "not implemented (M1)"});
}

std::expected<const uint8_t*, Error> CfbReader::SectorBytes(
    uint32_t /*sector*/) const {
  return std::unexpected(Error{ErrorCode::kIoError, "not implemented (M1)"});
}

std::expected<std::vector<uint8_t>, Error> CfbReader::ReadChain(
    uint32_t /*start*/, uint64_t /*size*/, bool /*mini*/) const {
  return std::unexpected(Error{ErrorCode::kIoError, "not implemented (M1)"});
}

}  // namespace doccraft::cfb
