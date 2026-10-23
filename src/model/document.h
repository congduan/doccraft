#ifndef DOCCRAFT_MODEL_DOCUMENT_H_
#define DOCCRAFT_MODEL_DOCUMENT_H_
#include <string>
#include <vector>
namespace doccraft::model {
struct Run {
  std::string text;
  std::vector<size_t> special_offsets;
};
struct Paragraph {
  std::vector<Run> runs;
};
struct Document {
  std::vector<Paragraph> paragraphs;
};
Document FromText(const std::string& text);
}  // namespace doccraft::model
#endif
