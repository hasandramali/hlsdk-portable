// Sven Co-op user-message formats, verified against client.dll (see steam-refs).
// Kept independent of VGUI so truncated/untrusted packets can be tested directly.
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

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
