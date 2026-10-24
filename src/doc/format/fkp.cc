#include "doc/format/internal.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include <utility>

namespace doccraft::doc::detail {
uint32_t Cp1252(uint8_t c) {
  constexpr uint16_t map[32] = {0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
  return c>=0x80 && c<=0x9f ? map[c-0x80] : c;
}
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
} // namespace doccraft::doc::detail
