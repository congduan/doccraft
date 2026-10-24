#ifndef DOCCRAFT_DOC_FORMAT_SPRM_H_
#define DOCCRAFT_DOC_FORMAT_SPRM_H_
#include "doc/format/internal.h"

namespace doccraft::doc::detail {
// Every PRL is parsed by its spra. Unknown modifiers retain their length so
// later modifiers in the same grpprl remain reachable.
template<class F> bool Walk(Bytes b,F fn) {
  for(size_t p=0;p<b.size();) {
    if(b.size()-p==1 && b[p]==0)return true; // PAPX word alignment byte
    if(p+2>b.size()) return false;
    uint16_t op=U16(b,p);p+=2; size_t len=0;
    switch(op>>13) {
      case 0: case 1: len=1;break;
      case 2: case 4: case 5: len=2;break;
      case 3: len=4;break;
      case 7: len=3;break;
      case 6:
        if(p>=b.size()) return false;
        len=1+b[p];
        if(op==0xD608 || op==0xD606) { if(p+2>b.size()) return false;len=1+U16(b,p); }
        break;
    }
    if(len>b.size()-p) return false;
    fn(op,b.subspan(p,len));p+=len;
  }
  return true;
}
template<class F> bool WalkParagraph(Bytes b,Bytes data,F fn,int depth=0) {
  bool valid=true;
  bool walked=Walk(b,[&](uint16_t op,Bytes v){
    if((op==0x6646 || op==0x646B) && depth<4 && v.size()==4) {
      uint32_t pos=U32(v,0);
      if(pos<=data.size() && data.size()-pos>=2) {
        size_t len=U16(data,pos);
        if(len<=data.size()-pos-2)valid&=WalkParagraph(data.subspan(pos+2,len),data,fn,depth+1);
        else valid=false;
      } else valid=false;
    } else fn(op,v);
  });
  return walked && valid;
}
bool ApplyTable(model::Table& table,model::RowProperties& row,uint16_t op,Bytes v);
void ApplyRun(model::Run& r,uint16_t op,Bytes v,const std::vector<std::string>& fonts);
void ApplyPara(model::Paragraph& p,uint16_t op,Bytes v);
bool ApplySection(model::Section& s,uint16_t op,Bytes v);
} // namespace doccraft::doc::detail
#endif
