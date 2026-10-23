#include "model/document.h"
namespace doccraft::model {
Document FromText(const std::string& text) {
  Document doc;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      doc.paragraphs.push_back(
          Paragraph{{Run{text.substr(start, i - start), {}}}});
      start = i + 1;
    }
  }
  if (start < text.size() || doc.paragraphs.empty())
    doc.paragraphs.push_back(Paragraph{{Run{text.substr(start), {}}}});
  for (auto& paragraph : doc.paragraphs) {
    for (auto& run : paragraph.runs) {
      for (size_t i = 0; i < run.text.size(); ++i) {
        if (static_cast<unsigned char>(run.text[i]) < 0x20)
          run.special_offsets.push_back(i);
      }
    }
  }
  return doc;
}
}  // namespace doccraft::model
