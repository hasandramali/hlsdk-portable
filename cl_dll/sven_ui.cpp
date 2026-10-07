// Sven Co-op menu and interactive camera protocol. No server message IDs are hardcoded.
#include "hud.h"
#include "cl_util.h"
#include "ammohistory.h"
#include "sven_ui.h"
#include "sven_ui_protocol.h"
#include "triangleapi.h"
#include "com_model.h"
#include "event_api.h"
#include "pm_defs.h"
#include "camera.h"
#include <map>
#include <stdlib.h>
#include "keydefs.h"
#include <stdio.h>

static SvenUI::Camera camera;
static std::vector<SvenUI::Item> items;
static std::vector<std::string> maps;
static SvenUI::Vote vote;
static float voteEnd;
static float lastExitRequest=-10;
static unsigned revision;
static int cameraSprite;
static bool keyboard;
static bool uiCaptured;
static int dragFinger=-1;
static int fingers[4] = {-1,-1,-1,-1};

static std::vector<std::string> tracks;
static int trackIndex;
static bool mediaPaused;
static std::string playlistName;
static void LoadPlaylist(const char *name)
{
 tracks.clear(); trackIndex=0; mediaPaused=false;
 std::string path=name && *name ? name : "playlist.txt";
 if(!SvenUI::SafeArgument(path) || path.find("..")!=std::string::npos || path[0]=='/' || path.find(':')!=std::string::npos) return;
 char *file=(char *)gEngfuncs.COM_LoadFile(path.c_str(),5,NULL);
 if(!file && path=="playlist.txt") { path="playlist.m3u"; file=(char *)gEngfuncs.COM_LoadFile(path.c_str(),5,NULL); }
 playlistName=path;
 if(!file) { ++revision; return; }
 std::string text=file; gEngfuncs.COM_FreeFile(file);
 for(size_t pos=0;pos<text.size() && tracks.size()<4096;) {
  size_t end=text.find_first_of("\r\n",pos); if(end==std::string::npos) end=text.size();
  std::string line=text.substr(pos,end-pos); pos=end+1;
  size_t first=line.find_first_not_of(" \t"); if(first==std::string::npos) continue; line.erase(0,first);
  size_t last=line.find_last_not_of(" \t"); line.erase(last+1);
  if(line.empty() || line[0]=='#' || line[0]=='[') continue;
  if(line.compare(0,4,"File")==0 && line.find('=')!=std::string::npos) line.erase(0,line.find('=')+1);
  else if(line.find('=')!=std::string::npos) continue;
  if(line.size()>1 && line[0]=='"' && line[line.size()-1]=='"') line=line.substr(1,line.size()-2);
  if(SvenUI::SafeArgument(line) && line.find("..") == std::string::npos && line[0]!='/' && line.find(':')==std::string::npos) tracks.push_back(line);
 }
 ++revision;
}
static void PlayTrack()
{
 if(tracks.empty()) return;
 trackIndex=(trackIndex+(int)tracks.size())%tracks.size();
 char cmd[320]; snprintf(cmd,sizeof(cmd),"mp3 playfile \"%s\"",tracks[trackIndex].c_str());
 gEngfuncs.pfnClientCmd(cmd); mediaPaused=false; ++revision;
}
static void MediaPlay() { PlayTrack(); }
static void MediaStop() { gEngfuncs.pfnClientCmd("mp3 stop"); mediaPaused=false; }
static void MediaPause() { mediaPaused=!mediaPaused; gEngfuncs.pfnClientCmd(mediaPaused?"cd pause":"cd resume"); }
static void MediaNext() { ++trackIndex; PlayTrack(); }
static void MediaPrevious() { --trackIndex; PlayTrack(); }
static void MediaRandom() { if(!tracks.empty()) {trackIndex=gEngfuncs.pfnRandomLong(0,tracks.size()-1);PlayTrack();} }
static void MediaVolumeUp() { gEngfuncs.Cvar_SetValue("MP3Volume",std::min(1.f,gEngfuncs.pfnGetCvarFloat("MP3Volume")+.1f)); }
static void MediaVolumeDown() { gEngfuncs.Cvar_SetValue("MP3Volume",std::max(0.f,gEngfuncs.pfnGetCvarFloat("MP3Volume")-.1f)); }
static void MediaReload() { LoadPlaylist(playlistName.c_str()); }
static int Playlist(const char *,int size,void *data)
{
 SvenUI::Reader r(data,size); std::string name=r.string(); if(!r.done()) return 0;
 LoadPlaylist(name.c_str()); return 1;
}

