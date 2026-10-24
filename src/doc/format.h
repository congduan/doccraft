#ifndef DOCCRAFT_DOC_FORMAT_H_
#define DOCCRAFT_DOC_FORMAT_H_
#include <cstdint>
#include <expected>
#include <span>
#include "doc/fib.h"
#include "model/document.h"
namespace doccraft::doc {
std::expected<model::Document, Error> ParseFormatted(
    std::span<const uint8_t> word, std::span<const uint8_t> table,
    const Fib& fib, std::span<const uint8_t> data = {});
}
#endif
