#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "cfb/cfb_reader.h"
#include "doc/fib.h"
#include "doc/format.h"
#include "docx/writer.h"
#include "miniz.h"

namespace {
void U16(std::vector<uint8_t>& b,size_t p,uint16_t n){b[p]=n&255;b[p+1]=n>>8;}
void U32(std::vector<uint8_t>& b,size_t p,uint32_t n){U16(b,p,n);U16(b,p+2,n>>16);}
std::vector<uint8_t> Prl(uint16_t op,std::vector<uint8_t> value) {
  std::vector<uint8_t> out{uint8_t(op),uint8_t(op>>8)};
  out.insert(out.end(),value.begin(),value.end());return out;
}
void Add(std::vector<uint8_t>& target,uint16_t op,std::vector<uint8_t> value) {
  auto p=Prl(op,std::move(value));target.insert(target.end(),p.begin(),p.end());
}
std::vector<uint8_t> Definition() {
  std::vector<uint8_t> v(3+6+40);v[2]=2;
  U16(v,3,0);U16(v,5,2400);U16(v,7,4800);
  U16(v,11,2400);U16(v,31,2400);
  U16(v,0,v.size()-1);return v;
}
std::vector<uint8_t> Definition10() {
  std::vector<uint8_t> v(9);v[2]=2;
  U16(v,3,0);U16(v,5,2400);U16(v,7,4800);U16(v,0,v.size()-1);
  return v;
}
std::expected<doccraft::model::Document,doccraft::doc::Error> SyntheticTable(
    const std::vector<uint8_t>& extra,bool with_definition=true,
    bool indirect=false,bool corrupt_nested=false) {
  std::vector<uint8_t> word(1536),table(64),data;
  for(size_t i=0;i<5;i++)word[1024+i]=uint8_t("A\aB\a\a"[i]);
  table[0]=2;U32(table,1,16);U32(table,5,0);U32(table,9,5);
  U32(table,15,0x40000000u+2048);
  for(size_t i=0;i<4;i++)U32(word,4*i,1024+uint32_t(i==0?0:i==1?2:i==2?4:5));
  for(size_t i=0;i<3;i++) {
    word[16+13*i]=uint8_t((100+100*i)/2);
    std::vector<uint8_t> sprms;
    Add(sprms,0x2416,{1});
    if(i==2) {
      Add(sprms,0x2417,{1});
      if(indirect) {
        std::vector<uint8_t> nested;
        if(with_definition)Add(nested,0xD608,Definition());
        nested.insert(nested.end(),extra.begin(),extra.end());
        data.resize(2+nested.size());
        U16(data,0,nested.size()+(corrupt_nested?1:0));
        std::copy(nested.begin(),nested.end(),data.begin()+2);
        Add(sprms,0x646B,{0,0,0,0});
      } else {
        if(with_definition)Add(sprms,0xD608,Definition());
        sprms.insert(sprms.end(),extra.begin(),extra.end());
      }
    }
    if((sprms.size()&1)==0)Add(sprms,0x0000,{0});
    size_t p=100+100*i;
    word[p]=uint8_t((sprms.size()+3)/2);U16(word,p+1,0);
    std::copy(sprms.begin(),sprms.end(),word.begin()+p+3);
  }
  word[511]=3;
  U32(table,32,1024);U32(table,36,1029);U32(table,40,0);
  doccraft::doc::Fib fib;fib.ccp_text=5;fib.fc_clx=0;fib.lcb_clx=21;fib.papx_bte={32,12};
  return doccraft::doc::ParseFormatted(word,table,fib,data);
}
std::string Xml(const doccraft::model::Document& d) {
  auto zip=doccraft::docx::Write(d);if(!zip)return {};
  mz_zip_archive z={};if(!mz_zip_reader_init_mem(&z,zip->data(),zip->size(),0))return {};
  size_t size=0;void* data=mz_zip_reader_extract_file_to_heap(&z,"word/document.xml",&size,0);
  std::string xml;if(data){xml.assign(static_cast<const char*>(data),size);mz_free(data);}
  mz_zip_reader_end(&z);return xml;
}
std::string FixtureXml(const std::string& name) {
  std::ifstream file(std::string(DOCCRAFT_SOURCE_DIR)+"/tests/fixtures/"+name+".doc",std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
  auto cfb=doccraft::cfb::CfbReader::Open(bytes);if(!cfb)return {};
  auto word=cfb->ReadStream("WordDocument");if(!word)return {};
  auto fib=doccraft::doc::ParseFib(*word);if(!fib)return {};
  auto table=cfb->ReadStream(fib->table_stream());if(!table)return {};
  auto data=cfb->ReadStream("Data");
  auto doc=data?doccraft::doc::ParseFormatted(*word,*table,*fib,*data):
                doccraft::doc::ParseFormatted(*word,*table,*fib);
  return doc?Xml(*doc):std::string{};
}
TEST(M3Batch3, TDefTableCellWidthBorderAndMerge) {
  auto d=SyntheticTable({});ASSERT_TRUE(d.has_value()) << d.error().message;
  ASSERT_EQ(d->tables.size(),1u);
  ASSERT_EQ(d->tables[0].rows.size(),1u);
  EXPECT_EQ(d->tables[0].columns,(std::vector<int>{2400,2400}));
  EXPECT_NE(Xml(*d).find("<w:tcW w:w=\"2400\""),std::string::npos);
  auto def=Definition();U16(def,9,2);U16(def,11,1800);
  def[13]=8;def[14]=3;def[15]=6;
  auto detailed=SyntheticTable(Prl(0xD608,def));
  ASSERT_TRUE(detailed.has_value()) << detailed.error().message;
  auto xml=Xml(*detailed);
  EXPECT_NE(xml.find("<w:tcW w:w=\"1800\""),std::string::npos);
  EXPECT_NE(xml.find("<w:hMerge w:val=\"restart\""),std::string::npos);
  EXPECT_NE(xml.find("<w:top w:val=\"double\""),std::string::npos);
}
TEST(M3Batch3, TableSetBorderShadingPaddingMergeAndDirection) {
  std::vector<uint8_t> sprms;
  Add(sprms,0xD62F,{11,0,2,15,0,0,255,0,8,3,2,0});
  Add(sprms,0xD62D,{12,0,2,0,0,255,0,255,255,0,0,8,0});
  Add(sprms,0xD632,{6,0,2,15,3,120,0});
  Add(sprms,0x5624,{0,2});
  Add(sprms,0xD62B,{2,0,3});
  Add(sprms,0x7629,{0,2,1,0});
  Add(sprms,0x3466,{1});Add(sprms,0x3404,{1});
  auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
  auto xml=Xml(*d);
  for(const auto& fragment:{"<w:tcBorders>","<w:shd w:val=\"pct50\"",
       "<w:tcMar>","<w:hMerge w:val=\"restart\"",
       "<w:vMerge w:val=\"restart\"","<w:textDirection w:val=\"tbRl\"",
       "<w:cantSplit/>","<w:tblHeader/>"})
    EXPECT_NE(xml.find(fragment),std::string::npos) << fragment;
  EXPECT_NE(xml.find("<w:top w:w=\"120\""),std::string::npos);
}
TEST(M3Batch3, TableInsertDeleteSplitAndDefaultShading) {
  std::vector<uint8_t> sprms;
  Add(sprms,0x7621,{1,1,100,0});
  Add(sprms,0x5622,{2,3});
  Add(sprms,0x5624,{0,2});Add(sprms,0x5625,{0,2});
  Add(sprms,0xD612,{10,0,0,255,0,255,255,0,0,8,0});
  auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
  ASSERT_EQ(d->tables[0].row_properties[0].cells.size(),2u);
  EXPECT_EQ(d->tables[0].row_properties[0].cells[0].horizontal_merge,0);
  EXPECT_NE(Xml(*d).find("<w:shd"),std::string::npos);
}
TEST(M3Batch3, LegacyCellBorderAndCantSplit) {
  std::vector<uint8_t> sprms;
  Add(sprms,0xD620,{7,0,2,1,8,3,6,0});
  Add(sprms,0x3403,{1});
  auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
  auto xml=Xml(*d);
  EXPECT_NE(xml.find("<w:top w:val=\"double\""),std::string::npos);
  EXPECT_NE(xml.find("<w:cantSplit/>"),std::string::npos);
  auto nil=SyntheticTable(Prl(0xD62F,{11,0,1,1,255,255,255,255,255,255,255,255}));
  ASSERT_TRUE(nil.has_value()) << nil.error().message;
  EXPECT_NE(Xml(*nil).find("<w:top w:val=\"nil\""),std::string::npos);
}
TEST(M3Batch3, ShadingVariantsAndDefaultPadding) {
  const std::vector<uint8_t> shd={0,0,255,0,255,255,0,0,8,0};
  for(const auto& [op,offset]:std::vector<std::pair<uint16_t,size_t>>{
      {0xD612,0},{0xD616,22},{0xD60C,44},
      {0xD670,0},{0xD671,22},{0xD672,44}}) {
    SCOPED_TRACE(op);
    std::vector<uint8_t> sprms;
    if(offset)Add(sprms,0x7621,{2,uint8_t(offset-1),100,0});
    std::vector<uint8_t> value{10};value.insert(value.end(),shd.begin(),shd.end());
    Add(sprms,op,value);
    auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
    ASSERT_GT(d->tables[0].row_properties[0].cells.size(),offset);
    EXPECT_EQ(d->tables[0].row_properties[0].cells[offset].shade,0x0000ffff);
  }
  std::vector<uint8_t> padding;
  Add(padding,0xD634,{6,0,1,10,3,108,0});
  auto d=SyntheticTable(padding);ASSERT_TRUE(d.has_value()) << d.error().message;
  EXPECT_NE(Xml(*d).find("<w:left w:w=\"108\""),std::string::npos);
}
TEST(M3Batch3, OddCellShadingAndTDefTable10) {
  std::vector<uint8_t> sprms;
  Add(sprms,0xD606,Definition10());
  Add(sprms,0xD62E,{12,0,2,0,0,255,0,255,255,0,0,8,0});
  auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
  const auto& cells=d->tables[0].row_properties[0].cells;
  EXPECT_EQ(d->tables[0].columns,(std::vector<int>{2400,2400}));
  EXPECT_EQ(cells[0].shade,0x0000ffff);
  EXPECT_EQ(cells[1].shade,-1);
}
TEST(M3Batch3, TableWidthColumnWidthRowHeightAndAutoFit) {
  std::vector<uint8_t> sprms;
  Add(sprms,0xF614,{3,0x70,0x17});
  Add(sprms,0x3615,{1});
  Add(sprms,0x9407,{0x68,0x01});
  Add(sprms,0x7623,{0,2,0xC4,0x09});
  Add(sprms,0xD635,{5,0,1,3,0xB8,0x0B});
  auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
  auto xml=Xml(*d);
  EXPECT_NE(xml.find("<w:tblW w:w=\"6000\" w:type=\"dxa\""),std::string::npos);
  EXPECT_NE(xml.find("<w:tblLayout w:type=\"autofit\""),std::string::npos);
  EXPECT_NE(xml.find("<w:trHeight w:val=\"360\" w:hRule=\"atLeast\""),std::string::npos);
  EXPECT_NE(xml.find("<w:gridCol w:w=\"2500\""),std::string::npos);
  EXPECT_NE(xml.find("<w:tcW w:w=\"3000\""),std::string::npos);
}
TEST(M3Batch3, LegacyAndModernTableBorders) {
  for(uint16_t op:{uint16_t(0xD605),uint16_t(0xD613)}) {
    SCOPED_TRACE(op);
    size_t size=op==0xD605?4:8;
    std::vector<uint8_t> value(1+6*size);value[0]=uint8_t(6*size);
    if(size==4) {value[1]=8;value[2]=3;value[3]=6;}
    else {value[1]=0;value[2]=0;value[3]=255;value[4]=0;value[5]=8;value[6]=3;value[7]=0;}
    auto d=SyntheticTable(Prl(op,value));ASSERT_TRUE(d.has_value()) << d.error().message;
    auto xml=Xml(*d);
    EXPECT_NE(xml.find("<w:tblBorders>"),std::string::npos);
    EXPECT_NE(xml.find("<w:top w:val=\"double\""),std::string::npos);
  }
}
TEST(M3Batch3, TableCellVerticalAlignmentNoWrapAndFitText) {
  std::vector<uint8_t> sprms;
  Add(sprms,0xD62C,{3,0,2,1});
  Add(sprms,0xD639,{3,0,2,1});
  Add(sprms,0xD642,{3,0,2,1});
  Add(sprms,0xF636,{0,2,1});
  auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
  auto xml=Xml(*d);
  EXPECT_NE(xml.find("<w:vAlign w:val=\"center\""),std::string::npos);
  EXPECT_NE(xml.find("<w:noWrap/>"),std::string::npos);
  EXPECT_NE(xml.find("<w:tcFitText/>"),std::string::npos);
  EXPECT_NE(xml.find("<w:hideMark/>"),std::string::npos);
}
TEST(M3Batch3, TableShadingIndentAlignmentAndBidi) {
  for(uint16_t bidi:{uint16_t(0x560B),uint16_t(0x5664)}) {
    SCOPED_TRACE(bidi);
    std::vector<uint8_t> sprms;
    Add(sprms,0xD660,{10,0,0,255,0,255,255,0,0,8,0});
    Add(sprms,0xF661,{3,240,0});
    Add(sprms,0x548A,{1,0});
    Add(sprms,bidi,{1,0});
    auto d=SyntheticTable(sprms);ASSERT_TRUE(d.has_value()) << d.error().message;
    auto xml=Xml(*d);
    EXPECT_NE(xml.find("<w:shd w:val=\"pct50\""),std::string::npos);
    EXPECT_NE(xml.find("<w:tblInd w:w=\"240\" w:type=\"dxa\""),std::string::npos);
    EXPECT_NE(xml.find("<w:jc w:val=\"center\""),std::string::npos);
    EXPECT_NE(xml.find("<w:bidiVisual/>"),std::string::npos);
  }
}
TEST(M3Batch3, SectionBreakCarriesPrecedingSectionProperties) {
  doccraft::model::Document d;
  d.paragraphs.resize(2);
  doccraft::model::Run first_run;first_run.text="First";
  doccraft::model::Run second_run;second_run.text="Second";
  d.paragraphs[0].runs.push_back(first_run);d.paragraphs[1].runs.push_back(second_run);
  doccraft::model::Section first;first.width=15840;first.height=12240;
  d.section_breaks.emplace_back(0,first);
  auto xml=Xml(d);
  size_t first_text=xml.find("First"),break_pos=xml.find("<w:sectPr>");
  size_t second_text=xml.find("Second"),last_section=xml.rfind("<w:sectPr>");
  ASSERT_NE(first_text,std::string::npos);ASSERT_NE(break_pos,std::string::npos);
  ASSERT_NE(second_text,std::string::npos);ASSERT_NE(last_section,std::string::npos);
  EXPECT_LT(break_pos,first_text);EXPECT_LT(first_text,second_text);
  EXPECT_LT(second_text,last_section);
  EXPECT_NE(xml.find("w:w=\"15840\"",break_pos),std::string::npos);
  EXPECT_NE(xml.find("w:w=\"12240\"",last_section),std::string::npos);
}
TEST(M3Batch3, ParsesSectionBoundaryIntoPrecedingParagraph) {
  std::vector<uint8_t> word(1536),table(128);
  word[1024]='A';word[1025]=0x0c;word[1026]='B';word[1027]='\r';
  table[0]=2;U32(table,1,16);U32(table,5,0);U32(table,9,4);
  U32(table,15,0x40000000u+2048);
  U32(table,64,0);U32(table,68,2);U32(table,72,4);
  U32(table,64+14,600);U32(table,64+26,620);
  auto width=Prl(0xB01F,{0xE0,0x3D});
  auto height=Prl(0xB020,{0xD0,0x2F});
  U16(word,600,width.size()+height.size());
  std::copy(width.begin(),width.end(),word.begin()+602);
  std::copy(height.begin(),height.end(),word.begin()+602+width.size());
  U16(word,620,0);
  doccraft::doc::Fib fib;fib.ccp_text=4;fib.fc_clx=0;fib.lcb_clx=21;fib.sed={64,36};
  auto d=doccraft::doc::ParseFormatted(word,table,fib);
  ASSERT_TRUE(d.has_value()) << d.error().message;
  ASSERT_EQ(d->section_breaks.size(),1u);
  EXPECT_EQ(d->section_breaks[0].first,0u);
  EXPECT_EQ(d->section_breaks[0].second.width,15840);
  EXPECT_EQ(d->section.width,12240);
  auto xml=Xml(*d);
  EXPECT_NE(xml.find("<w:pgSz w:w=\"15840\" w:h=\"12240\""),std::string::npos);
  EXPECT_NE(xml.find("<w:pgSz w:w=\"12240\" w:h=\"15840\""),std::string::npos);
}
TEST(M3Batch3, CharacterSymbolEmitsSymElement) {
  std::vector<uint8_t> word(1536),table(160);
  word[1024]='X';word[1025]='\r';
  table[0]=2;U32(table,1,16);U32(table,5,0);U32(table,9,2);
  U32(table,15,0x40000000u+2048);
  U32(table,32,1024);U32(table,36,1026);U32(table,40,1);
  U16(table,64,1);table[68]=54;
  const std::string font="Symbol";
  for(size_t i=0;i<font.size();i++)U16(table,68+40+2*i,uint8_t(font[i]));
  U32(word,512,1024);U32(word,516,1026);word[520]=20;word[1023]=1;
  word[552]=6;auto prl=Prl(0x6A09,{0,0,0xA7,0xF0});
  std::copy(prl.begin(),prl.end(),word.begin()+553);
  doccraft::doc::Fib fib;fib.ccp_text=2;fib.fc_clx=0;fib.lcb_clx=21;
  fib.chpx_bte={32,12};fib.fonts={64,64};
  auto d=doccraft::doc::ParseFormatted(word,table,fib);
  ASSERT_TRUE(d.has_value()) << d.error().message;
  EXPECT_NE(Xml(*d).find("<w:sym w:font=\"Symbol\" w:char=\"F0A7\""),std::string::npos);
}
TEST(M3Batch3, TruncatedTableDefinitionFailsClosed) {
  auto d=SyntheticTable(Prl(0xD608,{10,0,2,0,0}),false);
  EXPECT_FALSE(d.has_value());
  auto partial=Definition();partial.pop_back();U16(partial,0,partial.size()-1);
  auto malformed=SyntheticTable(Prl(0xD608,partial),false);
  EXPECT_FALSE(malformed.has_value());
}
TEST(M3Batch3, IndirectTablePropertyLengthChain) {
  auto valid=SyntheticTable(Prl(0xD632,{6,0,2,2,3,120,0}),true,true);
  ASSERT_TRUE(valid.has_value()) << valid.error().message;
  EXPECT_NE(Xml(*valid).find("<w:left w:w=\"120\""),std::string::npos);
  auto malformed=SyntheticTable({},true,true,true);
  EXPECT_FALSE(malformed.has_value());
}
TEST(M3Batch3, RealTableFixturesEmitCellProperties) {
  auto styled=FixtureXml("table-borders-shading");
  EXPECT_NE(styled.find("<w:tcBorders>"),std::string::npos);
  EXPECT_NE(styled.find("w:fill=\"FFF2CC\""),std::string::npos);
  EXPECT_NE(styled.find("<w:tcMar>"),std::string::npos);
  EXPECT_NE(styled.find("<w:tblInd w:w=\"-8\""),std::string::npos);
  auto merged=FixtureXml("table-merge-direction");
  EXPECT_NE(merged.find("<w:vMerge w:val=\"restart\""),std::string::npos);
  EXPECT_NE(merged.find("<w:vMerge w:val=\"continue\""),std::string::npos);
  EXPECT_NE(merged.find("<w:textDirection"),std::string::npos);
  EXPECT_NE(merged.find("<w:tblInd w:w=\"-8\""),std::string::npos);
  EXPECT_NE(FixtureXml("table-shading-basic").find("<w:shd"),std::string::npos);
}
}
