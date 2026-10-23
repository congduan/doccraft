#ifndef DOCCRAFT_DOCX_WRITER_H_
#define DOCCRAFT_DOCX_WRITER_H_
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "model/document.h"
namespace doccraft::docx {
struct Error {
  std::string message;
};
uint32_t Crc32(std::string_view bytes);
std::expected<std::vector<uint8_t>, Error> Write(
    const model::Document& document);
}  // namespace doccraft::docx
#endif