bool SvenUI_CameraActive() { return camera.active; }
bool SvenUI_Capturing() { return camera.active || uiCaptured; }
void SvenUI_Capture(bool enabled)
{
 if(enabled==uiCaptured) return;
 uiCaptured=enabled;
 gEngfuncs.Cvar_SetValue("cl_sven_ui_capture",enabled?1:0);
 if(enabled) gEngfuncs.pfnClientCmd("-forward;-back;-moveleft;-moveright;-left;-right;-lookup;-lookdown;-attack;-attack2;-jump;-duck;-use;-speed;-strafe;-klook\n");
 if(gMobileEngfuncs) gMobileEngfuncs->pfnTouchSetClientOnly(enabled || camera.active);
}
static void Server(const char *command) { gEngfuncs.pfnServerCmd((char *)command); }
static void Mouse(int button,int action)
{
 if(!camera.active) return;
 float screen[3],world[3]; camera.screen(screen);
 gEngfuncs.pTriAPI->ScreenToWorld(screen,world);
 char command[256];
 snprintf(command,sizeof(command),"cam_mouse_%d_%d_%.2f_%.2f_%.2f_%.2f_%.2f",action,button,screen[0],screen[1],world[0],world[1],world[2]);
 Server(command);
}
static void MouseButton(int button,bool down)
{
 if(button<0 || button>2 || camera.buttons[button]==down) return;
 camera.buttons[button]=down; Mouse(button,down?1:0);
}
static void CameraKey(int key,bool down)
{
 if(!camera.active || key<0 || key>=103 || camera.keys[key]==down) return;
 camera.keys[key]=down;
 char command[48]; snprintf(command,sizeof(command),"cam_key %d %d",down?1:0,key); Server(command);
}
static void Motion(float x,float y)
{
 camera.move(x-camera.x,y-camera.y);
 float now=gEngfuncs.GetClientTime();
 if(now<camera.nextMotion && camera.nextMotion-now<1) return;
 int button=0; while(button<3 && !camera.buttons[button]) ++button;
 Mouse(button==3?0:button,button==3?4:3);
 camera.nextMotion=now+.05f; // Native client rate: 20 Hz, clicks are never throttled.
}
// Find real map exit buttons, rather than pretending that closing VGUI exits
// trigger_camera. stadium4 uses func_button -> multi_manager -> exitcamera.
// Follow only explicit target links, with a depth bound; never execute entity
// targets or console commands locally.
typedef std::map<std::string,std::string> MapEntity;
static std::vector<MapEntity> mapEntities;
static std::string Property(const MapEntity &e,const char *key)
{
 MapEntity::const_iterator it=e.find(key); return it==e.end()?std::string():it->second;
}
static bool ExitTarget(const std::string &name,int depth,int &budget)
{
 if(name.empty() || depth>8 || --budget<0) return false;
 for(size_t i=0;i<mapEntities.size();++i) {
  const MapEntity &e=mapEntities[i]; if(Property(e,"targetname")!=name) continue;
  std::string cls=Property(e,"classname");
  if(cls=="trigger_camera") {
   std::string wait=Property(e,"wait"); float time=(float)atof(wait.c_str());
   if(!wait.empty() && time>0 && time<=.1f && !(atoi(Property(e,"spawnflags").c_str()) & (16|128|512))) return true;
  } else if(cls=="multi_manager") {
   for(MapEntity::const_iterator it=e.begin();it!=e.end();++it) {
    std::string target=it->first.substr(0,it->first.find('#'));
    if(target!="targetname" && target!="classname" && target!="origin" && ExitTarget(target,depth+1,budget)) return true;
   }
  } else if(cls=="trigger_relay" && ExitTarget(Property(e,"target"),depth+1,budget)) return true;
 }
 return false;
}
static void ReadMapEntities(model_t *world)
{
 mapEntities.clear(); if(!world || !world->entities) return;
 char *p=world->entities,token[1024];
 while(p && mapEntities.size()<16384) {
  p=gEngfuncs.COM_ParseFile(p,token); if(!p || strcmp(token,"{")) break;
  MapEntity e;
  while(p) {
   p=gEngfuncs.COM_ParseFile(p,token); if(!p || !strcmp(token,"}")) break;
   std::string key=token; p=gEngfuncs.COM_ParseFile(p,token); if(!p) break; e[key]=token;
  }
  mapEntities.push_back(e);
 }
}
static bool ClickMapExit()
{
 cl_entity_t *worldEntity=gEngfuncs.GetEntityByIndex(0);
 model_t *world=worldEntity?worldEntity->model:NULL;
 if(!world) return false;
 if(mapEntities.empty()) ReadMapEntities(world);
 for(size_t i=0;i<mapEntities.size();++i) {
  const MapEntity &e=mapEntities[i];
  int budget=128;
  if(Property(e,"classname")!="func_button" || !ExitTarget(Property(e,"target"),0,budget)) continue;
  std::string model=Property(e,"model"); if(model.empty() || model[0]!='*') continue;
  int sub=atoi(model.c_str()+1); if(sub<=0 || sub>=world->numsubmodels || !world->submodels) continue;
  float point[3],screen[3];
  for(int j=0;j<3;++j) point[j]=(world->submodels[sub].mins[j]+world->submodels[sub].maxs[j])*.5f;
  if(gEngfuncs.pTriAPI->WorldToScreen(point,screen) || screen[0]<-1 || screen[0]>1 || screen[1]<-1 || screen[1]>1) continue;
  char command[256];
  for(int down=1;down>=0;--down) {
   snprintf(command,sizeof(command),"cam_mouse_%d_0_%.2f_%.2f_%.2f_%.2f_%.2f",down,screen[0],screen[1],point[0],point[1],point[2]); Server(command);
  }
  return true;
 }
 return false;
}
void SvenUI_ExitCamera()
{
 if(!camera.active) return;
 float now=gEngfuncs.GetClientTime();
 if(camera.exiting && now>=lastExitRequest && now-lastExitRequest<.25f) return;
 lastExitRequest=now;
 for(int i=0;i<3;++i) MouseButton(i,false);
 // Escape is also delivered to map scripts. Do not locally dismiss the camera:
 // CameraMouse(0) is the authoritative acknowledgement from trigger_camera.
 CameraKey(69,true); CameraKey(69,false);
 ClickMapExit();
 camera.exiting=true;
}

#if USE_VGUI
#include "vgui_TeamFortressViewport.h"
#include <VGUI_KeyCode.h>
#include <VGUI_MouseCode.h>
#include <VGUI_SurfaceBase.h>
#include <VGUI_Bitmap.h>
#include <VGUI_Cursor.h>
#include <VGUI_LineBorder.h>
#include "input_mouse.h"

class SvenMenu;
static SvenMenu *menus[31] = {};
class CameraPanel;
static CameraPanel *cameraPanel;
static void Refresh();
static void CloseMenu(int id);

