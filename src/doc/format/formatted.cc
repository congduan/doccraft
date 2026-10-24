#include "doc/format/sprm.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace doccraft::doc {
using namespace detail;
std::expected<model::Document,Error> ParseFormatted(Bytes word,Bytes table,const Fib& fib,Bytes data) {
  auto chars=ReadChars(word,table,fib,fib.ccp_text);if(!chars)return std::unexpected(chars.error());
  auto chpx=ReadFkps(word,table,fib.chpx_bte,false);if(!chpx)return std::unexpected(chpx.error());
  auto papx=ReadFkps(word,table,fib.papx_bte,true);if(!papx)return std::unexpected(papx.error());
  model::Document doc;doc.fonts=ReadFonts(table,fib.fonts);doc.styles=ReadStyles(table,fib.stsh,doc.fonts);
  ReadLists(doc,table,fib);
  std::vector<std::pair<uint32_t,model::Section>> sections;
  if(fib.sed.size) {
    if(!Valid(table,fib.sed) || fib.sed.size<20 || (fib.sed.size-4)%16)
      return std::unexpected(Error{"invalid SED PLC"});
    Bytes b=table.subspan(fib.sed.offset,fib.sed.size);size_t n=(b.size()-4)/16;
    uint32_t previous=0;
    for(size_t i=0;i<n;i++) {
      uint32_t end=U32(b,4*(i+1));
      if(end<=previous)return std::unexpected(Error{"invalid section CP order"});
      previous=end;
      size_t pos=4*(n+1)+12*i;
      uint32_t fc=U32(b,pos+2);
      model::Section section;
      if(fc!=0xffffffffu) {
        if(fc>word.size() || word.size()-fc<2)return std::unexpected(Error{"SEPX outside WordDocument"});
        size_t len=U16(word,fc);
        if(len>word.size()-fc-2)return std::unexpected(Error{"truncated SEPX"});
        bool valid=true;
        if(!Walk(word.subspan(fc+2,len),[&](uint16_t op,Bytes v){valid&=ApplySection(section,op,v);}) || !valid)
          return std::unexpected(Error{"invalid SEPX sprm"});
      }
      sections.emplace_back(end,std::move(section));
    }
    doc.section=sections.back().second;
  }
  model::Paragraph para;
  model::Table current_table;
  std::vector<model::Paragraph> row;
  bool in_table=false;
  int field_depth=0;
  bool field_instruction=false, page_field=false;
  std::string instruction, hyperlink;
  size_t next_section=0;
  auto add_paragraph=[&](model::Paragraph value) {
    doc.blocks.push_back({false,doc.paragraphs.size()});
    doc.paragraphs.push_back(std::move(value));
  };
  auto add_table=[&]() {
    if(!in_table)return;
    if(!row.empty()){current_table.rows.push_back(std::move(row));current_table.row_properties.emplace_back();row.clear();}
    doc.blocks.push_back({true,doc.tables.size()});
    doc.tables.push_back(std::move(current_table));current_table={};in_table=false;
  };
  for(size_t i=0;i<chars->size();i++) {
    const Char& ch=(*chars)[i];
    if(ch.text=="\x13") {if(field_depth++==0){instruction.clear();hyperlink.clear();page_field=false;field_instruction=true;}continue;}
    if(ch.text=="\x14" && field_depth==1) {
      field_instruction=false;
      auto tag=instruction.find("HYPERLINK");
      if(tag!=std::string::npos) {
        size_t p=tag+9;while(p<instruction.size() && instruction[p]==' ')p++;
        if(p<instruction.size() && instruction[p]=='\"') {
          size_t end=instruction.find('"',p+1);
          if(end!=std::string::npos)hyperlink=instruction.substr(p+1,end-p-1);
        } else {size_t end=instruction.find(' ',p);hyperlink=instruction.substr(p,end-p);}
      }
      page_field=instruction.find("PAGE")!=std::string::npos;
      continue;
    }
    if(ch.text=="\x15" && field_depth>0) {if(--field_depth==0){instruction.clear();hyperlink.clear();page_field=false;field_instruction=false;}continue;}
    if(field_depth>0 && field_instruction) {instruction+=ch.text;continue;}
    const auto* pap=Find(*papx,ch.fc);
    bool table_flag=false,row_end=false;
    if(pap)WalkParagraph(pap->sprms,data,[&](uint16_t op,Bytes v){
      if(op==0x2416)table_flag=v[0]!=0;
      if(op==0x2417)row_end=v[0]!=0;
    });
    if(in_table && !table_flag && !row_end && ch.text!="\x07")add_table();
    if(ch.text=="\x07") {
      if(!in_table)in_table=true;
      if(row_end) {
        model::RowProperties properties;
        bool valid=true;uint16_t invalid_op=0;
        if(pap)valid=WalkParagraph(pap->sprms,data,[&](uint16_t op,Bytes v){
          if(valid && (op==0xD608 || op==0xD606 || (op>>10&7)==5))
            if(!(valid=ApplyTable(current_table,properties,op,v)))invalid_op=op;
        }) && valid;
        if(!valid)return std::unexpected(Error{"invalid TAPX operand " + std::to_string(invalid_op)});
        if(properties.cells.empty())properties.cells.resize(row.size());
        properties.columns=current_table.columns;
        current_table.row_properties.push_back(std::move(properties));
        current_table.rows.push_back(std::move(row));row.clear();para={};
      } else {
        if(pap) {para.style=pap->style;WalkParagraph(pap->sprms,data,[&](uint16_t op,Bytes v){ApplyPara(para,op,v);});}
        row.push_back(std::move(para));para={};
      }
      continue;
    }
    if(ch.newline) {
      if(pap) {
        para.style=pap->style;
        WalkParagraph(pap->sprms,data,[&](uint16_t op,Bytes v){ApplyPara(para,op,v);});
      }
      if(in_table)row.push_back(std::move(para));
      else add_paragraph(std::move(para));
      para={};
      continue;
    }
    if(ch.text.size()==1 && ch.text[0]=='\x0c') {
      if(next_section+1<sections.size() && ch.cp+1==sections[next_section].first) {
        if(pap) {para.style=pap->style;WalkParagraph(pap->sprms,data,[&](uint16_t op,Bytes v){ApplyPara(para,op,v);});}
        add_paragraph(std::move(para));para={};
        doc.section_breaks.emplace_back(doc.paragraphs.size()-1,sections[next_section].second);
        next_section++;
      }
      continue;
    }
    model::Run r;
    r.hyperlink=hyperlink;r.page_field=page_field;
    if(pap)r=InheritedRun(doc,pap->style);
    if(const auto* x=Find(*chpx,ch.fc)) Walk(x->sprms,[&](uint16_t op,Bytes v){ApplyRun(r,op,v,doc.fonts);});
    if(!para.runs.empty()) {
      auto& back=para.runs.back();
      model::Run prior=back;prior.text.clear();
      if(r.symbol_character<0 && prior.hyperlink==r.hyperlink && prior.page_field==r.page_field && prior.font==r.font && prior.size==r.size && prior.bold==r.bold && prior.italic==r.italic && prior.strike==r.strike && prior.double_strike==r.double_strike && prior.hidden==r.hidden && prior.bold_set==r.bold_set && prior.italic_set==r.italic_set && prior.strike_set==r.strike_set && prior.double_strike_set==r.double_strike_set && prior.hidden_set==r.hidden_set && prior.shadow==r.shadow && prior.shadow_set==r.shadow_set && prior.emboss==r.emboss && prior.emboss_set==r.emboss_set && prior.imprint==r.imprint && prior.imprint_set==r.imprint_set && prior.outline==r.outline && prior.outline_set==r.outline_set && prior.small_caps==r.small_caps && prior.small_caps_set==r.small_caps_set && prior.caps==r.caps && prior.caps_set==r.caps_set && prior.underline==r.underline && prior.underline_set==r.underline_set && prior.super_sub==r.super_sub && prior.super_sub_set==r.super_sub_set && prior.spacing==r.spacing && prior.spacing_set==r.spacing_set && prior.scale==r.scale && prior.scale_set==r.scale_set && prior.color==r.color && prior.color_rgb==r.color_rgb && prior.highlight==r.highlight && prior.shade==r.shade && prior.shade_fore==r.shade_fore && prior.shade_pattern==r.shade_pattern && prior.border.set==r.border.set && prior.border.type==r.border.type && prior.border.width==r.border.width && prior.border.space==r.border.space && prior.border.color==r.border.color && prior.border.shadow==r.border.shadow && prior.border.frame==r.border.frame && prior.symbol_character==r.symbol_character && prior.symbol_font==r.symbol_font && prior.style==r.style) {back.text+=ch.text;continue;}
    }
    r.text=ch.text;para.runs.push_back(std::move(r));
  }
  add_table();
  if(!para.runs.empty() || doc.blocks.empty())add_paragraph(std::move(para));
  auto headers=ReadHeaders(doc,word,table,fib,*chpx,*papx);
  if(!headers)return std::unexpected(headers.error());
  return doc;
}
} // namespace doccraft::doc
