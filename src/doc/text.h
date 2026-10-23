#ifndef DOCCRAFT_DOC_TEXT_H_
#define DOCCRAFT_DOC_TEXT_H_
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "doc/fib.h"
namespace doccraft::doc {
std::expected<std::string, Error> ExtractText(std::span<const uint8_t> word,
                                              std::span<const uint8_t> table,
                                              const Fib& fib);
}
#endif