class MenuAction: public vgui::ActionSignal
{
 SvenMenu *owner; int action,value;
public:
 MenuAction(SvenMenu *p,int a,int v=0):owner(p),action(a),value(v) {}
 void actionPerformed(vgui::Panel *);
};
class SvenButton: public vgui::Button
{
public:
 SvenButton(const char *text,int x,int y,int w,int h):vgui::Button(text,x,y,w,h) {}
 void paintBackground() {
  int w,h;getPaintSize(w,h);drawSetColor(0,0,0,100);drawFilledRect(0,0,w,h);
  int thickness=isSelected()?3:1;
  drawSetColor(210,220,230,40);
  drawFilledRect(0,0,w,thickness);drawFilledRect(0,h-thickness,w,h);
  drawFilledRect(0,0,thickness,h);drawFilledRect(w-thickness,0,w,h);
 }
};
class SvenMenu: public CMenuPanel
{
 vgui::Label *title,*detail,*pageLabel;
 vgui::Button *rows[8],*tabs[4],*previous,*next,*exit;
 unsigned seen;
 int page,choice,kind,selected,numPages;
 std::vector<std::string> playerIds;
public:
 SvenMenu(int id): CMenuPanel(80,0,XRES(60),YRES(30),XRES(520),YRES(420)),seen(~0u),page(0),choice(0),kind(id),selected(-1),numPages(1)
 {
  SetMenuID(id);
  title=new vgui::Label("",XRES(12),YRES(8),XRES(id==22 || id==23 || id==24 ? 320 : 490),YRES(30)); title->setParent(this); title->setBgColor(0,0,0,140); title->setFgColor(245,245,245,0); title->setContentAlignment(vgui::Label::a_west); title->setBorder(new vgui::LineBorder(vgui::Color(210,220,230,40)));
  if(id==22 || id==23 || id==24) {
   vgui::Label *caption=new vgui::Label("VOTE MENU",XRES(340),YRES(8),XRES(162),YRES(30));
   caption->setParent(this); caption->setBgColor(0,0,0,140); caption->setFgColor(245,245,245,0);
   caption->setContentAlignment(vgui::Label::a_east);
   caption->setBorder(new vgui::LineBorder(vgui::Color(210,220,230,40)));
  }
  detail=new vgui::Label("",XRES(12),YRES(312),XRES(490),YRES(53)); detail->setParent(this); detail->setBgColor(0,0,0,180); detail->setFgColor(245,245,245,0);
  for(int i=0;i<4;++i) tabs[i]=Button("",12+i*124,44,120,30,10,i);
  for(int i=0;i<8;++i) rows[i]=Button("",12,80+i*28,496,26,20,i);
  previous=Button("<",12,378,65,30,30,-1);
  next=Button(">",83,378,65,30,30,1);
  pageLabel=new vgui::Label("",XRES(154),YRES(378),XRES(110),YRES(30)); pageLabel->setParent(this); pageLabel->setBgColor(0,0,0,140); pageLabel->setFgColor(235,240,255,0); pageLabel->setContentAlignment(vgui::Label::a_west);
  exit=Button("EXIT",395,378,113,30,40);
  Update();
 }
 vgui::Button *Button(const char *s,int x,int y,int w,int h,int a,int v=0)
 {
  vgui::Button *b=new SvenButton(s,XRES(x),YRES(y),XRES(w),YRES(h)); b->setParent(this);
  b->setFont(vgui::Scheme::sf_primary3); b->setBgColor(20,30,40,120); b->setFgColor(235,240,255,0);
  b->addActionSignal(new MenuAction(this,a,v)); return b;
 }
 bool SlotInput(int slot) {
  if(slot==0) { Action(40,0); return true; }
  if(slot>=1 && slot<=8 && rows[slot-1]->isVisible()) { Action(20,slot-1); return true; }
  return false;
 }
 void Close() { CMenuPanel::Close(); }
 void Open() { Update(); CMenuPanel::Open(); }
 void paintBackground() { if(seen!=revision) Update(); CMenuPanel::paintBackground(); }
 void Update()
 {
  seen=revision;
  for(int i=0;i<8;++i) rows[i]->setVisible(false);
  for(int i=0;i<4;++i) tabs[i]->setVisible(false);
  detail->setText("%s",""); previous->setVisible(false); next->setVisible(false);
  std::vector<std::string> names;
  if(kind==22 || kind==23) {
   const char *labels[]={"Vote kick","Vote ban","Vote kill","Vote map"};
   for(int i=0;i<4;++i) { tabs[i]->setText("%s",labels[i]); tabs[i]->setVisible(true); }
   const char *headings[]={"Vote Kick","Vote Ban","Vote Kill","Vote Map"};
   title->setText("%s",headings[choice]);
   if(choice==3) names=maps;
   else {
    playerIds.clear();
    for(int i=1;i<=gEngfuncs.GetMaxClients();++i) { hud_player_info_t p={}; gEngfuncs.pfnGetPlayerInfo(i,&p);
     if(p.name && *p.name) { names.push_back(p.name); playerIds.push_back(p.name); }
    }
   }
   if(names.empty()) detail->setText("%s",choice==3?"Waiting for the server map list.":"No players available.");
  } else if(kind==24) {
   title->setText("%s",vote.title.c_str());
   names.push_back(vote.yes.empty()?"Yes":vote.yes); names.push_back(vote.no.empty()?"No":vote.no);
  } else if(kind==26) {
   title->setText("%s","Sven Co-op - Inventory");
   const char *labels[]={"Activate","Drop","Drop all",""};
   for(int i=0;i<3;++i) { tabs[i]->setText("%s",labels[i]); tabs[i]->setVisible(true); tabs[i]->setEnabled(i==2 || (selected>=0 && selected<(int)items.size() && (i?items[selected].drop:items[selected].activate))); }
   for(size_t i=0;i<items.size();++i) names.push_back(items[i].title.empty()?items[i].name:items[i].title);
   if(selected>=0 && selected<(int)items.size()) detail->setText("%s",items[selected].description.c_str());
   else detail->setText("%s",items.empty()?"No inventory items.":"Select an item.");
  } else if(kind==30) {
   title->setText("%s","Sven Co-op - Media Player");
   const char *labels[]={"Play","Pause / Resume","Stop","Previous","Next","Random","Volume +","Volume -"};
   names.assign(labels,labels+8);
   names.insert(names.end(),tracks.begin(),tracks.end());
   detail->setText("%s",tracks.empty()?"No tracks in playlist.txt / playlist.m3u.":tracks[std::max(0,std::min(trackIndex,(int)tracks.size()-1))].c_str());
  }
  int pages=std::max(1,((int)names.size()+7)/8); page=std::max(0,std::min(page,pages-1)); numPages=pages;
  for(int i=0;i<8 && page*8+i<(int)names.size();++i) { rows[i]->setText("%s",names[page*8+i].c_str()); rows[i]->setVisible(true); }
  // pager UI is always open on multi-page lists; < > wrap around (see Action 30)
  previous->setVisible(pages>1); next->setVisible(pages>1);
  pageLabel->setVisible(pages>1);
  if(pages>1) pageLabel->setText("%d/%d",page+1,pages);
 }
 void Action(int action,int value)
 {
  if(action==40) { gViewPort->HideTopMenu(); return; }
  if(action==30) { page+=value; if(page<0) page=numPages-1; else if(page>=numPages) page=0; Update(); return; }
  if(action==10) {
   if(kind==22 || kind==23) { choice=value; page=0; Update(); }
   else if(kind==26) {
    if(value==2) Server("dropitem \"*\"");
    else if(selected>=0 && selected<(int)items.size() && (value?items[selected].drop:items[selected].activate)) {
     char cmd[64]; snprintf(cmd,sizeof(cmd),"%sitem \"#%d\"",value?"drop":"activate",items[selected].id); Server(cmd);
    }
   }
   return;
  }
  int index=page*8+value;
  if(kind==24) { Server(index?"voteno":"voteyes"); gViewPort->HideTopMenu(); }
  else if(kind==26) { selected=index; Update(); }
  else if(kind==22 || kind==23) {
   char cmd[320];
   if(choice==3) {
    if(index>=(int)maps.size() || !SvenUI::SafeArgument(maps[index])) return;
    snprintf(cmd,sizeof(cmd),"votemap \"%s\"",maps[index].c_str());
   } else {
    if(index>=(int)playerIds.size()) return;
    const char *commands[]={"votekick","voteban","votekill"};
    if(!SvenUI::SafeArgument(playerIds[index])) return;
    snprintf(cmd,sizeof(cmd),"%s \"%s\"",commands[choice],playerIds[index].c_str());
   }
   Server(cmd); gViewPort->HideTopMenu();
  } else if(kind==30) {
   void (*actions[])()={MediaPlay,MediaPause,MediaStop,MediaPrevious,MediaNext,MediaRandom,MediaVolumeUp,MediaVolumeDown};
   if(index<8) actions[index]();
   else if(index-8<(int)tracks.size()) { trackIndex=index-8; PlayTrack(); }
  }
 }
 void SetMapMode() { choice=3; Update(); }
};
void MenuAction::actionPerformed(vgui::Panel *) { owner->Action(action,value); }

