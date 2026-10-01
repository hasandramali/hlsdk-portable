#pragma once
void SvenUI_Init();
void SvenUI_Reset(bool preserveMaps = false);
void SvenUI_CloseKeyboard();
void SvenUI_Shutdown();
void SvenUI_VidInit();
bool SvenUI_CameraActive();
bool SvenUI_Capturing();
void SvenUI_Capture(bool enabled);
void SvenUI_ExitCamera();
int SvenUI_Key(int down,int key);
int SvenUI_InvRemove(const char *,int,void *);
int SvenUI_MapList(const char *,int,void *);
#if USE_VGUI
class CMenuPanel;
CMenuPanel *SvenUI_CreateMenu(int id);
void SvenUI_RefreshCursor();
#endif
