#include "model/document.h"
#include <utility>
namespace doccraft::model {
Document FromText(const std::string& text) {
  Document doc;
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      Paragraph p;
      p.runs.emplace_back();
      p.runs.back().text = text.substr(start, i - start);
      doc.paragraphs.push_back(std::move(p));
      start = i + 1;
    }
  }
  if (start < text.size() || doc.paragraphs.empty())
    {
      Paragraph p;
      p.runs.emplace_back();
      p.runs.back().text = text.substr(start);
      doc.paragraphs.push_back(std::move(p));
    }
  return doc;
}
}  // namespace doccraft::model
