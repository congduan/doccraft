#include "docx/writer.h"
#include <cstdint>
#include <cstdlib>
#include <map>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "miniz.h"
#include "pugixml.hpp"

namespace doccraft::docx {
namespace {
constexpr const char* kW="http://schemas.openxmlformats.org/wordprocessingml/2006/main";
std::string Id(int i) { if(i==0)return "Normal";if(i>0&&i<=9)return "Heading"+std::to_string(i);return "Style"+std::to_string(i); }
void Attr(pugi::xml_node n,const char* name,const std::string& value) { n.append_attribute(name).set_value(value.c_str()); }
void IntAttr(pugi::xml_node n,const char* name,int value) { n.append_attribute(name).set_value(value); }
void One(pugi::xml_node parent,const char* name) {parent.append_child(name);}
void Value(pugi::xml_node parent,const char* name,int value) {IntAttr(parent.append_child(name),"w:val",value);}
void SpecBool(pugi::xml_node parent,const char* name,bool value,bool set) {
  if(set) {auto n=parent.append_child(name);if(!value)Attr(n,"w:val","0");}
}
std::string Color(int rgb) {
  if(rgb<0 || (static_cast<uint32_t>(rgb)>>24)==0xff)return "auto";
  char out[7];std::snprintf(out,sizeof(out),"%02X%02X%02X",rgb&255,(rgb>>8)&255,(rgb>>16)&255);
  return out;
}
const char* BorderType(int type) {
  switch(type) {
    case 0:return "nil";case 1:return "single";case 2:return "thick";
    case 3:return "double";case 5:return "hairline";case 6:return "dotted";
    case 7:return "dashed";case 8:return "dotDash";case 9:return "dotDotDash";
    case 10:return "triple";case 20:return "wave";case 21:return "doubleWave";
    case 22:return "dashSmallGap";case 24:return "threeDEmboss";case 25:return "threeDEngrave";
    default:return "single";
  }
}
void BorderPr(pugi::xml_node parent,const char* name,const model::Border& b) {
  if(!b.set)return;
  auto side=parent.append_child(name);Attr(side,"w:val",BorderType(b.type));
  if(b.type) {
    IntAttr(side,"w:sz",b.width<2?2:b.width);
    Attr(side,"w:color",Color(b.color));IntAttr(side,"w:space",b.space);
    if(b.shadow)Attr(side,"w:shadow","1");
    if(b.frame)Attr(side,"w:frame","1");
  }
}
void ShadePr(pugi::xml_node parent,int back,int fore,int pattern) {
  if(back<0 && fore<0)return;
  static constexpr const char* patterns[]={"clear","solid","pct5","pct10","pct20","pct25","pct30","pct40","pct50","pct60","pct70","pct75","pct80","pct90","lightHorizontal","lightVertical","lightDown","lightUp","lightGrid","lightTrellis","horzStripe","vertStripe","reverseDiagStripe","diagStripe","horzCross","diagCross"};
  auto shd=parent.append_child("w:shd");
  Attr(shd,"w:val",pattern>=0 && size_t(pattern)<std::size(patterns)?patterns[pattern]:"clear");
  Attr(shd,"w:fill",Color(back));Attr(shd,"w:color",Color(fore));
}
void RunPr(pugi::xml_node parent,const model::Run& r) {
  pugi::xml_node n=parent.append_child("w:rPr");
  if(r.style>=0)Attr(n.append_child("w:rStyle"),"w:val",Id(r.style));
  if(!r.font.empty()) {auto f=n.append_child("w:rFonts");Attr(f,"w:ascii",r.font);Attr(f,"w:hAnsi",r.font);}
  if(r.size>0)Value(n,"w:sz",r.size);
  SpecBool(n,"w:b",r.bold,r.bold_set);SpecBool(n,"w:i",r.italic,r.italic_set);
  SpecBool(n,"w:strike",r.strike,r.strike_set);
  SpecBool(n,"w:dstrike",r.double_strike,r.double_strike_set);
  SpecBool(n,"w:vanish",r.hidden,r.hidden_set);
  SpecBool(n,"w:shadow",r.shadow,r.shadow_set);SpecBool(n,"w:emboss",r.emboss,r.emboss_set);
  SpecBool(n,"w:imprint",r.imprint,r.imprint_set);SpecBool(n,"w:outline",r.outline,r.outline_set);
  SpecBool(n,"w:smallCaps",r.small_caps,r.small_caps_set);SpecBool(n,"w:caps",r.caps,r.caps_set);
  if(r.underline_set) {
    const char* kind="single";
    switch(r.underline) {
      case 0:kind="none";break;case 1:kind="single";break;case 2:kind="words";break;
      case 3:kind="double";break;case 4:kind="dotted";break;case 6:kind="thick";break;
      case 7:kind="dash";break;case 9:kind="dotDash";break;case 10:kind="dotDotDash";break;
      case 11:kind="wave";break;case 0x14:kind="dottedHeavy";break;
      case 0x17:kind="dashedHeavy";break;case 0x19:kind="dotDashHeavy";break;
      case 0x1A:kind="dotDotDashHeavy";break;case 0x1B:kind="wavyHeavy";break;
      case 0x27:kind="dashLong";break;case 0x2B:kind="wavyDouble";break;
      case 0x37:kind="dashLongHeavy";break;
    }
    auto u=n.append_child("w:u");Attr(u,"w:val",kind);
  }
  if(r.super_sub_set)Attr(n.append_child("w:vertAlign"),"w:val",r.super_sub==1?"superscript":r.super_sub==2?"subscript":"baseline");
  if(r.spacing_set)IntAttr(n.append_child("w:spacing"),"w:val",r.spacing);
  if(r.scale_set)IntAttr(n.append_child("w:w"),"w:val",r.scale);
  if(r.color>=0) {
    constexpr const char* colors[]={"auto","000000","0000FF","00FFFF","008000","FF00FF","FF0000","FFFF00","FFFFFF","000080","008080","008000","800080","800000","808000","808080"};
    Attr(n.append_child("w:color"),"w:val",r.color_rgb?Color(r.color):r.color<16?colors[r.color]:"auto");
  }
  if(r.highlight>=0) {
    constexpr const char* colors[]={"none","black","blue","cyan","green","magenta","red","yellow","white","darkBlue","darkCyan","darkGreen","darkMagenta","darkRed","darkYellow","darkGray"};
    Attr(n.append_child("w:highlight"),"w:val",r.highlight<16?colors[r.highlight]:"none");
  }
  ShadePr(n,r.shade,r.shade_fore,r.shade_pattern);
  BorderPr(n,"w:bdr",r.border);
}
void ParaPr(pugi::xml_node parent,const model::Paragraph& p,bool style_props=false) {
  auto n=parent.append_child("w:pPr");
  if(!style_props && p.style>=0)Attr(n.append_child("w:pStyle"),"w:val",Id(p.style));
  if(p.align>=0) {
    const char* align[]={"left","center","right","both","distribute"};
    Attr(n.append_child("w:jc"),"w:val",p.align<5?align[p.align]:"left");
  }
  if(p.left_set || p.right_set || p.first_set) {
    auto ind=n.append_child("w:ind");
    if(p.left_set)IntAttr(ind,"w:left",p.left);
    if(p.right_set)IntAttr(ind,"w:right",p.right);
    if(p.first_set && p.first<0)IntAttr(ind,"w:hanging",-p.first);
    else if(p.first_set)IntAttr(ind,"w:firstLine",p.first);
  }
  if(p.before>=0 || p.after>=0 || p.line) {
    auto sp=n.append_child("w:spacing");
    if(p.before>=0)IntAttr(sp,"w:before",p.before);
    if(p.after>=0)IntAttr(sp,"w:after",p.after);
    if(p.line) {
      if(p.line_auto) {IntAttr(sp,"w:line",p.line);Attr(sp,"w:lineRule","auto");}
      else if(p.line<0) {IntAttr(sp,"w:line",-p.line);Attr(sp,"w:lineRule","exact");}
      else {IntAttr(sp,"w:line",p.line);Attr(sp,"w:lineRule","atLeast");}
    }
  }
  SpecBool(n,"w:keepNext",p.keep_next,p.keep_next_set);
  SpecBool(n,"w:keepLines",p.keep_lines,p.keep_lines_set);
  SpecBool(n,"w:pageBreakBefore",p.page_break_before,p.page_break_before_set);
  if(p.outline>=0 && p.outline<=9)Value(n,"w:outlineLvl",p.outline);
  if(!p.tabs.empty()) {
    auto tabs=n.append_child("w:tabs");
    const char* aligns[]={"left","center","right","decimal","bar","num"};
    const char* leaders[]={"none","dot","hyphen","underscore","heavy","middleDot"};
    for(const auto& tab:p.tabs) {
      auto stop=tabs.append_child("w:tab");
      Attr(stop,"w:val",tab.align<6?aligns[tab.align]:"left");
      IntAttr(stop,"w:pos",tab.position);
      if(tab.leader)Attr(stop,"w:leader",tab.leader<6?leaders[tab.leader]:"none");
    }
  }
  bool has_border=false;for(const auto& b:p.borders)has_border|=b.set;
  for(const auto& b:p.extra_borders)has_border|=b.set;
  if(has_border) {
    auto bd=n.append_child("w:pBdr");
    const char* sides[]={"w:top","w:left","w:bottom","w:right"};
    for(size_t i=0;i<4;i++)BorderPr(bd,sides[i],p.borders[i]);
    BorderPr(bd,"w:between",p.extra_borders[0]);
    BorderPr(bd,"w:bar",p.extra_borders[1]);
  }
  ShadePr(n,p.shade,p.shade_fore,p.shade_pattern);
  if(p.list_id>0) {auto num=n.append_child("w:numPr");Value(num,"w:ilvl",p.list_level);Value(num,"w:numId",p.list_id);}
}
void Text(pugi::xml_node run,std::string_view text) {
  std::string chunk;
  auto flush=[&]() {if(chunk.empty())return;auto t=run.append_child("w:t");Attr(t,"xml:space","preserve");t.text().set(chunk.c_str());chunk.clear();};
  for(unsigned char c:text) {
    if(c==7 || c==11) {flush();run.append_child("w:br");}
    else if(c==9) {flush();run.append_child("w:tab");}
    else if(c==0x1e) {flush();run.append_child("w:noBreakHyphen");}
    else if(c==0x1f) {flush();run.append_child("w:softHyphen");}
    else if(c>=32)chunk+=char(c);
  }
  flush();
}
void SectionPr(pugi::xml_node sec,const model::Section& section) {
  if(section.break_specified) {
    const char* types[]={"continuous","nextColumn","nextPage","evenPage","oddPage"};
    Attr(sec.append_child("w:type"),"w:val",section.break_type<5?types[section.break_type]:"nextPage");
  }
  auto size=sec.append_child("w:pgSz");IntAttr(size,"w:w",section.width);IntAttr(size,"w:h",section.height);
  if(section.orientation || section.width>section.height)Attr(size,"w:orient","landscape");
  auto mar=sec.append_child("w:pgMar");
  IntAttr(mar,"w:left",section.left);IntAttr(mar,"w:right",section.right);
  IntAttr(mar,"w:top",section.top);IntAttr(mar,"w:bottom",section.bottom);
  if(section.gutter)IntAttr(mar,"w:gutter",section.gutter);
  if(section.header_distance>=0)IntAttr(mar,"w:header",section.header_distance);
  if(section.footer_distance>=0)IntAttr(mar,"w:footer",section.footer_distance);
  if(section.line_count_by>0) {
    auto line=sec.append_child("w:lnNumType");
    IntAttr(line,"w:countBy",section.line_count_by);
    if(section.line_start)IntAttr(line,"w:start",section.line_start+1);
    if(section.line_distance>=0)IntAttr(line,"w:distance",section.line_distance);
    const char* restart[]={"newPage","newSection","continuous"};
    Attr(line,"w:restart",section.line_restart<3?restart[section.line_restart]:"newPage");
  }
  if(section.footnote_position>=0 || section.footnote_restart>=0 || section.footnote_start>=0 || section.footnote_format>=0) {
    auto note=sec.append_child("w:footnotePr");
    if(section.footnote_position>=0)Attr(note.append_child("w:pos"),"w:val",section.footnote_position==2?"beneathText":"pageBottom");
    if(section.footnote_restart>=0) {
      const char* restart[]={"continuous","eachSect","eachPage"};
      Attr(note.append_child("w:numRestart"),"w:val",section.footnote_restart<3?restart[section.footnote_restart]:"continuous");
    }
    if(section.footnote_start>0)Value(note,"w:numStart",section.footnote_start);
    if(section.footnote_format>=0) {
      const char* format[]={"decimal","upperRoman","lowerRoman","upperLetter","lowerLetter"};
      Attr(note.append_child("w:numFmt"),"w:val",section.footnote_format<5?format[section.footnote_format]:"decimal");
    }
  }
}
std::string Save(const pugi::xml_document& d) {std::ostringstream out;d.save(out,"",pugi::format_raw,pugi::encoding_utf8);return out.str();}
}
uint32_t Crc32(std::string_view bytes) {return mz_crc32(0,reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size());}
std::expected<std::vector<uint8_t>,Error> Write(const model::Document& document) {
  pugi::xml_document doc;
  auto root=doc.append_child("w:document");Attr(root,"xmlns:w",kW);
  Attr(root,"xmlns:r","http://schemas.openxmlformats.org/officeDocument/2006/relationships");
  auto body=root.append_child("w:body");
  std::map<std::string,int> hyperlinks;
  auto paragraph=[&](pugi::xml_node parent,const model::Paragraph& p,const model::Section* break_section=nullptr) {
    auto pn=parent.append_child("w:p");ParaPr(pn,p);
    if(break_section)SectionPr(pn.child("w:pPr").append_child("w:sectPr"),*break_section);
    for(const auto& r:p.runs) {
      pugi::xml_node owner=pn;
      if(!r.hyperlink.empty()) {
        auto [it,inserted]=hyperlinks.try_emplace(r.hyperlink,int(hyperlinks.size())+10);
        (void)inserted;
        owner=pn.append_child("w:hyperlink");Attr(owner,"r:id","rId"+std::to_string(it->second));
      } else if(r.page_field) {
        owner=pn.append_child("w:fldSimple");Attr(owner,"w:instr","PAGE");
      }
      auto rn=owner.append_child("w:r");RunPr(rn,r);Text(rn,r.text);
    }
  };
  auto table=[&](const model::Table& t) {
    auto tn=body.append_child("w:tbl");auto pr=tn.append_child("w:tblPr");
    auto width=pr.append_child("w:tblW");IntAttr(width,"w:w",5000);Attr(width,"w:type","pct");
    Attr(pr.append_child("w:tblLayout"),"w:type","fixed");
    auto margins=pr.append_child("w:tblCellMar");
    for(const char* side:{"w:top","w:left","w:bottom","w:right"}) {
      auto m=margins.append_child(side);IntAttr(m,"w:w",0);Attr(m,"w:type","dxa");
    }
    auto grid=tn.append_child("w:tblGrid");
    for(int c:t.columns)IntAttr(grid.append_child("w:gridCol"),"w:w",c);
    for(const auto& row:t.rows) {
      auto tr=tn.append_child("w:tr");
      for(size_t j=0;j<row.size();j++) {
        auto tc=tr.append_child("w:tc");auto tcp=tc.append_child("w:tcPr");
        if(j<t.columns.size()) {auto cw=tcp.append_child("w:tcW");IntAttr(cw,"w:w",t.columns[j]);Attr(cw,"w:type","dxa");}
        paragraph(tc,row[j]);
      }
    }
  };
  auto break_for=[&](size_t index)->const model::Section* {
    for(const auto& [at,section]:document.section_breaks)if(at==index)return &section;
    return nullptr;
  };
  if(document.blocks.empty())for(size_t i=0;i<document.paragraphs.size();i++)
    paragraph(body,document.paragraphs[i],break_for(i));
  else for(const auto& b:document.blocks) {
    if(b.table && b.index<document.tables.size())table(document.tables[b.index]);
    else if(!b.table && b.index<document.paragraphs.size())paragraph(body,document.paragraphs[b.index],break_for(b.index));
  }
  auto sec=body.append_child("w:sectPr");
  SectionPr(sec,document.section);
  std::vector<std::pair<std::string,std::string>> header_parts;
  for(size_t i=0;i<6;i++)if(!document.section.headers[i].empty()) {
    bool is_header=i==0 || i==1 || i==4;
    std::string part=(is_header?"header":"footer")+std::to_string(i+1)+".xml";
    auto ref=sec.append_child(is_header?"w:headerReference":"w:footerReference");
    Attr(ref,"w:type",i==0 || i==2?"even":i==1 || i==3?"default":"first");
    Attr(ref,"r:id","rId"+std::to_string(i+3));
    pugi::xml_document h;
    auto hr=h.append_child(is_header?"w:hdr":"w:ftr");Attr(hr,"xmlns:w",kW);
    Attr(hr,"xmlns:r","http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    for(const auto& p:document.section.headers[i])paragraph(hr,p);
    header_parts.push_back({part,Save(h)});
  }
  if(document.section.first_page_different)One(sec,"w:titlePg");
  pugi::xml_document styles;
  auto sr=styles.append_child("w:styles");Attr(sr,"xmlns:w",kW);
  auto defaults=sr.append_child("w:docDefaults");
  model::Run default_run;default_run.font="Liberation Serif";default_run.size=24;
  auto run_defaults=defaults.append_child("w:rPrDefault");
  RunPr(run_defaults,default_run);
  Value(run_defaults.child("w:rPr"),"w:kern",2);
  for(size_t i=0;i<document.styles.size();i++) {
    const auto& s=document.styles[i];if(s.type<1 || s.type>4 || s.name.empty())continue;
    const char* types[]={"","paragraph","character","table","numbering"};
    auto sn=sr.append_child("w:style");Attr(sn,"w:type",types[s.type]);Attr(sn,"w:styleId",Id(i));
    if(i==0)Attr(sn,"w:default","1");
    Attr(sn.append_child("w:name"),"w:val",s.name);
    if(s.base>=0 && size_t(s.base)<document.styles.size() && !document.styles[s.base].name.empty())Attr(sn.append_child("w:basedOn"),"w:val",Id(s.base));
    if(s.next>=0 && size_t(s.next)<document.styles.size() && s.next!=int(i) && !document.styles[s.next].name.empty())Attr(sn.append_child("w:next"),"w:val",Id(s.next));
    if(s.type==1 || s.type==3 || s.type==4)ParaPr(sn,s.paragraph,true);
    RunPr(sn,s.run);
  }
  pugi::xml_document numbering;
  auto nr=numbering.append_child("w:numbering");Attr(nr,"xmlns:w",kW);
  for(size_t i=0;i<document.lists.size();i++) {
    const auto& list=document.lists[i];
    auto an=nr.append_child("w:abstractNum");IntAttr(an,"w:abstractNumId",i);
    for(size_t j=0;j<list.levels.size();j++) {
      const auto& l=list.levels[j];auto ln=an.append_child("w:lvl");IntAttr(ln,"w:ilvl",j);
      Value(ln,"w:start",l.start);
      if(l.style>=0)Attr(ln.append_child("w:pStyle"),"w:val",Id(l.style));
      const char* format="decimal";
      if(l.format==0x17)format="bullet";
      else if(l.format==0xff)format="none";
      else if(l.format==1)format="upperRoman";
      else if(l.format==2)format="lowerRoman";
      else if(l.format==3)format="upperLetter";
      else if(l.format==4)format="lowerLetter";
      Attr(ln.append_child("w:numFmt"),"w:val",format);
      Attr(ln.append_child("w:lvlText"),"w:val",l.text);
      if(l.suffix==2)Attr(ln.append_child("w:suff"),"w:val","nothing");
      else if(l.suffix==1)Attr(ln.append_child("w:suff"),"w:val","space");
      auto pp=ln.append_child("w:pPr");
      if(l.left || l.first) {
        auto ind=pp.append_child("w:ind");IntAttr(ind,"w:left",l.left);
        if(l.first<0)IntAttr(ind,"w:hanging",-l.first);
        else IntAttr(ind,"w:firstLine",l.first);
      }
      if(!l.font.empty()) {
        auto rp=ln.append_child("w:rPr");auto rf=rp.append_child("w:rFonts");
        Attr(rf,"w:ascii",l.font);Attr(rf,"w:hAnsi",l.font);
      }
    }
  }
  for(size_t i=0;i<document.list_overrides.size();i++) {
    if(document.list_overrides[i]<0)continue;
    auto num=nr.append_child("w:num");IntAttr(num,"w:numId",i+1);
    Value(num,"w:abstractNumId",document.list_overrides[i]);
  }
  std::string content_types="<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/><Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/><Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>";
  for(const auto& [part,content]:header_parts) {
    (void)content;
    bool header=part.starts_with("header");
    content_types+="<Override PartName=\"/word/"+part+"\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml."+(header?std::string("header"):std::string("footer"))+"+xml\"/>";
  }
  if(document.section.odd_even_different)content_types+="<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>";
  content_types+="</Types>";
  const std::string rels="<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>";
  pugi::xml_document docrels;
  auto relations=docrels.append_child("Relationships");
  Attr(relations,"xmlns","http://schemas.openxmlformats.org/package/2006/relationships");
  auto style_rel=relations.append_child("Relationship");
  Attr(style_rel,"Id","rId1");Attr(style_rel,"Type","http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles");Attr(style_rel,"Target","styles.xml");
  auto number_rel=relations.append_child("Relationship");
  Attr(number_rel,"Id","rId2");Attr(number_rel,"Type","http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering");Attr(number_rel,"Target","numbering.xml");
  for(size_t i=0;i<6;i++)if(!document.section.headers[i].empty()) {
    bool header=i==0 || i==1 || i==4;
    auto rel=relations.append_child("Relationship");
    Attr(rel,"Id","rId"+std::to_string(i+3));
    Attr(rel,"Type",std::string("http://schemas.openxmlformats.org/officeDocument/2006/relationships/")+(header?"header":"footer"));
    Attr(rel,"Target",(header?"header":"footer")+std::to_string(i+1)+".xml");
  }
  if(document.section.odd_even_different) {
    auto rel=relations.append_child("Relationship");Attr(rel,"Id","rId9");
    Attr(rel,"Type","http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings");
    Attr(rel,"Target","settings.xml");
  }
  for(const auto& [url,id]:hyperlinks) {
    auto link=relations.append_child("Relationship");
    Attr(link,"Id","rId"+std::to_string(id));
    Attr(link,"Type","http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink");
    Attr(link,"Target",url);Attr(link,"TargetMode","External");
  }
  mz_zip_archive z={};
  if(!mz_zip_writer_init_heap(&z,0,0))return std::unexpected(Error{"ZIP init failed"});
  auto add=[&](const char* name,const std::string& data){return mz_zip_writer_add_mem(&z,name,data.data(),data.size(),MZ_DEFAULT_LEVEL)!=0;};
  bool ok=add("[Content_Types].xml",content_types)&&add("_rels/.rels",rels)&&add("word/_rels/document.xml.rels",Save(docrels))&&add("word/document.xml",Save(doc))&&add("word/styles.xml",Save(styles))&&add("word/numbering.xml",Save(numbering));
  for(const auto& [part,content]:header_parts)if(ok)ok=add(("word/"+part).c_str(),content);
  if(ok && document.section.odd_even_different)ok=add("word/settings.xml","<?xml version=\"1.0\"?><w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:evenAndOddHeaders/></w:settings>");
  void* ptr=nullptr;size_t len=0;
  if(ok)ok=mz_zip_writer_finalize_heap_archive(&z,&ptr,&len)!=0;
  mz_zip_writer_end(&z);
  if(!ok){if(ptr)mz_free(ptr);return std::unexpected(Error{"ZIP write failed"});}
  auto* bytes=static_cast<uint8_t*>(ptr);std::vector<uint8_t> result(bytes,bytes+len);mz_free(ptr);return result;
}
}
