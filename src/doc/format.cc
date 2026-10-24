#include "doc/format.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace doccraft::doc {
namespace {
using Bytes = std::span<const uint8_t>;
uint16_t U16(Bytes b, size_t p) { return uint16_t(b[p]) | uint16_t(b[p+1]) << 8; }
uint32_t U32(Bytes b, size_t p) { return uint32_t(U16(b,p)) | uint32_t(U16(b,p+2)) << 16; }
int16_t S16(Bytes b, size_t p) { return static_cast<int16_t>(U16(b,p)); }
void Utf8(std::string& s, uint32_t c) {
  if(c < 0x80) s += char(c);
  else if(c < 0x800) { s += char(0xc0 | (c>>6)); s += char(0x80 | (c&63)); }
  else if(c < 0x10000) { s += char(0xe0 | (c>>12)); s += char(0x80 | ((c>>6)&63)); s += char(0x80 | (c&63)); }
  else { s += char(0xf0 | (c>>18)); s += char(0x80 | ((c>>12)&63)); s += char(0x80 | ((c>>6)&63)); s += char(0x80 | (c&63)); }
}
uint32_t Cp1252(uint8_t c) {
  constexpr uint16_t map[32] = {0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
  return c>=0x80 && c<=0x9f ? map[c-0x80] : c;
}
struct Char { std::string text; uint32_t cp, fc; bool newline; };
struct FormatRange { uint32_t first, last; std::vector<uint8_t> sprms; int style = -1; };
bool Valid(Bytes b, Fib::Range r) { return r.offset <= b.size() && r.size <= b.size()-r.offset; }
std::expected<std::vector<Char>, Error> ReadChars(Bytes word, Bytes table, const Fib& fib,uint32_t limit) {
  if (fib.complex) return std::unexpected(Error{"fast-saved document unsupported"});
  if (!Valid(table,{fib.fc_clx,fib.lcb_clx})) return std::unexpected(Error{"CLX outside table stream"});
  Bytes b=table.subspan(fib.fc_clx,fib.lcb_clx); size_t p=0;
  while(p<b.size() && b[p]==1) { if(p+3>b.size()) return std::unexpected(Error{"truncated CLX"}); size_t n=U16(b,p+1); if(n>b.size()-p-3) return std::unexpected(Error{"truncated CLX Prc"}); p+=3+n; }
  if(p+5>b.size() || b[p]!=2) return std::unexpected(Error{"missing Pcdt"});
  size_t nbytes=U32(b,p+1);p+=5;
  if(nbytes<16 || nbytes>b.size()-p || (nbytes-4)%12) return std::unexpected(Error{"invalid Pcdt"});
  size_t count=(nbytes-4)/12, desc=p+4*(count+1);
  std::vector<Char> out;
  size_t emitted=0;
  for(size_t i=0;i<count;i++) {
    uint32_t cp=U32(b,p+4*i), end=U32(b,p+4*(i+1));
    if(end<=cp) return std::unexpected(Error{"invalid piece CP order"});
    if(cp>=limit) break;
    end=std::min(end,limit);
    size_t length=end-cp;
    if(length>word.size()-emitted)
      return std::unexpected(Error{"piece table over-covers WordDocument"});
    emitted+=length;
    uint32_t raw=U32(b,desc+8*i+2); bool compressed=raw&0x40000000u;
    uint64_t start=compressed?(raw&0x3fffffffu)/2:raw&0x3fffffffu;
    uint64_t bytes=uint64_t(length)*(compressed?1:2);
    if(start>word.size() || bytes>word.size()-start) return std::unexpected(Error{"piece outside WordDocument"});
    for(uint32_t j=0;j<length;j++) {
      uint32_t character_cp=cp+j;
      uint32_t c=compressed?Cp1252(word[start+j]):U16(word,start+j*2);
      uint32_t fc=start+j*(compressed?1:2);
      if(!compressed && c>=0xd800 && c<=0xdbff && j+1<length) {
        uint32_t low=U16(word,start+(j+1)*2);
        if(low>=0xdc00 && low<=0xdfff) { c=0x10000+((c-0xd800)<<10)+(low-0xdc00);j++; }
      }
      if(c==0xfffe || c==0xffff || (c>=0xd800 && c<=0xdfff)) c=0xfffd;
      Char ch{{},character_cp,fc,c==13};
      Utf8(ch.text,c==13?10:c);
      out.push_back(std::move(ch));
    }
  }
  return out;
}
std::expected<std::vector<FormatRange>,Error> ReadFkps(Bytes word, Bytes table, Fib::Range r, bool para) {
  std::vector<FormatRange> out;
  if(!r.size) return out;
  if(!Valid(table,r) || r.size<12 || (r.size-4)%8) return std::unexpected(Error{"invalid BTE table"});
  Bytes plc=table.subspan(r.offset,r.size);size_t pages=(r.size-4)/8;
  if(pages>word.size()/512) return std::unexpected(Error{"BTE page count exceeds WordDocument"});
  std::vector<uint8_t> seen(word.size()/512);
  for(size_t k=0;k<pages;k++) {
    size_t pn=U32(plc,4*(pages+1)+4*k)&0x3fffff;
    if(pn>=seen.size()) return std::unexpected(Error{"FKP page outside WordDocument"});
    if(seen[pn]++) return std::unexpected(Error{"duplicate FKP page"});
    Bytes f=word.subspan(pn*512,512); size_t n=f[511], stride=para?13:1;
    if(n==0 || n>(para?29:101) || 4*(n+1)+stride*n>511) return std::unexpected(Error{"invalid FKP run count"});
    for(size_t j=0;j<n;j++) {
      uint32_t first=U32(f,4*j),last=U32(f,4*(j+1));
      if(last<=first) return std::unexpected(Error{"invalid FKP FC order"});
      size_t off=2*f[4*(n+1)+stride*j];
      FormatRange x{first,last,{}};
      if(off) {
        if(off>=511) return std::unexpected(Error{"invalid FKP format offset"});
        size_t len=0, start=0;
        if(para) {
          size_t cb=f[off];
          if(cb) { len=cb*2-1; start=off+1; }
          else { if(off+2>511) return std::unexpected(Error{"truncated PAPX"}); len=2*f[off+1];start=off+2; }
          if(len<2 || len>511-start) return std::unexpected(Error{"invalid PAPX size"});
          x.style=U16(f,start);start+=2;len-=2;
        } else { len=f[off];start=off+1;if(len>511-start) return std::unexpected(Error{"invalid CHPX size"}); }
        x.sprms.assign(f.begin()+start,f.begin()+start+len);
      }
      out.push_back(std::move(x));
    }
  }
  std::sort(out.begin(),out.end(),[](const auto& x,const auto& y){return x.first<y.first;});
  for(size_t i=1;i<out.size();i++)
    if(out[i].first<out[i-1].last) return std::unexpected(Error{"overlapping FKP ranges"});
  return out;
}
const FormatRange* Find(const std::vector<FormatRange>& a,uint32_t fc) {
  auto it=std::upper_bound(a.begin(),a.end(),fc,
                           [](uint32_t value,const FormatRange& x){return value<x.first;});
  if(it==a.begin()) return nullptr;
  --it;
  return fc<it->last?&*it:nullptr;
}
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
bool Toggle(bool old,uint8_t v) { return v==0x80?old:v==0x81?!old:v!=0; }
bool ReadBorder(model::Border& b,Bytes v,bool old) {
  size_t start=old?0:1;
  if(v.size()!=(old?4u:9u) || (!old && v[0]!=8))return false;
  b.set=true;b.color=old?-1:static_cast<int>(U32(v,start));
  b.width=v[start+(old?0:4)];b.type=v[start+(old?1:5)];
  uint8_t flags=v[start+(old?3:6)];b.space=flags&31;
  b.shadow=(flags&32)!=0;b.frame=(flags&64)!=0;
  return true;
}
bool ReadShade(int& back,int& fore,int& pattern,Bytes v) {
  if(v.size()!=11 || v[0]!=10)return false;
  fore=static_cast<int>(U32(v,1));back=static_cast<int>(U32(v,5));pattern=U16(v,9);
  return true;
}
bool TableBorder(model::Border& b,Bytes v,bool old) {
  if(v.size()==(old?4u:8u) && U32(v,0)==0xffffffffu &&
     (old || U32(v,4)==0xffffffffu)) {b={};b.set=true;return true;}
  if(old)return ReadBorder(b,v,true);
  if(v.size()!=8)return false;
  b.set=true;b.color=static_cast<int>(U32(v,0));b.width=v[4];b.type=v[5];
  b.space=v[6]&31;b.shadow=(v[6]&32)!=0;b.frame=(v[6]&64)!=0;
  return true;
}
bool TableShade(model::CellProperties& c,Bytes v) {
  if(v.size()!=10)return false;
  c.shade_fore=static_cast<int>(U32(v,0));c.shade=static_cast<int>(U32(v,4));
  c.shade_pattern=U16(v,8);return true;
}
bool TableRange(model::RowProperties& row,Bytes v,size_t offset,size_t& first,size_t& limit) {
  if(v.size()<offset+2)return false;
  first=v[offset];limit=v[offset+1];
  return first<limit && limit<=row.cells.size();
}
bool ApplyTable(model::Table& table,model::RowProperties& row,uint16_t op,Bytes v) {
  auto& cells=row.cells;
  if(op==0xD608 || op==0xD606) {
    if(v.size()<5)return false;
    size_t count=v[2];
    if(count>63 || 3+2*(count+1)>v.size())return false;
    row.cells.assign(count,{});table.columns.clear();
    for(size_t i=0;i<count;i++) {
      int width=S16(v,3+2*(i+1))-S16(v,3+2*i);
      if(width<0)return false;
      table.columns.push_back(width);
    }
    if(op==0xD606)return true; // obsolete BRC10 cells are not TC80 cells
    size_t start=3+2*(count+1);
    if((v.size()-start)%20)return false;
    for(size_t i=0;i<count && start+20<=v.size();i++,start+=20) {
      auto& c=cells[i];uint16_t flags=U16(v,start);
      c.width=U16(v,start+2)?U16(v,start+2):table.columns[i];
      c.width_type=(flags>>9)&7;
      unsigned horizontal=flags&3;
      c.horizontal_merge=horizontal>=2?2:horizontal;
      unsigned vertical=(flags>>5)&3;
      c.vertical_merge=vertical==3?2:vertical;
      unsigned flow=(flags>>2)&7;
      if(flow==0 || flow==1 || flow==3 || flow==4 || flow==5)
        c.text_direction=flow?int(flow):-1;
      for(size_t side=0;side<4;side++) {
        Bytes brc=v.subspan(start+4+side*4,4);
        if(U32(brc,0)!=0xffffffffu && U32(brc,0)!=0)TableBorder(c.borders[side],brc,true);
      }
    }
    return true;
  }
  if(op==0x3403 || op==0x3466) {row.cant_split=v[0]!=0;return true;}
  if(op==0x3404) {row.header=v[0]!=0;return true;}
  if(op==0x9407) {row.height=S16(v,0);return true;}
  if(op==0xF614) {
    if(v.size()!=3 || (v[0]!=0 && v[0]!=2 && v[0]!=3))return false;
    table.width_type=v[0];table.width=U16(v,1);return true;
  }
  if(op==0x3615) {table.auto_fit=v[0]!=0;return true;}
  if(op==0xF661) {
    if(v.size()!=3 || v[0]!=3)return false;
    table.indent=U16(v,1);return true;
  }
  if(op==0x548A) {
    if(U16(v,0)>2)return false;
    table.align=U16(v,0);return true;
  }
  if(op==0x9601) {table.left_origin=S16(v,0);table.indent=table.left_origin-table.gap_half;return true;}
  if(op==0x9602) {
    if(S16(v,0)<0)return false;
    table.gap_half=S16(v,0);
    if(table.left_origin>=0)table.indent=table.left_origin-table.gap_half;
    return true;
  }
  if(op==0xD633) {
    if(v.size()!=7 || v[0]!=6 || v[1]!=0 || v[2]!=1 || v[3]!=15 ||
       (v[4]!=3 && v[4]!=0 && v[4]!=0x13) || S16(v,5)<0 || S16(v,5)>15840)
      return false;
    table.cell_spacing=v[4]==0?0:S16(v,5);
    return true;
  }
  if(op==0x560B || op==0x5664) {table.bidi=U16(v,0)!=0;return true;}
  if(op==0xD660) {
    return ReadShade(table.shade,table.shade_fore,table.shade_pattern,v);
  }
  if(op==0xD605 || op==0xD613) {
    bool old=op==0xD605;size_t size=old?4:8;
    if(v.size()!=1+6*size || v[0]!=6*size)return false;
    for(size_t i=0;i<6;i++) {
      Bytes b=v.subspan(1+i*size,size);
      if(!old || U32(b,0)!=0xffffffffu)
        if(!TableBorder(table.borders[i],b,old))return false;
    }
    return true;
  }
  if(op==0x7621) {
    if(v.size()!=4 || !v[1] || v[0]>cells.size() || cells.size()+v[1]>63)return false;
    cells.insert(cells.begin()+v[0],v[1],model::CellProperties{});
    table.columns.insert(table.columns.begin()+std::min<size_t>(v[0],table.columns.size()),v[1],U16(v,2));
    return true;
  }
  if(op==0x5622) {
    size_t first,limit;if(!TableRange(row,v,0,first,limit) || limit-first==cells.size())return false;
    cells.erase(cells.begin()+first,cells.begin()+limit);
    if(limit<=table.columns.size())table.columns.erase(table.columns.begin()+first,table.columns.begin()+limit);
    return true;
  }
  if(op==0x7623) {
    size_t first,limit;
    if(v.size()!=4 || !TableRange(row,v,0,first,limit))return false;
    for(size_t i=first;i<limit;i++) {
      cells[i].width=U16(v,2);
      if(i<table.columns.size())table.columns[i]=U16(v,2);
    }
    return true;
  }
  if(op==0xD635) {
    size_t first,limit;
    if(v.size()!=6 || v[0]!=5 || !TableRange(row,v,1,first,limit) ||
       (v[3]!=0 && v[3]!=2 && v[3]!=3))return false;
    for(size_t i=first;i<limit;i++) {
      cells[i].width_type=v[3];cells[i].width=U16(v,4);
    }
    return true;
  }
  if(op==0x5624 || op==0x5625) {
    size_t first,limit;if(!TableRange(row,v,0,first,limit))return false;
    for(size_t i=first;i<limit;i++)cells[i].horizontal_merge=op==0x5625?0:i==first?2:1;
    return true;
  }
  if(op==0xD62B) {
    if(v.size()!=3 || v[0]!=2 || v[1]>=cells.size() || (v[2]!=0 && v[2]!=1 && v[2]!=3))return false;
    cells[v[1]].vertical_merge=v[2]==3?2:v[2];return true;
  }
  if(op==0x7629) {
    size_t first,limit;if(!TableRange(row,v,0,first,limit) || v.size()!=4 || U16(v,2)>5 || U16(v,2)==2)return false;
    for(size_t i=first;i<limit;i++)cells[i].text_direction=U16(v,2);
    return true;
  }
  if(op==0xD62C || op==0xD639 || op==0xD642) {
    size_t first,limit;
    if(v.size()!=4 || v[0]!=3 || !TableRange(row,v,1,first,limit))return false;
    if(op==0xD62C && v[3]>2)return false;
    for(size_t i=first;i<limit;i++) {
      if(op==0xD62C)cells[i].vertical_align=v[3];
      else if(op==0xD639)cells[i].no_wrap=v[3]!=0;
      else cells[i].hide_mark=v[3]!=0;
    }
    return true;
  }
  if(op==0xF636) {
    size_t first,limit;
    if(v.size()!=3 || !TableRange(row,v,0,first,limit))return false;
    for(size_t i=first;i<limit;i++)cells[i].fit_text=v[2]!=0;
    return true;
  }
  if(op==0xD632 || op==0xD634) {
    if(v.size()!=7 || v[0]!=6 || (v[3]&~15) || (v[4]!=0 && v[4]!=3))return false;
    if(op==0xD634 && (v[1]!=0 || v[2]!=1))return false;
    size_t first=0,limit=cells.size();
    if(op==0xD632 && !TableRange(row,v,1,first,limit))return false;
    int width=v[4]==3?U16(v,5):0;
    if(width>31680)return false;
    for(size_t i=first;i<limit;i++)for(size_t side=0;side<4;side++)
      if(v[3]&(1<<side))cells[i].margins[side]=width;
    return true;
  }
  if(op==0xD62D || op==0xD62E) {
    size_t first,limit;if(v.size()!=13 || v[0]!=12 || !TableRange(row,v,1,first,limit))return false;
    for(size_t i=first;i<limit;i++)if(op==0xD62D || ((i-first)&1)==0)
      if(!TableShade(cells[i],v.subspan(3,10)))return false;
    return true;
  }
  if(op==0xD609) {
    if(v.empty() || size_t(v[0])+1!=v.size() || v[0]%2)return false;
    // SHD80 has palette colors; modern SHD values later in the grpprl override it.
    return true;
  }
  if(op==0xD612 || op==0xD616 || op==0xD60C || op==0xD670 || op==0xD671 || op==0xD672) {
    size_t first=(op==0xD616 || op==0xD671)?22:(op==0xD60C || op==0xD672)?44:0;
    if(v.empty() || size_t(v[0])+1!=v.size() || v[0]%10)return false;
    for(size_t i=0;i<v[0]/10 && first+i<cells.size();i++)
      if(!TableShade(cells[first+i],v.subspan(1+i*10,10)))return false;
    return true;
  }
  if(op==0xD62F || op==0xD620) {
    size_t first,limit;
    if(v.size()!=(op==0xD62F?12u:8u) || v[0]!=v.size()-1 ||
       !TableRange(row,v,1,first,limit) || (v[3]&~63))return false;
    bool old=op==0xD620;
    for(size_t i=first;i<limit;i++)for(size_t side=0;side<6;side++)
      if(v[3]&(1<<side))if(!TableBorder(cells[i].borders[side],v.subspan(4,old?4:8),old))return false;
    return true;
  }
  return true;
}
void ApplyRun(model::Run& r,uint16_t op,Bytes v,const std::vector<std::string>& fonts) {
  auto flag=[&](bool& x,bool& set){ x=Toggle(x,v[0]);set=true; };
  switch(op) {
    case 0x0835:flag(r.bold,r.bold_set);break; case 0x0836:flag(r.italic,r.italic_set);break;
    case 0x0837:flag(r.strike,r.strike_set);break; case 0x2A53:flag(r.double_strike,r.double_strike_set);break;
    case 0x083C:flag(r.hidden,r.hidden_set);break;
    case 0x0838:flag(r.outline,r.outline_set);break;
    case 0x0839:flag(r.shadow,r.shadow_set);break;
    case 0x083A:flag(r.small_caps,r.small_caps_set);break;
    case 0x083B:flag(r.caps,r.caps_set);break;
    case 0x0854:flag(r.imprint,r.imprint_set);break;
    case 0x0858:flag(r.emboss,r.emboss_set);break;
    case 0x2A3E:r.underline=v[0];r.underline_set=true;break;
    case 0x4A43:r.size=U16(v,0);break;
    case 0x4A61:r.size_cs=U16(v,0);break;
    case 0x085C:flag(r.bold_cs,r.bold_cs_set);break;
    case 0x085D:flag(r.italic_cs,r.italic_cs_set);break;
    case 0x484B:r.kern=S16(v,0);break;
    case 0x4845:r.position=S16(v,0);r.position_set=true;break;
    case 0x2A48:r.super_sub=v[0];r.super_sub_set=true;break;
    case 0x8840:r.spacing=S16(v,0);r.spacing_set=true;break;
    case 0x4852:r.scale=U16(v,0);r.scale_set=true;break;
    case 0x2A42:r.color=v[0];r.color_rgb=false;break;
    case 0x6870:r.color=U32(v,0);r.color_rgb=true;break;
    case 0x2A0C:r.highlight=v[0];break;
    case 0xCA71:ReadShade(r.shade,r.shade_fore,r.shade_pattern,v);break;
    case 0x6865:ReadBorder(r.border,v,true);break;
    case 0xCA72:ReadBorder(r.border,v,false);break;
    case 0x4A30:r.style=U16(v,0);break;
    case 0x4A4F: {
      unsigned n=U16(v,0);if(n<fonts.size()) r.font=fonts[n];break;
    }
    case 0x6A09: {
      unsigned n=U16(v,0);
      if(n<fonts.size()) {r.symbol_font=fonts[n];r.symbol_character=U16(v,2);}
      break;
    }
    default:break;
  }
}
void ApplyPara(model::Paragraph& p,uint16_t op,Bytes v) {
  if(op==0xC615 || op==0xC60D) {
    if(v.size()<3)return;
    size_t x=1,deleted=v[x++],delbytes=(op==0xC615?4:2)*deleted;
    if(delbytes>v.size()-x)return;
    x+=delbytes;if(x>=v.size())return;
    size_t added=v[x++];
    if(added>64 || added*3>v.size()-x)return;
    for(size_t i=0;i<added;i++) {
      int pos=S16(v,x+2*i),tbd=v[x+2*added+i];
      p.tabs.push_back({pos,tbd&7,(tbd>>3)&7});
    }
    return;
  }
  if(op>=0xC64E && op<=0xC653) {
    size_t i=op-0xC64E;
    ReadBorder(i<4?p.borders[i]:p.extra_borders[i-4],v,false);return;
  }
  if((op>=0x6424 && op<=0x6428) || op==0x6629) {
    size_t i=op==0x6629?5:op-0x6424;
    ReadBorder(i<4?p.borders[i]:p.extra_borders[i-4],v,true);return;
  }
  if(op==0xC64D) {ReadShade(p.shade,p.shade_fore,p.shade_pattern,v);return;}
  switch(op) {
    case 0x4600:p.style=U16(v,0);break;
    case 0x2403:case 0x2461:p.align=v[0];break;
    case 0x840F:case 0x845E:p.left=S16(v,0);p.left_set=true;break;
    case 0x840E:case 0x845D:p.right=S16(v,0);p.right_set=true;break;
    case 0x8411:case 0x8460:p.first=S16(v,0);p.first_set=true;break;
    case 0xA413:p.before=U16(v,0);break;
    case 0xA414:p.after=U16(v,0);break;
    case 0x6412:p.line=S16(v,0);p.line_auto=U16(v,2)!=0;break;
    case 0x2405:p.keep_lines=v[0]!=0;p.keep_lines_set=true;break;
    case 0x2406:p.keep_next=v[0]!=0;p.keep_next_set=true;break;
    case 0x2407:p.page_break_before=v[0]!=0;p.page_break_before_set=true;break;
    case 0x2640:p.outline=v[0];break;
    case 0x460B:p.list_id=U16(v,0);break;
    case 0x260A:p.list_level=v[0];break;
    case 0x2423:if(v[0]<=5)p.frame_wrap=v[0];break;
    case 0x261B:p.frame_anchor=v[0];break;
    default:break;
  }
}
bool ApplySection(model::Section& s,uint16_t op,Bytes v) {
  switch(op) {
    case 0x3009:s.break_type=v[0];s.break_specified=true;break;
    case 0x301D:s.orientation=v[0];break;
    case 0x300A:s.first_page_different=v[0]!=0;break;
    case 0x3005:s.columns_even=v[0]!=0;break;
    case 0x500B:if(U16(v,0)>43)return false;s.columns=U16(v,0)+1;break;
    case 0x900C:s.column_space=U16(v,0);break;
    case 0x3011:s.page_restart=v[0]!=0;break;
    case 0x501C:s.page_start=U16(v,0);break;
    case 0x7044:if(U32(v,0)>2147483646u)return false;s.page_start=static_cast<int>(U32(v,0));break;
    case 0x300E:s.page_format=v[0];break;
    case 0x9031:if(U16(v,0)<1 || U16(v,0)>31680)return false;s.grid_line_pitch=U16(v,0);break;
    case 0x5032:if(U16(v,0)>3)return false;s.grid_mode=U16(v,0);break;
    case 0x5033:s.text_flow=U16(v,0);break;
    case 0x3013:s.line_restart=v[0];break;
    case 0x5015:s.line_count_by=U16(v,0);break;
    case 0x9016:s.line_distance=U16(v,0);break;
    case 0x501B:s.line_start=U16(v,0);break;
    case 0x303B:s.footnote_position=v[0];break;
    case 0x303C:s.footnote_restart=v[0];break;
    case 0x503F:s.footnote_start=U16(v,0);break;
    case 0x5040:s.footnote_format=U16(v,0);break;
    case 0xB017:s.header_distance=U16(v,0);break;
    case 0xB018:s.footer_distance=U16(v,0);break;
    case 0xB01F:s.width=U16(v,0);break;case 0xB020:s.height=U16(v,0);break;
    case 0xB021:s.left=U16(v,0);break;case 0xB022:s.right=U16(v,0);break;
    case 0x9023:s.top=std::abs(S16(v,0));break;case 0x9024:s.bottom=std::abs(S16(v,0));break;
    case 0xB025:s.gutter=U16(v,0);break;default:break;
  }
  return true;
}
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
std::expected<void,Error> ReadHeaders(model::Document& doc,Bytes word,Bytes table,
                                      const Fib& fib,const std::vector<FormatRange>& chpx,
                                      const std::vector<FormatRange>& papx) {
  if(!fib.ccp_hdd || !fib.headers.size)return {};
  if(!Valid(table,fib.headers) || fib.headers.size%4 || fib.headers.size<14*4)
    return std::unexpected(Error{"invalid header story PLC"});
  uint64_t base=uint64_t(fib.ccp_text)+fib.ccp_ftn;
  uint64_t end=base+fib.ccp_hdd;
  if(end>UINT32_MAX)return std::unexpected(Error{"header CP range too large"});
  auto chars=ReadChars(word,table,fib,end);
  if(!chars)return std::unexpected(chars.error());
  Bytes plc=table.subspan(fib.headers.offset,fib.headers.size);
  for(size_t i=0;i<6;i++) {
    uint32_t first=U32(plc,4*(6+i)),last=U32(plc,4*(7+i));
    if(last<first || last>fib.ccp_hdd)return std::unexpected(Error{"header CP range outside story"});
    if(first==last)continue;
    model::Paragraph para;
    // The last paragraph mark is a guard and is not part of the story.
    for(const auto& ch:*chars) {
      if(ch.cp<base+first || ch.cp>=base+last-1)continue;
      if(ch.newline) {
        if(const auto* x=Find(papx,ch.fc)) {
          para.style=x->style;
          Walk(x->sprms,[&](uint16_t op,Bytes v){ApplyPara(para,op,v);});
        }
        doc.section.headers[i].push_back(std::move(para));para={};
      } else if(ch.text.size()!=1 || static_cast<unsigned char>(ch.text[0])>=32) {
        model::Run r;
        if(const auto* x=Find(chpx,ch.fc))Walk(x->sprms,[&](uint16_t op,Bytes v){ApplyRun(r,op,v,doc.fonts);});
        r.text=ch.text;para.runs.push_back(std::move(r));
      }
    }
    if(!para.runs.empty())doc.section.headers[i].push_back(std::move(para));
    bool has_text=false;
    for(const auto& paragraph:doc.section.headers[i])
      for(const auto& run:paragraph.runs)if(!run.text.empty())has_text=true;
    if(!has_text)doc.section.headers[i].clear();
  }
  doc.section.first_page_different=doc.section.first_page_different || !doc.section.headers[4].empty() || !doc.section.headers[5].empty();
  doc.section.odd_even_different=!doc.section.headers[0].empty() || !doc.section.headers[2].empty();
  return {};
}
} // namespace
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
