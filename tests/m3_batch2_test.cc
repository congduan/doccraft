#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "doc/format.h"
#include "doc/format/sprm.h"
#include "docx/writer.h"
#include "miniz.h"

namespace {
void Put16(std::vector<uint8_t>& b,size_t p,uint16_t n) {b[p]=n&255;b[p+1]=n>>8;}
void Put32(std::vector<uint8_t>& b,size_t p,uint32_t n) {Put16(b,p,n&65535);Put16(b,p+2,n>>16);}
std::vector<uint8_t> Prl(uint16_t op,std::vector<uint8_t> value) {
  std::vector<uint8_t> result{uint8_t(op),uint8_t(op>>8)};
  result.insert(result.end(),value.begin(),value.end());return result;
}
std::vector<uint8_t> Brc(bool modern) {
  return modern?std::vector<uint8_t>{8,0,0,255,0,8,3,2,0}:
      std::vector<uint8_t>{8,3,6,2};
}
std::vector<uint8_t> Shd() {return {10,0,0,255,0,255,255,0,0,8,0};}
std::expected<doccraft::model::Document,doccraft::doc::Error> Synthetic(
    std::vector<uint8_t> pap,std::vector<uint8_t> chp) {
  std::vector<uint8_t> word(1536),table(64);
  word[1024]='X';word[1025]='\r';
  table[0]=2;Put32(table,1,16);Put32(table,5,0);Put32(table,9,2);
  Put32(table,15,0x40000000u+2048);
  auto fkp=[&](size_t base,size_t page,std::vector<uint8_t> sprms,bool paragraph) {
    Put32(word,base,1024);Put32(word,base+4,1026);
    word[base+8]=20;word[base+511]=1;
    if(paragraph) {
      if((sprms.size()&1)==0) {auto nop=Prl(0x0000,{0});sprms.insert(sprms.end(),nop.begin(),nop.end());}
      word[base+40]=uint8_t((sprms.size()+3)/2);
      Put16(word,base+41,0);
      std::copy(sprms.begin(),sprms.end(),word.begin()+base+43);
    } else {
      word[base+40]=uint8_t(sprms.size());
      std::copy(sprms.begin(),sprms.end(),word.begin()+base+41);
    }
    Put32(table,page,1024);Put32(table,page+4,1026);
    Put32(table,page+8,uint32_t(base/512));
  };
  fkp(0,32,pap,true);fkp(512,48,chp,false);
  doccraft::doc::Fib fib;fib.ccp_text=2;fib.fc_clx=0;fib.lcb_clx=21;
  fib.papx_bte={32,12};fib.chpx_bte={48,12};
  return doccraft::doc::ParseFormatted(word,table,fib);
}
std::string Xml(const doccraft::model::Document& d) {
  auto zip=doccraft::docx::Write(d);
  if(!zip)return {};
  mz_zip_archive z={};
  if(!mz_zip_reader_init_mem(&z,zip->data(),zip->size(),0))return {};
  size_t size=0;void* p=mz_zip_reader_extract_file_to_heap(&z,"word/document.xml",&size,0);
  std::string xml;if(p){xml.assign(static_cast<const char*>(p),size);mz_free(p);}
  mz_zip_reader_end(&z);return xml;
}
TEST(M3Batch2, ParagraphBorderOpcodes) {
  const std::vector<std::pair<uint16_t,std::string>> cases={
    {0x6424,"w:top"},{0x6425,"w:left"},{0x6426,"w:bottom"},
    {0x6427,"w:right"},{0x6428,"w:between"},{0x6629,"w:bar"},
    {0xC64E,"w:top"},{0xC64F,"w:left"},{0xC650,"w:bottom"},
    {0xC651,"w:right"},{0xC652,"w:between"},{0xC653,"w:bar"}};
  for(const auto& [op,tag]:cases) {
    SCOPED_TRACE(op);
    auto d=Synthetic(Prl(op,Brc(op>=0xC64E)),{});
    ASSERT_TRUE(d.has_value()) << d.error().message;
    auto xml=Xml(*d);EXPECT_NE(xml.find("<"+tag+" w:val=\"double\""),std::string::npos);
  }
}
TEST(M3Batch2, ParagraphShadingPatternAndColors) {
  auto d=Synthetic(Prl(0xC64D,Shd()),{});
  ASSERT_TRUE(d.has_value());
  auto xml=Xml(*d);
  EXPECT_NE(xml.find("w:val=\"pct50\""),std::string::npos);
  EXPECT_NE(xml.find("w:fill=\"FFFF00\""),std::string::npos);
  EXPECT_NE(xml.find("w:color=\"0000FF\""),std::string::npos);
}
TEST(M3Batch2, CharacterBorderAndEffects) {
  for(uint16_t op:{uint16_t(0x6865),uint16_t(0xCA72)}) {
    SCOPED_TRACE(op);auto d=Synthetic({},Prl(op,Brc(op==0xCA72)));
    ASSERT_TRUE(d.has_value());EXPECT_NE(Xml(*d).find("<w:bdr w:val=\"double\""),std::string::npos);
  }
  for(const auto& [op,tag]:std::vector<std::pair<uint16_t,std::string>>{
       {0x0838,"outline"},{0x0839,"shadow"},{0x083A,"smallCaps"},
       {0x083B,"caps"},{0x0854,"imprint"},{0x0858,"emboss"}}) {
    SCOPED_TRACE(op);auto d=Synthetic({},Prl(op,{1}));
    ASSERT_TRUE(d.has_value());EXPECT_NE(Xml(*d).find("<w:"+tag+"/"),std::string::npos);
  }
}
TEST(M3Batch2, CharacterShadingPatternAndColors) {
  auto d=Synthetic({},Prl(0xCA71,Shd()));ASSERT_TRUE(d.has_value());
  auto xml=Xml(*d);EXPECT_NE(xml.find("w:val=\"pct50\""),std::string::npos);
  EXPECT_NE(xml.find("w:fill=\"FFFF00\""),std::string::npos);
}
TEST(M3Batch2, UnderlineStyles) {
  for(const auto& [code,name]:std::vector<std::pair<uint8_t,std::string>>{
      {1,"single"},{3,"double"},{4,"dotted"},{10,"dotDotDash"},{11,"wave"}}) {
    auto d=Synthetic({},Prl(0x2A3E,{code}));ASSERT_TRUE(d.has_value());
    EXPECT_NE(Xml(*d).find("<w:u w:val=\""+name+"\""),std::string::npos);
  }
}
TEST(M3Batch2, TabStopAlignmentAndLeader) {
  for(uint16_t op:{uint16_t(0xC615),uint16_t(0xC60D)}) {
    SCOPED_TRACE(op);
    auto d=Synthetic(Prl(op,{5,0,1,0xD0,0x02,0x22}),{});
    ASSERT_TRUE(d.has_value()) << d.error().message;
    auto xml=Xml(*d);
    EXPECT_NE(xml.find("<w:tab w:val=\"right\" w:pos=\"720\" w:leader=\"heavy\""),std::string::npos);
  }
}
TEST(M3Batch2, FloatingParagraphWrapAndAnchors) {
  std::vector<uint8_t> pap;
  auto wrap=Prl(0x2423,{2});pap.insert(pap.end(),wrap.begin(),wrap.end());
  auto anchor=Prl(0x261B,{0x90});pap.insert(pap.end(),anchor.begin(),anchor.end());
  auto d=Synthetic(pap,{});ASSERT_TRUE(d.has_value()) << d.error().message;
  auto xml=Xml(*d);
  EXPECT_NE(xml.find("<w:framePr w:wrap=\"around\" w:hAnchor=\"page\" w:vAnchor=\"page\""),std::string::npos);
}
TEST(M3Batch5, CharacterLanguageAndScriptOpcodes) {
  using doccraft::doc::detail::ApplyRun;
  using doccraft::doc::detail::Bytes;
  struct Case {uint16_t op;std::vector<uint8_t> value;std::string expected;};
  const std::vector<Case> cases={
    {0x486D,{0x09,0x04},"w:val=\"en-US\""},
    {0x4873,{0x07,0x04},"w:val=\"de-DE\""},
    {0x486E,{0x04,0x08},"w:eastAsia=\"zh-CN\""},
    {0x4874,{0x11,0x04},"w:eastAsia=\"ja-JP\""},
    {0x485F,{0x01,0x04},"w:bidi=\"ar-SA\""},
    {0x0811,{1},"<w:webHidden/"},
    {0x0818,{1},"<w:specVanish/"},
    {0x085A,{1},"<w:rtl/"},
    {0x0868,{1},"<w:snapToGrid/"},
    {0x0875,{1},"<w:noProof/"},
    {0x0882,{1},"<w:cs/"},
    {0x2859,{4},"w:val=\"antsBlack\""},
    {0x286F,{1},"w:hint=\"eastAsia\""},
    {0x4A50,{0,0},"w:eastAsia=\"Fixture Font\""},
    {0x4A51,{0,0},"w:hAnsi=\"Fixture Font\""},
    {0x4A5E,{0,0},"w:cs=\"Fixture Font\""},
  };
  for(const auto& c:cases) {
    SCOPED_TRACE(c.op);
    doccraft::model::Run run;run.text="X";
    ApplyRun(run,c.op,Bytes(c.value),{"Fixture Font"});
    doccraft::model::Document d;doccraft::model::Paragraph p;p.runs.push_back(run);d.paragraphs.push_back(p);
    EXPECT_NE(Xml(d).find(c.expected),std::string::npos);
  }
  doccraft::model::Run run;run.text="X";run.underline=1;run.underline_set=true;
  const std::vector<uint8_t> color={255,0,0,0};
  ApplyRun(run,0x6877,Bytes(color),{});
  doccraft::model::Document d;doccraft::model::Paragraph p;p.runs.push_back(run);d.paragraphs.push_back(p);
  EXPECT_NE(Xml(d).find("w:color=\"FF0000\""),std::string::npos);
}
TEST(M3Batch5, ObjectAndRevisionSkeletons) {
  using doccraft::doc::detail::ApplyRun;
  using doccraft::doc::detail::Bytes;
  doccraft::model::Run r;r.text="X";
  const std::vector<uint8_t> offset={0x34,0x12,0,0};
  const std::vector<uint8_t> yes={1};
  const std::vector<uint8_t> mark={1,1};
  ApplyRun(r,0x6A03,Bytes(offset),{});
  ApplyRun(r,0x0806,Bytes(yes),{});
  ApplyRun(r,0x080A,Bytes(yes),{});
  ApplyRun(r,0x0856,Bytes(yes),{});
  ApplyRun(r,0x0800,Bytes(yes),{});
  ApplyRun(r,0x0801,Bytes(yes),{});
  ApplyRun(r,0xCA89,Bytes(mark),{});
  EXPECT_EQ(r.object_offset,0x1234);
  EXPECT_TRUE(r.binary_data);
  EXPECT_TRUE(r.ole_object);
  EXPECT_TRUE(r.embedded_object);
  EXPECT_TRUE(r.revision_insert);
  EXPECT_TRUE(r.revision_delete);
  EXPECT_TRUE(r.property_revision);
  doccraft::model::Document d;doccraft::model::Paragraph p;p.runs.push_back(r);d.paragraphs.push_back(p);
  EXPECT_NE(Xml(d).find("<w:rPrChange w:id=\"0\""),std::string::npos);
}
}
