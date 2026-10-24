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
std::vector<std::string> ReadFonts(Bytes table,Fib::Range r) {
  std::vector<std::string> out;
  if(!r.size || !Valid(table,r) || r.size<4) return out;
  Bytes b=table.subspan(r.offset,r.size);
  unsigned count=U16(b,0);size_t p=4;
  for(unsigned i=0;i<count && p<b.size();i++) {
    size_t len=b[p],end=p+1+len;
    if(end>b.size() || len<40) break;
    std::string name;
    for(size_t x=p+40;x+1<end;x+=2) {uint16_t c=U16(b,x);if(!c)break;Utf8(name,c);}
    out.push_back(std::move(name));p=end;
  }
  return out;
}
std::vector<model::Style> ReadStyles(Bytes table,Fib::Range r,const std::vector<std::string>& fonts) {
  std::vector<model::Style> out;
  if(!r.size || !Valid(table,r) || r.size<20) return out;
  Bytes b=table.subspan(r.offset,r.size);
  size_t info=U16(b,0);if(info<18 || info+2>b.size()) return out;
  unsigned count=U16(b,2),base_size=U16(b,4);if(count>4096 || (base_size!=10 && base_size!=18))return out;
  size_t p=2+info;out.resize(count);
  for(unsigned i=0;i<count && p+2<=b.size();i++) {
    size_t len=U16(b,p);p+=2;if(len>b.size()-p)break;
    Bytes d=b.subspan(p,len);p+=len+(len&1);
    if(len<base_size+4)continue;
    auto& s=out[i];s.type=U16(d,2)&15;s.base=U16(d,2)>>4;s.next=U16(d,4)>>4;
    if(s.base==0x0fff)s.base=-1;
    size_t x=base_size,chars=U16(d,x);x+=2;
    if(chars>(d.size()-x)/2 || x+2*chars+2>d.size())continue;
    for(size_t j=0;j<chars;j++)Utf8(s.name,U16(d,x+2*j));
    x+=2*chars+2;if(x&1)x++;
    unsigned upx=U16(d,4)&15;
    for(unsigned j=0;j<upx && x+2<=d.size();j++) {
      size_t n=U16(d,x);x+=2;if(n>d.size()-x)break;
      Bytes payload=d.subspan(x,n);
      if(s.type==3 && j==0)s.table_sprms.assign(payload.begin(),payload.end());
      if(((s.type==1 || s.type==4) && j==0) || (s.type==3 && j==1)) {
        if(n>=2)Walk(payload.subspan(2),[&](uint16_t op,Bytes v){ApplyPara(s.paragraph,op,v);});
      }
      if((s.type==1 && j==1) || (s.type==2 && j==0) || (s.type==3 && j==2))
        Walk(payload,[&](uint16_t op,Bytes v){ApplyRun(s.run,op,v,fonts);});
      x+=n;if(x&1)x++;
    }
  }
  auto overlay_run=[](model::Run& dst,const model::Run& src) {
    if(!src.font.empty())dst.font=src.font;
    if(src.size)dst.size=src.size;
    if(src.bold_set){dst.bold=src.bold;dst.bold_set=true;}
    if(src.italic_set){dst.italic=src.italic;dst.italic_set=true;}
    if(src.strike_set){dst.strike=src.strike;dst.strike_set=true;}
    if(src.double_strike_set){dst.double_strike=src.double_strike;dst.double_strike_set=true;}
    if(src.hidden_set){dst.hidden=src.hidden;dst.hidden_set=true;}
    if(src.shadow_set){dst.shadow=src.shadow;dst.shadow_set=true;}
    if(src.emboss_set){dst.emboss=src.emboss;dst.emboss_set=true;}
    if(src.imprint_set){dst.imprint=src.imprint;dst.imprint_set=true;}
    if(src.outline_set){dst.outline=src.outline;dst.outline_set=true;}
    if(src.small_caps_set){dst.small_caps=src.small_caps;dst.small_caps_set=true;}
    if(src.caps_set){dst.caps=src.caps;dst.caps_set=true;}
    if(src.underline_set){dst.underline=src.underline;dst.underline_set=true;}
    if(src.super_sub_set){dst.super_sub=src.super_sub;dst.super_sub_set=true;}
    if(src.spacing_set){dst.spacing=src.spacing;dst.spacing_set=true;}
    if(src.scale_set){dst.scale=src.scale;dst.scale_set=true;}
    if(src.color>=0){dst.color=src.color;dst.color_rgb=src.color_rgb;}
    if(src.highlight>=0)dst.highlight=src.highlight;
    if(src.shade>=0)dst.shade=src.shade;
    if(src.shade_fore>=0)dst.shade_fore=src.shade_fore;
    if(src.shade>=0 || src.shade_fore>=0)dst.shade_pattern=src.shade_pattern;
    if(src.border.set)dst.border=src.border;
  };
  auto overlay_para=[](model::Paragraph& dst,const model::Paragraph& src) {
    if(src.align>=0)dst.align=src.align;
    if(src.left_set){dst.left=src.left;dst.left_set=true;}
    if(src.right_set){dst.right=src.right;dst.right_set=true;}
    if(src.first_set){dst.first=src.first;dst.first_set=true;}
    if(src.before>=0)dst.before=src.before;
    if(src.after>=0)dst.after=src.after;
    if(src.line){dst.line=src.line;dst.line_auto=src.line_auto;}
    if(src.keep_next_set){dst.keep_next=src.keep_next;dst.keep_next_set=true;}
    if(src.keep_lines_set){dst.keep_lines=src.keep_lines;dst.keep_lines_set=true;}
    if(src.page_break_before_set){dst.page_break_before=src.page_break_before;dst.page_break_before_set=true;}
    if(src.outline>=0)dst.outline=src.outline;
    if(src.list_id>=0){dst.list_id=src.list_id;dst.list_level=src.list_level;}
    if(!src.tabs.empty())dst.tabs=src.tabs;
    for(size_t k=0;k<4;k++)if(src.borders[k].set)dst.borders[k]=src.borders[k];
    for(size_t k=0;k<3;k++)if(src.extra_borders[k].set)dst.extra_borders[k]=src.extra_borders[k];
    if(src.shade>=0)dst.shade=src.shade;
    if(src.shade_fore>=0)dst.shade_fore=src.shade_fore;
    if(src.shade>=0 || src.shade_fore>=0)dst.shade_pattern=src.shade_pattern;
  };
  std::vector<uint8_t> state(out.size());
  std::function<void(size_t)> resolve=[&](size_t i) {
    if(state[i]==2)return;
    if(state[i]==1)return;
    state[i]=1;
    int base=out[i].base;
    if(base>=0 && size_t(base)<out.size() && size_t(base)!=i) {
      resolve(base);
      if(state[base]==2) {
        out[i].resolved_run=out[base].resolved_run;
        out[i].resolved_paragraph=out[base].resolved_paragraph;
      }
    }
    overlay_run(out[i].resolved_run,out[i].run);
    overlay_para(out[i].resolved_paragraph,out[i].paragraph);
    state[i]=2;
  };
  for(size_t i=0;i<out.size();i++)resolve(i);
  return out;
}
model::Run InheritedRun(const model::Document& doc,int style) {
  if(style>=0 && size_t(style)<doc.styles.size()) {
    model::Run value=doc.styles[style].resolved_run;
    // Inherited properties are read for ToggleOperand evaluation, but are
    // represented by the paragraph style in OOXML rather than direct rPr.
    value.font.clear();value.size=0;value.underline_set=false;
    value.super_sub_set=false;value.spacing_set=false;value.scale_set=false;
    value.color=-1;value.highlight=-1;value.shade=-1;
    value.shade_fore=-1;value.border={};
    value.bold_set=value.italic_set=value.strike_set=false;
    value.double_strike_set=value.hidden_set=false;
    value.shadow_set=value.emboss_set=value.imprint_set=false;
    value.outline_set=value.small_caps_set=value.caps_set=false;
    return value;
  }
  return {};
}
} // namespace doccraft::doc::detail