// Bitmap pixels are generated from the supplied PojavLauncher cursor resource.
#include "sven_ui_cursor.h"
class PojavBitmap: public vgui::Bitmap
{
public:
 PojavBitmap() { setSize(36,54); for(int y=0;y<54;++y) for(int x=0;x<36;++x) { unsigned p=(y*36+x)*2; setRGBA(x,y,pojavCursor[p],pojavCursor[p],pojavCursor[p],pojavCursor[p+1]); } }
};
class CameraInput: public vgui::InputSignal
{
public:
 void cursorMoved(int x,int y,vgui::Panel *) { if(dragFinger<0 && fingers[0]<0 && fingers[1]<0 && fingers[2]<0 && fingers[3]<0) Motion((float)x/ScreenWidth,(float)y/ScreenHeight); }
 void cursorEntered(vgui::Panel *) {}
 void cursorExited(vgui::Panel *) {}
 void mousePressed(vgui::MouseCode c,vgui::Panel *) { MouseButton(c,true); }
 void mouseDoublePressed(vgui::MouseCode c,vgui::Panel *) { MouseButton(c,true); }
 void mouseReleased(vgui::MouseCode c,vgui::Panel *) { MouseButton(c,false); }
 void mouseWheeled(int d,vgui::Panel *) { Mouse(d,5); }
 void keyPressed(vgui::KeyCode c,vgui::Panel *) { if(c==vgui::KEY_ESCAPE) SvenUI_ExitCamera(); else CameraKey(c,true); }
 void keyTyped(vgui::KeyCode,vgui::Panel *) {}
 void keyReleased(vgui::KeyCode c,vgui::Panel *) { CameraKey(c,false); }
 void keyFocusTicked(vgui::Panel *) {}
};
class CameraPanel: public CMenuPanel
{
 PojavBitmap cursor;
public:
 CameraPanel(): CMenuPanel(255,0,0,0,ScreenWidth,ScreenHeight) { addInputSignal(new CameraInput); }
 void Open() { CMenuPanel::Open(); requestFocus(); }
 void Close() { if(camera.active) { SvenUI_ExitCamera(); return; } CMenuPanel::Close(); }
 void paintBackground()
 {
  if(cameraSprite) {
   const struct model_s *model=gEngfuncs.GetSpritePointer(cameraSprite);
   if(model && gEngfuncs.pTriAPI->SpriteTexture((struct model_s *)model,0)) {
    gEngfuncs.pTriAPI->RenderMode(kRenderTransTexture); gEngfuncs.pTriAPI->Color4f(1,1,1,1);
    gEngfuncs.pTriAPI->Begin(TRI_QUADS);
    gEngfuncs.pTriAPI->TexCoord2f(0,0); gEngfuncs.pTriAPI->Vertex3f(0,0,0);
    gEngfuncs.pTriAPI->TexCoord2f(1,0); gEngfuncs.pTriAPI->Vertex3f(ScreenWidth,0,0);
    gEngfuncs.pTriAPI->TexCoord2f(1,1); gEngfuncs.pTriAPI->Vertex3f(ScreenWidth,ScreenHeight,0);
    gEngfuncs.pTriAPI->TexCoord2f(0,1); gEngfuncs.pTriAPI->Vertex3f(0,ScreenHeight,0);
    gEngfuncs.pTriAPI->End(); gEngfuncs.pTriAPI->RenderMode(kRenderNormal);
   }
  }
 }
 void paint()
 {
  if(gMobileEngfuncs) {
   DrawControl("MOUSE1",0); DrawControl("MOUSE2",1); DrawControl(keyboard?"KEYBOARD ON":"KEYBOARD",2); DrawControl(camera.exiting?"EXIT ...":"EXIT",3);
  }
  cursor.setPos(camera.x*ScreenWidth,camera.y*ScreenHeight); cursor.doPaint(this);
 }
 static void Bounds(int i,int &x,int &y,int &w,int &h)
 {
  // controls.json (scale 130): preserve corner anchors and square mouse buttons.
  float scale=ScreenHeight/480.f;
  if(i<2) { w=h=145.45454f*scale; x=i*(w+4*scale); y=.00589054f*ScreenHeight; }
  else if(i==2) { w=104*scale; h=39*scale; x=.98970854f*ScreenWidth-w; y=.01509316f*ScreenHeight; }
  else { w=h=65*scale; x=.9760203f*ScreenWidth-w; y=ScreenHeight-h; }
 }
 void DrawControl(const char *s,int i)
 {
  int x,y,w,h; Bounds(i,x,y,w,h);
  drawSetColor(0,0,0,178); drawFilledRect(x,y,x+w,y+h);
  int tw=0,th=0; gEngfuncs.pfnDrawConsoleStringLen(s,&tw,&th);
  gEngfuncs.pfnDrawSetTextColor(1,1,1); gEngfuncs.pfnDrawConsoleString(x+(w-tw)/2,y+(h-th)/2,(char *)s);
 }
};
void SvenUI_RefreshCursor()
{
 if(!camera.active) return;
 CurrentMouseInput()->IN_SetVisibleMouse(true);
 // Keep VGUI input active while rendering the reference cursor ourselves.
 static vgui::Cursor *invisible;
 if(!invisible) { class EmptyBitmap: public vgui::Bitmap { public: EmptyBitmap() {setSize(1,1);setRGBA(0,0,0,0,0,0);} }; invisible=new vgui::Cursor(new EmptyBitmap,0,0); }
 vgui::App::getInstance()->setCursorOveride(invisible);
}
CMenuPanel *SvenUI_CreateMenu(int id)
{
 if(id==25) {
  if(!camera.active) return NULL;
  if(!cameraPanel) cameraPanel=new CameraPanel;
  return cameraPanel;
 }
 if(id!=22 && id!=23 && id!=24 && id!=26 && id!=30) return NULL;
 if(!menus[id]) menus[id]=new SvenMenu(id);
 if(id==23) menus[id]->SetMapMode(); return menus[id];
}
static void CloseMenu(int id)
{
 if(gViewPort) gViewPort->DismissMenu(id);
}
static void Refresh() { ++revision; }
#else
static void CloseMenu(int) {}
static void Refresh() { ++revision; }
#endif

