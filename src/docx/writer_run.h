#ifndef DOCCRAFT_DOCX_WRITER_RUN_H_
#define DOCCRAFT_DOCX_WRITER_RUN_H_
#include "model/document.h"
#include "pugixml.hpp"
namespace doccraft::docx {
void RunPr(pugi::xml_node parent,const model::Run& run);
}
#endif
