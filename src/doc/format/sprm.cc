#include "doc/format/sprm.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <cstdio>
#include <string>
#include <vector>
#include <utility>

namespace doccraft::doc::detail {
std::string LanguageTag(uint16_t lid) {
  // Common Windows LIDs; retain uncommon IDs as a private BCP 47 tag.
  switch(lid) {
    case 0x0409:return "en-US"; case 0x0809:return "en-GB";
    case 0x0404:return "zh-TW"; case 0x0804:return "zh-CN";
    case 0x0C04:return "zh-HK"; case 0x0411:return "ja-JP";
    case 0x0412:return "ko-KR"; case 0x0407:return "de-DE";
    case 0x040C:return "fr-FR"; case 0x040A:return "es-ES";
    case 0x0410:return "it-IT"; case 0x0419:return "ru-RU";
    case 0x0416:return "pt-BR"; case 0x0816:return "pt-PT";
    case 0x0401:return "ar-SA"; case 0x040D:return "he-IL";
    case 0x0439:return "hi-IN";
  }
  char tag[16];std::snprintf(tag,sizeof(tag),"x-lcid-%04x",lid);
  return tag;
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
    case 0x0800:r.revision_delete=Toggle(r.revision_delete,v[0]);break;
    case 0x0801:r.revision_insert=Toggle(r.revision_insert,v[0]);break;
    case 0xCA89:if(v.size()>1)r.property_revision=v[1]!=0;break;
    case 0x6A03:r.object_offset=static_cast<int>(U32(v,0));break;
    case 0x0806:r.binary_data=v[0]!=0;break;
    case 0x080A:r.ole_object=v[0]!=0;break;
    case 0x0856:r.embedded_object=v[0]!=0;break;
    case 0x0835:flag(r.bold,r.bold_set);break; case 0x0836:flag(r.italic,r.italic_set);break;
    case 0x0837:flag(r.strike,r.strike_set);break; case 0x2A53:flag(r.double_strike,r.double_strike_set);break;
    case 0x083C:flag(r.hidden,r.hidden_set);break;
    case 0x0811:flag(r.web_hidden,r.web_hidden_set);break;
    case 0x0818:flag(r.spec_vanish,r.spec_vanish_set);break;
    case 0x085A:flag(r.rtl,r.rtl_set);break;
    case 0x0868:flag(r.snap_to_grid,r.snap_to_grid_set);break;
    case 0x0875:flag(r.no_proof,r.no_proof_set);break;
    case 0x0882:flag(r.complex_script,r.complex_script_set);break;
    case 0x2859:r.text_effect=v[0];break;
    case 0x286F:r.font_hint=v[0];break;
    case 0x6877:r.underline_color=static_cast<int>(U32(v,0));break;
    case 0x485F:r.lang_cs=LanguageTag(U16(v,0));break;
    case 0x486D:case 0x4873:r.lang=LanguageTag(U16(v,0));break;
    case 0x486E:case 0x4874:r.lang_east_asia=LanguageTag(U16(v,0));break;
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
    case 0x4A4F:case 0x4A50:case 0x4A51:case 0x4A5E: {
      unsigned n=U16(v,0);
      if(n<fonts.size()) {
        if(op==0x4A50)r.font_east_asia=fonts[n];
        else if(op==0x4A51)r.font_hansi=fonts[n];
        else if(op==0x4A5E)r.font_cs=fonts[n];
        else r.font=fonts[n];
      }
      break;
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
    case 0x3019:s.columns_separator=v[0]!=0;break;
    case 0x301A:if(v[0]>3)return false;s.vertical_justify=v[0];break;
    case 0x3228:s.bidi=v[0]!=0;break;
    case 0x322A:s.rtl_gutter=v[0]!=0;break;
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
} // namespace doccraft::doc::detail