static int CameraMessage(const char *,int size,void *data)
{
 SvenUI::Reader r(data,size); int mode=r.byte(); std::string sprite;
 if(mode==2) sprite=r.string();
 if(!r.done() || mode>2) return 0;
#if USE_VGUI
 if(!gViewPort) return 0;
 if(!mode) {
  camera=SvenUI::Camera(); cameraSprite=0;
  SvenUI_CloseKeyboard(); dragFinger=-1; for(int i=0;i<4;++i) fingers[i]=-1;
  gEngfuncs.Cvar_SetValue("cl_sven_camera_mouse",0);
  if(gMobileEngfuncs) gMobileEngfuncs->pfnTouchSetClientOnly(false);
  CloseMenu(25); gViewPort->UpdateCursorState();
 } else {
  if(!camera.active) {
   gViewPort->HideVGUIMenu(); camera=SvenUI::Camera(); camera.active=true;
   // Key-up events belong to the panel now: release gameplay bindings first.
   gEngfuncs.pfnClientCmd("-forward;-back;-moveleft;-moveright;-left;-right;-lookup;-lookdown;-attack;-attack2;-jump;-duck;-use;-speed;-strafe;-klook\n");
  }
  camera.exiting=false;
  cameraSprite=sprite.empty()?0:gEngfuncs.pfnSPR_Load(sprite.c_str());
  gEngfuncs.Cvar_SetValue("cl_sven_camera_mouse",1);
  if(gMobileEngfuncs) gMobileEngfuncs->pfnTouchSetClientOnly(true);
  gViewPort->ShowVGUIMenu(25); SvenUI_RefreshCursor();
 }
#endif
 return 1;
}
static int VoteMessage(const char *,int size,void *data)
{
 if(!SvenUI::ReadVote(data,size,vote)) return 0;
 voteEnd=gEngfuncs.GetClientTime()+vote.duration; Refresh();
#if USE_VGUI
 if(gViewPort) gViewPort->ShowVGUIMenu(24);
#endif
 return 1;
}
static int EndVote(const char *,int size,void *) { if(size) return 0; CloseMenu(24); return 1; }
static int AddItem(const char *,int size,void *data)
{
 SvenUI::Item item; if(!SvenUI::ReadItem(data,size,item)) return 0;
 for(size_t i=0;i<items.size();++i) if(items[i].id==item.id) { items[i]=item; Refresh(); return 1; }
 if(items.size()>=1024) return 0;
 items.push_back(item); Refresh(); return 1;
}
int SvenUI_InvRemove(const char *,int size,void *data)
{
 SvenUI::Reader r(data,size); int id=r.integer(); bool keep=r.byte()!=0;
 if(!r.done()) return 0;
 SvenUI::RemoveItems(items,id,keep); Refresh(); return 1;
}
int SvenUI_MapList(const char *,int size,void *data)
{
 if(!SvenUI::ReadMaps(data,size,maps)) return 0;
 Refresh(); return 1;
}
static void OpenVote() {
#if USE_VGUI
 if(gViewPort && !camera.active) gViewPort->ShowVGUIMenu(22);
#endif
}
static void OpenMaps() {
#if USE_VGUI
 if(gViewPort && !camera.active) gViewPort->ShowVGUIMenu(23);
#endif
}
static void OpenInventory() {
#if USE_VGUI
 if(gViewPort && !camera.active) gViewPort->ShowVGUIMenu(26);
#endif
}
static void CloseInventory() { CloseMenu(26); }
static void ServerMotd() {
#if USE_VGUI
 if(gViewPort) gViewPort->ShowVGUIMenu(MENU_INTRO);
#endif
}
static void MissionBriefing() {
#if USE_VGUI
 if(gViewPort) gViewPort->ShowVGUIMenu(MENU_MAPBRIEFING);
#endif
}
static void CloseAll() {
#if USE_VGUI
 if(gViewPort) gViewPort->HideVGUIMenu();
#endif
}
static void OpenMedia() {
#if USE_VGUI
 if(gViewPort && !camera.active) gViewPort->ShowVGUIMenu(30);
#endif
}
void SvenUI_Init()
{
 gEngfuncs.pfnRegisterVariable("cl_sven_camera_mouse","0",0);
 gEngfuncs.pfnRegisterVariable("cl_sven_ui_capture","0",0);
 gEngfuncs.pfnHookUserMsg("CameraMouse",CameraMessage);
 gEngfuncs.pfnHookUserMsg("Playlist",Playlist);
 gEngfuncs.pfnHookUserMsg("VoteMenu",VoteMessage);
 gEngfuncs.pfnHookUserMsg("EndVote",EndVote);
 gEngfuncs.pfnHookUserMsg("InvAdd",AddItem);
 gEngfuncs.pfnHookUserMsg("InvRemove",SvenUI_InvRemove);
 gEngfuncs.pfnHookUserMsg("MapList",SvenUI_MapList);
 gEngfuncs.pfnAddCommand("votemenu",OpenVote);
 gEngfuncs.pfnAddCommand("votemapmenu",OpenMaps);
 gEngfuncs.pfnAddCommand("vgui_votemap",OpenMaps);
 gEngfuncs.pfnAddCommand("inventory",OpenInventory);
 gEngfuncs.pfnAddCommand("+inventory",OpenInventory);
 gEngfuncs.pfnAddCommand("-inventory",CloseInventory);
 gEngfuncs.pfnAddCommand("mediamenu",OpenMedia);
 gEngfuncs.pfnAddCommand("MediaPlayer",OpenMedia);
 gEngfuncs.pfnAddCommand("PlayMedia",MediaPlay);
 gEngfuncs.pfnAddCommand("PauseSong",MediaPause);
 gEngfuncs.pfnAddCommand("StopSong",MediaStop);
 gEngfuncs.pfnAddCommand("NextSong",MediaNext);
 gEngfuncs.pfnAddCommand("LastSong",MediaPrevious);
 gEngfuncs.pfnAddCommand("RandomSong",MediaRandom);
 gEngfuncs.pfnAddCommand("IncVol",MediaVolumeUp);
 gEngfuncs.pfnAddCommand("DecVol",MediaVolumeDown);
 gEngfuncs.pfnAddCommand("RelPls",MediaReload);
 LoadPlaylist(NULL);
 gEngfuncs.pfnAddCommand("vgui_closeall",CloseAll);
 gEngfuncs.pfnAddCommand("servermotd",ServerMotd);
 gEngfuncs.pfnAddCommand("missionbriefing",MissionBriefing);
}
void SvenUI_CloseKeyboard()
{
 if(keyboard && gMobileEngfuncs) gMobileEngfuncs->pfnEnableTextInput(false);
 keyboard=false;
}
void SvenUI_Reset(bool preserveMaps)
{
 camera=SvenUI::Camera(); cameraSprite=0; SvenUI_Capture(false); mapEntities.clear(); items.clear(); if(!preserveMaps) maps.clear(); voteEnd=0; Refresh();
 SvenUI_CloseKeyboard(); dragFinger=-1; for(int i=0;i<4;++i) fingers[i]=-1;
 gEngfuncs.Cvar_SetValue("cl_sven_camera_mouse",0);
 if(gMobileEngfuncs) gMobileEngfuncs->pfnTouchSetClientOnly(false);
#if USE_VGUI
 if(cameraPanel) { cameraPanel->Close(); cameraPanel->Reset(); }
 for(int i=0;i<31;++i) if(menus[i]) { menus[i]->Close(); menus[i]->Reset(); }
#endif
}
int SvenUI_Key(int down,int key)
{
 if(!camera.active) return 1;
 // VGUI supplies native key codes for the remaining keys.
 return 0;
}
extern "C" int DLLEXPORT IN_ClientTouchEvent(int type,int finger,float x,float y,float dx,float dy)
{
 // Center touch-orbit dot first: it consumes its own touches so the look
 // area never steals a drag started on the dot.
 if( gHUD.m_TouchOrbit.Event( type, finger, x, y, dx, dy ) )
  return 1;
 if(!camera.active) return 0;
#if USE_VGUI
 if(type==0) { // event_down
  int hit=-1;
  for(int i=0;i<4;++i) { int bx,by,bw,bh; CameraPanel::Bounds(i,bx,by,bw,bh);
   if(x*ScreenWidth>=bx && x*ScreenWidth<bx+bw && y*ScreenHeight>=by && y*ScreenHeight<by+bh) {hit=i;break;}
  }
  if(hit<0 && dragFinger<0) dragFinger=finger;
  if(hit>=0 && fingers[hit]<0) {
   fingers[hit]=finger;
   if(hit<2) MouseButton(hit,true);
   else if(hit==2 && gMobileEngfuncs) {keyboard=!keyboard;gMobileEngfuncs->pfnEnableTextInput(keyboard);}
   else if(hit==3) SvenUI_ExitCamera();
  }
 } else if(type==1) { // event_up
  if(dragFinger==finger) dragFinger=-1;
  for(int i=0;i<4;++i) if(fingers[i]==finger) {if(i<2) MouseButton(i,false);fingers[i]=-1;}
 } else {
  if(dragFinger==finger) Motion(camera.x+dx,camera.y+dy);
 }
#endif
 return 1;
}

