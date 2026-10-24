#include "doc/format/sprm.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace doccraft::doc::detail {
void ReadLists(model::Document& doc,Bytes table,const Fib& fib) {
  if(!fib.lists.size || !Valid(table,fib.lists) || fib.lists.size<2)return;
  Bytes b=table.subspan(fib.lists.offset);
  unsigned count=U16(b,0);
  if(count>1024 || 2+size_t(count)*28>fib.lists.size)return;
  size_t p=2+size_t(count)*28;
  for(unsigned i=0;i<count;i++) {
    Bytes head=b.subspan(2+i*28,28);
    model::List list;list.id=U32(head,0);
    unsigned levels=(head[26]&1)?1:9;
    for(unsigned j=0;j<levels;j++) {
      if(p+28>b.size())return;
      Bytes lv=b.subspan(p);
      size_t cp=lv[24],pp=lv[25],name_at=28+cp+pp;
      if(name_at+2>lv.size())return;
      size_t chars=U16(lv,name_at);
      if(chars>(lv.size()-name_at-2)/2)return;
      model::ListLevel level;
      level.start=U32(lv,0);level.format=lv[4];level.suffix=lv[15];
      int associated=U16(head,8+2*j);
      if(associated<0x0fff)level.style=associated;
      for(size_t k=0;k<chars;k++) {
        uint16_t c=U16(lv,name_at+2+2*k);
        if(c<=8 && level.format!=0x17)level.text+="%"+std::to_string(c+1);
        else Utf8(level.text,c);
      }
      Walk(lv.subspan(28,pp),[&](uint16_t op,Bytes v){
        if(op==0x845E)level.left=S16(v,0);
        if(op==0x8460)level.first=S16(v,0);
      });
      model::Run r;
      Walk(lv.subspan(28+pp,cp),[&](uint16_t op,Bytes v){ApplyRun(r,op,v,doc.fonts);});
      level.font=r.font;
      list.levels.push_back(std::move(level));
      p+=name_at+2+2*chars;
    }
    doc.lists.push_back(std::move(list));
  }
  if(fib.list_overrides.size && Valid(table,fib.list_overrides) && fib.list_overrides.size>=4) {
    Bytes o=table.subspan(fib.list_overrides.offset,fib.list_overrides.size);
    unsigned n=U32(o,0);
    if(n<=1024) {
      size_t pos=4;
      for(unsigned i=0;i<n;i++) {
        if(pos>o.size() || o.size()-pos<16) return;
        int id=U32(o,pos),found=-1;
        for(size_t j=0;j<doc.lists.size();j++)if(doc.lists[j].id==id){found=j;break;}
        doc.list_overrides.push_back(found);
        size_t step=16+4*size_t(o[pos+12]);
        if(step>o.size()-pos) return;
        pos+=step;
      }
    }
  }
}
} // namespace doccraft::doc::detail
