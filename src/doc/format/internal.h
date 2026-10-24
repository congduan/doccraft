#ifndef DOCCRAFT_DOC_FORMAT_INTERNAL_H_
#define DOCCRAFT_DOC_FORMAT_INTERNAL_H_
#include "doc/format.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace doccraft::doc::detail {
using Bytes = std::span<const uint8_t>;
inline uint16_t U16(Bytes b, size_t p) { return uint16_t(b[p]) | uint16_t(b[p+1]) << 8; }
inline uint32_t U32(Bytes b, size_t p) { return uint32_t(U16(b,p)) | uint32_t(U16(b,p+2)) << 16; }
inline int16_t S16(Bytes b, size_t p) { return static_cast<int16_t>(U16(b,p)); }
inline void Utf8(std::string& s, uint32_t c) {
  if(c < 0x80) s += char(c);
  else if(c < 0x800) { s += char(0xc0 | (c>>6)); s += char(0x80 | (c&63)); }
  else if(c < 0x10000) { s += char(0xe0 | (c>>12)); s += char(0x80 | ((c>>6)&63)); s += char(0x80 | (c&63)); }
  else { s += char(0xf0 | (c>>18)); s += char(0x80 | ((c>>12)&63)); s += char(0x80 | ((c>>6)&63)); s += char(0x80 | (c&63)); }
}
struct Char { std::string text; uint32_t cp, fc; bool newline; };
struct FormatRange { uint32_t first, last; std::vector<uint8_t> sprms; int style = -1; };
inline bool Valid(Bytes b, Fib::Range r) { return r.offset <= b.size() && r.size <= b.size()-r.offset; }
std::expected<std::vector<Char>, Error> ReadChars(Bytes word, Bytes table, const Fib& fib,uint32_t limit);
std::expected<std::vector<FormatRange>,Error> ReadFkps(Bytes word, Bytes table, Fib::Range r, bool para);
const FormatRange* Find(const std::vector<FormatRange>& a,uint32_t fc);
std::vector<std::string> ReadFonts(Bytes table,Fib::Range r);
std::vector<model::Style> ReadStyles(Bytes table,Fib::Range r,const std::vector<std::string>& fonts);
void ReadLists(model::Document& doc,Bytes table,const Fib& fib);
model::Run InheritedRun(const model::Document& doc,int style);
std::expected<void,Error> ReadHeaders(model::Document& doc,Bytes word,Bytes table,
                                      const Fib& fib,const std::vector<FormatRange>& chpx,
                                      const std::vector<FormatRange>& papx);
} // namespace doccraft::doc::detail
#endif
