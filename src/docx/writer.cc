#include "docx/writer.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
namespace doccraft::docx {
namespace {
void U16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(v & 255);
  out.push_back(v >> 8);
}
void U32(std::vector<uint8_t>& out, uint32_t v) {
  U16(out, v & 65535);
  U16(out, v >> 16);
}
void Bytes(std::vector<uint8_t>& out, std::string_view s) {
  out.insert(out.end(), s.begin(), s.end());
}
void XmlText(std::string& out, std::string_view s) {
  for (unsigned char c : s) {
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else if (c == 0x07 || c == 0x0B)
      out += "</w:t><w:br/><w:t xml:space=\"preserve\">";
    else if (c == '\t')
      out += "</w:t><w:tab/><w:t xml:space=\"preserve\">";
    else if (c == 0x1E)
      out += "</w:t><w:noBreakHyphen/><w:t xml:space=\"preserve\">";
    else if (c == 0x1F)
      out += "</w:t><w:softHyphen/><w:t xml:space=\"preserve\">";
    else if (c < 0x20 && c != '\n' && c != '\r')
      continue;
    else
      out += char(c);
  }
}
struct Entry {
  std::string name, data;
  uint32_t offset = 0;
};
}  // namespace
uint32_t Crc32(std::string_view bytes) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char c : bytes) {
    crc ^= c;
    for (int i = 0; i < 8; ++i)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}
std::expected<std::vector<uint8_t>, Error> Write(
    const model::Document& document) {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
      "<w:document "
      "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/"
      "main\"><w:body>";
  for (const auto& p : document.paragraphs) {
    xml += "<w:p>";
    for (const auto& r : p.runs) {
      xml +=
          "<w:r><w:rPr><w:rFonts w:ascii=\"Liberation Serif\" "
          "w:hAnsi=\"Liberation Serif\"/><w:sz w:val=\"24\"/></w:rPr><w:t "
          "xml:space=\"preserve\">";
      XmlText(xml, r.text);
      xml += "</w:t></w:r>";
    }
    xml += "</w:p>";
  }
  xml +=
      "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/>"
      "<w:pgMar w:left=\"1134\" w:right=\"1134\" w:top=\"1134\" "
      "w:bottom=\"1134\"/>"
      "</w:sectPr></w:body></w:document>";
  std::vector<Entry> entries = {
      {"[Content_Types].xml",
       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       "<Types "
       "xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
       "<Default Extension=\"rels\" "
       "ContentType=\"application/"
       "vnd.openxmlformats-package.relationships+xml\"/>"
       "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
       "<Override PartName=\"/word/document.xml\" "
       "ContentType=\"application/"
       "vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/"
       ">"
       "</Types>"},
      {"_rels/.rels",
       "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       "<Relationships "
       "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
       "<Relationship Id=\"rId1\" "
       "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/"
       "relationships/officeDocument\" Target=\"word/document.xml\"/>"
       "</Relationships>"},
      {"word/document.xml", std::move(xml)}};
  std::vector<uint8_t> out;
  for (auto& e : entries) {
    if (e.data.size() > std::numeric_limits<uint32_t>::max() ||
        e.name.size() > 65535 || out.size() > UINT32_MAX)
      return std::unexpected(Error{"docx ZIP size limit"});
    e.offset = static_cast<uint32_t>(out.size());
    U32(out, 0x04034B50);
    U16(out, 20);
    U16(out, 0);
    U16(out, 0);
    U16(out, 0);
    U16(out, 0);
    U32(out, Crc32(e.data));
    U32(out, e.data.size());
    U32(out, e.data.size());
    U16(out, e.name.size());
    U16(out, 0);
    Bytes(out, e.name);
    Bytes(out, e.data);
  }
  if (out.size() > UINT32_MAX)
    return std::unexpected(Error{"docx ZIP size limit"});
  const uint32_t central = out.size();
  for (const auto& e : entries) {
    U32(out, 0x02014B50);
    U16(out, 20);
    U16(out, 20);
    U16(out, 0);
    U16(out, 0);
    U16(out, 0);
    U16(out, 0);
    U32(out, Crc32(e.data));
    U32(out, e.data.size());
    U32(out, e.data.size());
    U16(out, e.name.size());
    U16(out, 0);
    U16(out, 0);
    U16(out, 0);
    U16(out, 0);
    U32(out, 0);
    U32(out, e.offset);
    Bytes(out, e.name);
  }
  if (out.size() > UINT32_MAX)
    return std::unexpected(Error{"docx ZIP size limit"});
  const uint32_t central_size = out.size() - central;
  U32(out, 0x06054B50);
  U16(out, 0);
  U16(out, 0);
  U16(out, entries.size());
  U16(out, entries.size());
  U32(out, central_size);
  U32(out, central);
  U16(out, 0);
  return out;
}
}  // namespace doccraft::docx