// Traceable crosshair state (overrides engine SetCrosshair: the sprite is
// drawn at the eye-ray impact point, not the screen center).
HSPRITE m_hsprCrosshair;
wrect_t m_rcCrosshair;
static HSPRITE m_hsprScopeOverlay;
static wrect_t m_rcScopeOverlay;
void SetCrosshair( HSPRITE sprite, wrect_t size, int k, int l, int m )
{
 (void)k; (void)l; (void)m;
 m_hsprCrosshair = sprite;
 m_rcCrosshair = size;
}
void SetScopeOverlay( HSPRITE sprite, wrect_t size )
{
	m_hsprScopeOverlay = sprite;
	m_rcScopeOverlay = size;
}

#define ESF_CROSSHAIR_MAX_DIST 8192.0f

int CHudEsfCrosshair::Init( void )
{
 gHUD.AddHudElem( this );
 CHudBase::m_iFlags |= HUD_ACTIVE;
 m_hsprCrosshair = 0;
 return 1;
}
int CHudEsfCrosshair::VidInit( void )
{
 return 1;
}
int CHudEsfCrosshair::Draw( float flTime )
{
 (void)flTime;
	if( !m_hsprCrosshair && !m_hsprScopeOverlay )
		return 0;
 if( gHUD.m_fPlayerDead || g_iUser1 )
  return 0;
 cl_entity_t *local = gEngfuncs.GetLocalPlayer();
 if( !local )
  return 0;
	if( gEngfuncs.pfnGetCvarFloat( "crosshair" ) == 0.0f )
		return 0;
	int w = m_rcCrosshair.right - m_rcCrosshair.left;
	int h = m_rcCrosshair.bottom - m_rcCrosshair.top;
	int scopeW = m_rcScopeOverlay.right - m_rcScopeOverlay.left;
	int scopeH = m_rcScopeOverlay.bottom - m_rcScopeOverlay.top;
	if( gHUD.m_iFOV < 90 && m_hsprScopeOverlay && scopeW > 0 && scopeH > 0 )
	{
		SPR_Set( m_hsprScopeOverlay, 255, 255, 255 );
		if( gEngfuncs.pfnGetCvarFloat( "gl_spriteblend" ) == 0.0f )
			SPR_DrawHoles( 0, ( ScreenWidth - scopeW ) / 2, ( ScreenHeight - scopeH ) / 2, &m_rcScopeOverlay );
		else
			SPR_DrawAdditive( 0, ( ScreenWidth - scopeW ) / 2, ( ScreenHeight - scopeH ) / 2, &m_rcScopeOverlay );
	}
	if( !CL_IsThirdPerson() )
	{
	  if( m_hsprCrosshair && m_hsprCrosshair != m_hsprScopeOverlay && w > 0 && h > 0 )
	  {
	   SPR_Set( m_hsprCrosshair, 255, 255, 255 );
	   if( gEngfuncs.pfnGetCvarFloat( "gl_spriteblend" ) == 0.0f )
			SPR_DrawHoles( 0, ( ScreenWidth - w ) / 2, ( ScreenHeight - h ) / 2, &m_rcCrosshair );
		else
			SPR_DrawAdditive( 0, ( ScreenWidth - w ) / 2, ( ScreenHeight - h ) / 2, &m_rcCrosshair );
	  }
	  return 1;
	}
 vec3_t org, view_ofs, forward, end, screen;
 VectorCopy( local->origin, org );
 gEngfuncs.pEventAPI->EV_LocalPlayerViewheight( view_ofs );
 VectorAdd( org, view_ofs, org );
 AngleVectors( gHUD.m_vecAngles, forward, NULL, NULL );
 VectorMA( org, ESF_CROSSHAIR_MAX_DIST, forward, end );
 pmtrace_t tr;
 gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
 gEngfuncs.pEventAPI->EV_SetSolidPlayers( local->index - 1 );
 gEngfuncs.pEventAPI->EV_PlayerTrace( org, end, PM_NORMAL, -1, &tr );
 // TriAPI WorldToScreen always writes screen[2] = 0 and reports
 // clipping via its return value (1 = z-clipped/behind), so the return
 // value -- not screen[2] -- is the behind-camera test.
 if( gEngfuncs.pTriAPI->WorldToScreen( tr.endpos, screen ) )
  return 0; // behind the camera
	if( m_hsprCrosshair && m_hsprCrosshair != m_hsprScopeOverlay && w > 0 && h > 0 )
	{
	 SPR_Set( m_hsprCrosshair, 255, 255, 255 );
	 if( gEngfuncs.pfnGetCvarFloat( "gl_spriteblend" ) == 0.0f )
		SPR_DrawHoles( 0, XPROJECT( screen[0] ) - w / 2, YPROJECT( screen[1] ) - h / 2, &m_rcCrosshair );
	 else
		SPR_DrawAdditive( 0, XPROJECT( screen[0] ) - w / 2, YPROJECT( screen[1] ) - h / 2, &m_rcCrosshair );
	}
 return 1;
}

// Center touch-orbit control: fully invisible box at the screen center,
// active only in third person. Dragging it orbits cam_idealyaw/
// cam_idealpitch (hotter than touch look, accelerating); yaw snaps back
// to 0 exactly 7s after release, or immediately on sc_chasecam toggle.
#define TOUCH_ORBIT_SIZE_PX 121.0f
#define TOUCH_ORBIT_REVERT_TIME 7.0f
#define TOUCH_ORBIT_GAIN 2.0f
#define TOUCH_ORBIT_ACCEL 400.0f
#define TOUCH_ORBIT_GAIN_MAX 5.0f
#define TOUCH_ORBIT_DOUBLE_TAP 0.9f
#define TOUCH_ORBIT_LOCK_OFFSET -24.0f

