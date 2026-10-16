// Sven Co-op user-message formats, verified against client.dll (see steam-refs).
// Kept independent of VGUI so truncated/untrusted packets can be tested directly.
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace SvenUI
{
class Reader
{
 const unsigned char *p;
 size_t left;
public:
 bool ok;
 Reader(const void *data, int size): p((const unsigned char *)data), left(data && size > 0 ? size : 0), ok(data && size >= 0) {}
 unsigned byte() { if(!left) { ok=false; return 0; } --left; return *p++; }
 int shortInt() { unsigned a=byte(), b=byte(); return (int16_t)(a | b<<8); }
 int32_t integer() { uint32_t v=0; for(int i=0;i<4;++i) v |= byte()<<(8*i); return (int32_t)v; }
 float real() { int32_t bits=integer(); float v; memcpy(&v,&bits,4); return v; }
 std::string string() { std::string s; while(left) { unsigned c=byte(); if(!c) return s; s += (char)c; } ok=false; return s; }
 bool done() const { return ok && !left; }
};
// Each completed message is a snapshot. A subsequent transfer must not mutate
// the text in an already visible/queued window.
struct Motd
{
 std::string pending,last;
 bool complete,overflow;
 Motd():complete(true),overflow(false) {}
 bool append(const void *data,int size)
 {
  Reader r(data,size); bool final=r.byte()!=0; std::string text=r.string();
  if(!r.done()) return false;
  if(complete) { pending.clear(); overflow=false; }
  complete=final;
  if(pending.size()+text.size()>65536) { pending.clear(); overflow=true; }
  if(!overflow) pending+=text;
  if(complete) { if(overflow) return false; last=pending; }
  return true;
 }
};
struct Item
{
 int32_t id;
 bool activate, drop, persistent;
 float weight;
 std::string name, title, description, icon, extra;
};
struct NumberDisplay
{
 unsigned channel, flags, digits, maxdigits, mode;
 float value, x, y, fadein, fadeout, hold, effect;
 unsigned char colors[2][4];
 std::string sprite;
 int left, top, width, height;
};
inline bool ValidHudDisplay(const NumberDisplay &data)
{
 return data.channel<32 && data.width>=0 && data.height>=0
  && std::isfinite(data.x) && std::isfinite(data.y) && std::isfinite(data.fadein)
  && std::isfinite(data.fadeout) && std::isfinite(data.hold) && std::isfinite(data.effect);
}
inline void ReadHudColors(Reader &reader,NumberDisplay &data)
{
 for(unsigned color=0;color<2;++color)
  for(unsigned component=0;component<4;++component) data.colors[color][component]=reader.byte();
}
inline void ReadHudSprite(Reader &reader,NumberDisplay &data)
{
 data.sprite=reader.string(); data.left=reader.byte(); data.top=reader.byte();
 data.width=reader.shortInt(); data.height=reader.shortInt();
}
inline void ReadHudTiming(Reader &reader,NumberDisplay &data)
{
 data.fadein=reader.real(); data.fadeout=reader.real(); data.hold=reader.real();
 data.effect=reader.real(); data.mode=reader.byte();
}
struct SpriteDisplay
{
 NumberDisplay data;
 unsigned frame, frames;
 float framerate;
};
inline bool ReadSpriteDisplay(const void *buffer,int size,SpriteDisplay &out)
{
 Reader reader(buffer,size); SpriteDisplay next={};
 next.data.channel=reader.byte(); next.data.flags=(uint32_t)reader.integer();
 ReadHudSprite(reader,next.data);
 next.data.x=reader.real(); next.data.y=reader.real(); ReadHudColors(reader,next.data);
 next.frame=reader.byte(); next.frames=reader.byte(); next.framerate=reader.real();
 ReadHudTiming(reader,next.data);
 if(!reader.done() || !ValidHudDisplay(next.data) || !std::isfinite(next.framerate)) return false;
 out=next; return true;
}
struct TimeDisplay
{
 NumberDisplay data;
 float reference;
};
inline bool ReadTimeDisplay(const void *buffer,int size,TimeDisplay &out)
{
 Reader reader(buffer,size); TimeDisplay next={};
 next.data.channel=reader.byte(); next.data.flags=(uint32_t)reader.integer();
 next.reference=reader.real(); next.data.value=reader.real();
 next.data.x=reader.real(); next.data.y=reader.real(); ReadHudColors(reader,next.data);
 ReadHudSprite(reader,next.data); ReadHudTiming(reader,next.data);
 if(!reader.done() || !ValidHudDisplay(next.data) || !std::isfinite(next.reference)
  || !std::isfinite(next.data.value)) return false;
 out=next; return true;
}
inline std::string FormatHudTime(const NumberDisplay &data,float reference,float time)
{
 double value=data.value;
 if(!(data.flags&0x400000)) value+=((double)time-reference)*(data.flags&0x800000?-1:1);
 value=std::max(0.0,std::min(2147483647.0,value));
 unsigned flags=data.flags;
 if((flags&0xa0000)==0xa0000) flags|=0x40000;
 if((flags&0x140000)==0x140000) flags|=0x80000;
 const unsigned units[]={0x20000,0x40000,0x80000,0x100000};
 const int values[]={(int)(value/3600),(int)(value/60)%60,(int)value%60,(int)std::fmod(value*1000,1000)};
 std::string text;
 for(unsigned index=0;index<4;++index) {
  if(!(flags&units[index])) continue;
  if(!text.empty()) text+=index==3?'.':':';
  std::string digits=std::to_string(values[index]);
  text+=std::string(std::max(0,(index==3?3:2)-(int)digits.size()),'0')+digits;
 }
 return text;
}
inline bool ReadNumberDisplay(const void *data,int size,NumberDisplay &out)
{
 Reader reader(data,size); NumberDisplay next;
 next.channel=reader.byte(); next.flags=(uint32_t)reader.integer(); next.value=reader.real();
 next.digits=reader.byte(); next.maxdigits=reader.byte(); next.x=reader.real(); next.y=reader.real();
 for(unsigned color=0;color<2;++color)
  for(unsigned component=0;component<4;++component) next.colors[color][component]=reader.byte();
 next.sprite=reader.string(); next.left=reader.byte(); next.top=reader.byte();
 next.width=reader.shortInt(); next.height=reader.shortInt();
 next.fadein=reader.real(); next.fadeout=reader.real(); next.hold=reader.real(); next.effect=reader.real(); next.mode=reader.byte();
 if(!reader.done() || next.channel>=32 || next.maxdigits>32 || next.digits>32
   || next.width<0 || next.height<0 || !std::isfinite(next.value)
   || next.value<-2147483648.0 || next.value>2147483647.0
   || !std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.fadein)
   || !std::isfinite(next.fadeout) || !std::isfinite(next.hold) || !std::isfinite(next.effect)) return false;
 out=next; return true;
}
inline int NumberPosition(float value,int screen,int extent,bool pixels,bool center)
{
 double position=center?(screen-extent)*0.5:0;
 if(pixels) position+=value<0&&!center?screen-extent+value:value;
 else position+=value<0&&!center?(value+1)*screen-extent:value*screen;
 return (int)std::max(0.0,std::min((double)std::max(0,screen-extent),position));
}
inline bool ReadItem(const void *data,int size,Item &out)
{
 Reader r(data,size); Item i;
 i.id=r.integer(); i.activate=r.byte()!=0; i.drop=r.byte()!=0; i.persistent=r.byte()!=0;
 i.weight=r.real(); i.name=r.string(); i.title=r.string(); i.description=r.string(); i.icon=r.string(); i.extra=r.string();
 if(!r.done()) return false;
 out=i; return true;
}
inline void RemoveItems(std::vector<Item> &items,int32_t id,bool keepPersistent)
{
 for(size_t i=0;i<items.size();) {
  if((!id || items[i].id==id) && !(keepPersistent && items[i].persistent)) items.erase(items.begin()+i);
  else ++i;
 }
}
inline std::string InventoryHudIcon(const std::vector<Item> &items)
{
 if(items.empty()) return std::string();
 const std::string &icon=items.front().extra;
 for(size_t index=1;index<items.size();++index)
  if(items[index].extra!=icon) return std::string();
 return icon;
}
struct Vote { unsigned duration; std::string title, yes, no; };
inline bool ReadVote(const void *data,int size,Vote &out)
{
 Reader r(data,size); Vote v; v.duration=r.byte(); v.title=r.string(); v.yes=r.string(); v.no=r.string();
 if(!r.done()) return false;
 out=v; return true;
}
inline bool ReadMaps(const void *data,int size,std::vector<std::string> &maps)
{
 Reader r(data,size); unsigned mode=r.byte();
 if(mode==123) return r.done();
 int first=0,end;
 if(mode) { first=r.shortInt(); end=r.shortInt(); } else end=r.shortInt();
 if(!r.ok || first<0 || end<first || end>4096 || (mode && end>(int)maps.size())) return false;
 std::vector<std::string> next=mode ? maps : std::vector<std::string>(end);
 // Sven sends at most five entries in the initial packet; its SHORT
 // is the total allocation size, not the number of strings in this chunk.
 int chunkEnd=mode ? end : std::min(end,5);
 for(int i=first;i<chunkEnd;++i) next[i]=r.string();
 if(!r.done()) return false;
 maps.swap(next); return true;
}
inline bool ReadMapCycle(const void *data,int size,std::vector<std::string> &maps)
{
 if(!data || size<1 || size>65536) return false;
 const char *text=(const char *)data;
 if(text[size-1] || memchr(text,0,size-1)) return false;
 std::vector<std::string> next;
 int depth=0;
 const char *end=text+size-1;
 if(!strcmp(text,"mapcycle failure")) { maps.clear(); return true; }
 while(text<end) {
  const char *lineEnd=text;
  while(lineEnd<end && *lineEnd!='\r' && *lineEnd!='\n') ++lineEnd;
  while(text<lineEnd && (*text==' ' || *text=='\t')) ++text;
  const char *contentEnd=text;
  while(contentEnd<lineEnd && !(contentEnd+1<lineEnd && contentEnd[0]=='/' && contentEnd[1]=='/')) ++contentEnd;
  const char *nameEnd=text;
  while(nameEnd<contentEnd && *nameEnd!=' ' && *nameEnd!='\t' && *nameEnd!='{') ++nameEnd;
  if(!depth && text<nameEnd && *text!='#') {
   std::string name(text,nameEnd);
   if(name.size()>=2 && name[0]=='"' && name[name.size()-1]=='"') name=name.substr(1,name.size()-2);
   bool valid=!name.empty() && name.size()<=255;
   for(size_t index=0;index<name.size();++index) {
    unsigned char character=name[index];
    if(!((character>='a' && character<='z') || (character>='A' && character<='Z')
      || (character>='0' && character<='9') || character=='_' || character=='-' || character=='/')) valid=false;
   }
   if(valid && std::find(next.begin(),next.end(),name)==next.end()) next.push_back(name);
   if(next.size()>4096) return false;
  }
  for(const char *cursor=text;cursor<contentEnd;++cursor) {
   if(*cursor=='{') ++depth;
   else if(*cursor=='}' && depth) --depth;
  }
  text=lineEnd;
  while(text<end && (*text=='\r' || *text=='\n')) ++text;
 }
 maps.swap(next); return true;
}
// Never interpolate server/player text into the console command language.
inline bool SafeArgument(const std::string &s)
{
 if(s.empty() || s.size()>255) return false;
 for(size_t i=0;i<s.size();++i) if((unsigned char)s[i]<32 || s[i]=='"' || s[i]==';' || s[i]=='\\') return false;
 return true;
}
struct Camera
{
 bool active, exiting, buttons[3], keys[103];
 float x,y,nextMotion;
 Camera(): active(false),exiting(false),x(.5f),y(.5f),nextMotion(0) { memset(buttons,0,sizeof(buttons)); memset(keys,0,sizeof(keys)); }
 void move(float dx,float dy) { x=std::max(0.f,std::min(1.f,x+dx)); y=std::max(0.f,std::min(1.f,y+dy)); }
 void screen(float *v) const { v[0]=2*x-1; v[1]=1-2*y; v[2]=0; }
};
}