int CHudTouchOrbit::Init( void )
{
 gHUD.AddHudElem( this );
 CHudBase::m_iFlags |= HUD_ACTIVE;
 m_finger = -1;
 m_session = false;
 m_locked = false;
 m_restoreAt = 0.0f;
 m_lastTap = -1.0f;
 m_lookFingers[0] = m_lookFingers[1] = -1;
 return 1;
}
int CHudTouchOrbit::VidInit( void )
{
 return 1;
}
static void TouchOrbitRect( float &x0, float &y0, float &x1, float &y1 )
{
 float hw = ( TOUCH_ORBIT_SIZE_PX * 0.5f ) / (float)ScreenWidth;
 float hh = ( TOUCH_ORBIT_SIZE_PX * 0.5f ) / (float)ScreenHeight;
 x0 = 0.5f - hw; x1 = 0.5f + hw;
 y0 = 0.5f - hh; y1 = 0.5f + hh;
}
int CHudTouchOrbit::Draw( float flTime )
{
 (void)flTime;
 if( m_locked && !CL_IsThirdPerson() )
  Cancel();
 if( m_locked )
 {
  float x0, y0, x1, y1;
  TouchOrbitRect( x0, y0, x1, y1 );
  int x = (int)( x0 * ScreenWidth );
  int y = (int)( y0 * ScreenHeight );
  int w = (int)( ( x1 - x0 ) * ScreenWidth );
  int h = (int)( ( y1 - y0 ) * ScreenHeight );
  FillRGBA( x, y, w, 2, 255, 180, 32, 128 );
  FillRGBA( x, y + h - 2, w, 2, 255, 180, 32, 128 );
  FillRGBA( x, y, 2, h, 255, 180, 32, 128 );
  FillRGBA( x + w - 2, y, 2, h, 255, 180, 32, 128 );
 }
 // Fully invisible by request (opacity fully down), but the pending 7s
 // restore still runs here. Touches keep working (see Event); the center
 // crosshair marks the spot instead of the old box/dot.
 if( m_session && !m_locked && m_restoreAt > 0.0f && gEngfuncs.GetClientTime() >= m_restoreAt )
  Cancel();
 return 1;
}
int CHudTouchOrbit::Event( int type, int finger, float x, float y, float dx, float dy )
{
	if( !CL_IsThirdPerson() )
	{
	  if( m_locked ) Cancel();
	  return 0;
	}
	float nx0, ny0, nx1, ny1;
	TouchOrbitRect( nx0, ny0, nx1, ny1 );
	qboolean onCenter = x >= nx0 && x <= nx1 && y >= ny0 && y <= ny1;
	if( m_locked && (( x >= 0.5f && !onCenter ) || finger == m_lookFingers[0] || finger == m_lookFingers[1] ) )
	{
		int slot = finger == m_lookFingers[0] ? 0 : finger == m_lookFingers[1] ? 1 : -1;
		if( type == 0 && x >= 0.5f && slot < 0 )
		{
			slot = m_lookFingers[0] < 0 ? 0 : m_lookFingers[1] < 0 ? 1 : -1;
			if( slot >= 0 )
			{
				m_lookFingers[slot] = finger;
				m_lookX[slot] = x;
				m_lookY[slot] = y;
			}
			return 1;
		}
		if( slot < 0 )
			return 1;
		if( type == 1 )
		{
			m_lookFingers[slot] = -1;
			return 1;
		}
		if( m_lookFingers[0] >= 0 && m_lookFingers[1] >= 0 )
		{
			float oldDX = m_lookX[0] - m_lookX[1];
			float oldDY = m_lookY[0] - m_lookY[1];
			float oldDistance = sqrtf( oldDX * oldDX + oldDY * oldDY );
			m_lookX[slot] = x;
			m_lookY[slot] = y;
			float newDX = m_lookX[0] - m_lookX[1];
			float newDY = m_lookY[0] - m_lookY[1];
			float newDistance = sqrtf( newDX * newDX + newDY * newDY );
			float distance = gEngfuncs.pfnGetCvarFloat( "cam_idealdist" ) + ( newDistance - oldDistance ) * 300.0f;
			gEngfuncs.Cvar_SetValue( "cam_idealdist", Q_max( -150.0f, Q_min( 150.0f, distance ) ) );
		}
		else
		{
			float oldX = m_lookX[slot], oldY = m_lookY[slot];
			m_lookX[slot] = x;
			m_lookY[slot] = y;
			float syaw = gEngfuncs.pfnGetCvarFloat( "touch_yaw" );
			float spitch = gEngfuncs.pfnGetCvarFloat( "touch_pitch" );
			if( syaw == 0.0f ) syaw = 120.0f;
			if( spitch == 0.0f ) spitch = 90.0f;
			gEngfuncs.Cvar_SetValue( "cam_idealyaw", gEngfuncs.pfnGetCvarFloat( "cam_idealyaw" ) - ( x - oldX ) * syaw * TOUCH_ORBIT_GAIN );
			float pitch = gEngfuncs.pfnGetCvarFloat( "cam_idealpitch" ) + ( y - oldY ) * spitch * TOUCH_ORBIT_GAIN;
			gEngfuncs.Cvar_SetValue( "cam_idealpitch", Q_max( -90.0f, Q_min( 90.0f, pitch ) ) );
		}
		return 1;
	}
 if( type == 0 ) // down
 {
  if( m_finger >= 0 )
   return 0; // one orbit drag at a time
  if( x < nx0 || x > nx1 || y < ny0 || y > ny1 )
   return 0;
  m_finger = finger;
  m_downX = x;
  m_downY = y;
  if( !m_session )
  {
   m_baseYaw = gEngfuncs.pfnGetCvarFloat( "cam_idealyaw" );
   m_basePitch = gEngfuncs.pfnGetCvarFloat( "cam_idealpitch" );
   m_baseDist = gEngfuncs.pfnGetCvarFloat( "cam_idealdist" );
   m_baseOffsetZ = gEngfuncs.pfnGetCvarFloat( "cam_zoffset" );
  }
  m_session = true;
  m_restoreAt = 0.0f;
  float now = gEngfuncs.GetClientTime();
  if( m_lastTap >= 0.0f && now - m_lastTap <= TOUCH_ORBIT_DOUBLE_TAP )
  {
   m_locked = !m_locked;
   m_lastTap = -1.0f;
   if( m_locked )
   {
    m_restoreAt = 0.0f;
    gEngfuncs.Cvar_SetValue( "cam_zoffset", TOUCH_ORBIT_LOCK_OFFSET );
   }
   else
    m_restoreAt = now + TOUCH_ORBIT_REVERT_TIME;
  }
  return 1;
 }
 if( finger != m_finger )
  return 0;
 if( type == 1 ) // up: snap back exactly 7s later
 {
  m_finger = -1;
  if( fabsf( x - m_downX ) + fabsf( y - m_downY ) < 0.01f )
   m_lastTap = gEngfuncs.GetClientTime();
  else
   m_lastTap = -1.0f;
  if( !m_locked )
   m_restoreAt = gEngfuncs.GetClientTime() + TOUCH_ORBIT_REVERT_TIME;
  return 1;
 }
 // motion: orbit with touch-look gains, slightly hotter and accelerating
 // (ivmeli): fast flicks rotate proportionally further. dx/dy arrive as
 // screen fractions (slow drag ~1e-5, hard flick ~1e-3 squared).
 if( dx != dx || dy != dy )
  return 1;
 float syaw = gEngfuncs.pfnGetCvarFloat( "touch_yaw" );
 float spitch = gEngfuncs.pfnGetCvarFloat( "touch_pitch" );
 if( syaw == 0.0f ) syaw = 120.0f;
 if( spitch == 0.0f ) spitch = 90.0f;
 float speed2 = dx * dx + dy * dy;
 float k = TOUCH_ORBIT_GAIN * ( 1.0f + TOUCH_ORBIT_ACCEL * speed2 );
 if( k > TOUCH_ORBIT_GAIN_MAX )
  k = TOUCH_ORBIT_GAIN_MAX;
 float yaw = gEngfuncs.pfnGetCvarFloat( "cam_idealyaw" ) - dx * syaw * k;
 float pitch = gEngfuncs.pfnGetCvarFloat( "cam_idealpitch" ) + dy * spitch * k;
 if( pitch > 90.0f ) pitch = 90.0f;
 if( pitch < -90.0f ) pitch = -90.0f;
 gEngfuncs.Cvar_SetValue( "cam_idealyaw", yaw );
 gEngfuncs.Cvar_SetValue( "cam_idealpitch", pitch );
 return 1;
}
void CHudTouchOrbit::Cancel( void )
{
 if( m_session )
 {
  // Revert to the true original framing: straight-behind view
  // (cam_idealyaw 0, its default); pitch returns to the value it had
  // when the drag started.
  gEngfuncs.Cvar_SetValue( "cam_idealyaw", 0.0f );
  gEngfuncs.Cvar_SetValue( "cam_idealpitch", m_basePitch );
  gEngfuncs.Cvar_SetValue( "cam_idealdist", m_baseDist );
  gEngfuncs.Cvar_SetValue( "cam_zoffset", m_baseOffsetZ );
 }
 m_session = false;
 m_locked = false;
 m_restoreAt = 0.0f;
 m_finger = -1;
 m_lastTap = -1.0f;
 m_lookFingers[0] = m_lookFingers[1] = -1;
}

// Top-left weapon pickup notifier: green "weapon_<name>" text rows, max 3
// visible. Overflow scrolls down fading out while fresh rows slide in from
// the top with a fade-in. Console text has no per-glyph alpha, so fading
// is done by scaling the green toward black (same trick as the
// bottom-right history sprites).
#define PICKUP_NOTIFY_X 10
#define PICKUP_NOTIFY_Y 10
#define PICKUP_NOTIFY_LIFE 4.0f
#define PICKUP_NOTIFY_FADEIN 0.25f
#define PICKUP_NOTIFY_FADEOUT 1.0f
#define PICKUP_NOTIFY_SCROLL_PXSEC 240.0f
#define PICKUP_NOTIFY_PUSHOUT_FADE 0.5f

int CHudPickupNotify::Init( void )
{
 gHUD.AddHudElem( this );
 CHudBase::m_iFlags |= HUD_ACTIVE;
 m_count = 0;
 for( int i = 0; i < MAX_NOTIFY; i++ )
  m_items[i].iId = 0;
 return 1;
}
int CHudPickupNotify::VidInit( void )
{
 m_count = 0;
 for( int i = 0; i < MAX_NOTIFY; i++ )
  m_items[i].iId = 0;
 return 1;
}
void CHudPickupNotify::OnWeaponPickup( int iId )
{
 // GetWeapon does no bounds check (bare rgWeapons[] index), so clamp here.
 if( iId <= 0 || iId >= MAX_HUD_WEAPONS )
  return;
 float now = gHUD.m_flTime;
 // Same id twice in a row (server double-send): refresh, don't duplicate.
 for( int i = 0; i < m_count; i++ )
 {
  if( m_items[i].iId == iId )
  {
   m_items[i].birth = now;
   m_items[i].expire = now + PICKUP_NOTIFY_LIFE;
   m_items[i].pushed = 0.0f;
   return;
  }
 }
 if( m_count >= MAX_NOTIFY )
 {
  // Full (3 visible + 1 scrolling out): drop the oldest outright.
  m_count = MAX_NOTIFY - 1;
 }
 for( int i = m_count; i > 0; i-- )
  m_items[i] = m_items[i - 1];
 m_items[0].iId = iId;
 m_items[0].birth = now;
 m_items[0].expire = now + PICKUP_NOTIFY_LIFE;
 m_items[0].y = 0.0f;
 m_items[0].pushed = 0.0f;
 m_items[0].placed = false;
 m_count++;
}
int CHudPickupNotify::Draw( float flTime )
{
 (void)flTime;
 // Same gating as the ammo HUD that feeds us: hidden with the weapons.
 if( gHUD.m_iHideHUDDisplay & ( HIDEHUD_WEAPONS | HIDEHUD_ALL ) )
  return 1;
 float now = gHUD.m_flTime;
 int lineH = gHUD.m_iFontHeight + 4;
 if( lineH < 8 )
  lineH = 18;
 float dt = gHUD.m_flTimeDelta;
 if( dt < 0.0f )
  dt = 0.0f;
 if( dt > 0.25f )
  dt = 0.25f; // tab-away hitch: slide, don't teleport

 // Animate + expire. Names resolve lazily here (not on pickup):
 // WeaponList may arrive after WeapPickup, so the record might not exist
 // on the pickup frame yet.
 for( int i = 0; i < m_count; )
 {
  NotifyItem *it = &m_items[i];
  float targetY = (float)( PICKUP_NOTIFY_Y + i * lineH );
  if( !it->placed )
  {
   // Fresh rows enter from one line above, sliding down with fade-in.
   it->y = targetY - (float)lineH;
   it->placed = true;
  }
  else
  {
   float step = PICKUP_NOTIFY_SCROLL_PXSEC * dt;
   if( it->y < targetY )
    it->y = Q_min( it->y + step, targetY );
   else if( it->y > targetY )
    it->y = Q_max( it->y - step, targetY );
  }
  if( i >= MAXVIS_NOTIFY && it->pushed <= 0.0f )
   it->pushed = now; // scrolled past the window: fast fade starts
  float age = now - it->birth;
  float a = 1.0f;
  if( age < PICKUP_NOTIFY_FADEIN )
   a = age / PICKUP_NOTIFY_FADEIN;
  float remain = it->expire - now;
  if( remain < PICKUP_NOTIFY_FADEOUT )
   a = Q_min( a, remain / PICKUP_NOTIFY_FADEOUT );
  if( it->pushed > 0.0f )
   a = Q_min( a, 1.0f - ( now - it->pushed ) / PICKUP_NOTIFY_PUSHOUT_FADE );
  it->alpha = a;
  if( a <= 0.0f )
  {
   for( int k = i; k < m_count - 1; k++ )
    m_items[k] = m_items[k + 1];
   m_count--;
   continue;
  }
  i++;
 }

 // Never paint over the open weapon menu (same top-left corner):
 // entries keep aging underneath and expire on their own.
 if( gpActiveSel )
  return 1;

 for( int i = 0; i < m_count; i++ )
 {
  NotifyItem *it = &m_items[i];
  if( it->alpha <= 0.0f )
   continue;
  WEAPON *p = gWR.GetWeapon( it->iId );
  if( !p || !p->iId )
  {
   if( now - it->birth > 1.0f )
   {
    // Weapon record never arrived: drop silently, no ghost row.
    for( int k = i; k < m_count - 1; k++ )
     m_items[k] = m_items[k + 1];
    m_count--;
    i--;
   }
   continue;
  }
  int g = (int)( 255.0f * it->alpha );
  if( g < 0 )
   g = 0;
  if( g > 255 )
   g = 255;
  gHUD.DrawString( PICKUP_NOTIFY_X, (int)it->y, ScreenWidth, p->szName, 0, g, 0 );
 }
 return 1;
}

void SvenUI_Shutdown()
{
 SvenUI_Reset();
#if USE_VGUI
 cameraPanel=NULL;
 for(int i=0;i<31;++i) menus[i]=NULL;
#endif
}

void SvenUI_VidInit()
{
#if USE_VGUI
 if(cameraPanel) cameraPanel->setSize(ScreenWidth,ScreenHeight);
 if(camera.active && gViewPort) { gViewPort->ShowVGUIMenu(25); SvenUI_RefreshCursor(); }
#endif
}
