//---------------------------------------------------------------------------

#include <windows.h>
#include <ctype.h>
#include <math.h>
#include "luafar.h"
#include "reg.h"
#include "util.h"
#include "ustring.h"
#include "version.h"

typedef struct PluginStartupInfo PSInfo;

extern int luaopen_bit (lua_State *L);
extern int luaopen_uio (lua_State *L);
extern int luaopen_unicode (lua_State *L);
extern int luaopen_upackage (lua_State *L);
int  far_Find (lua_State*);
int  far_Gmatch (lua_State*);
int  far_Gsub (lua_State*);
int  far_Match (lua_State*);
int  far_Regex (lua_State*);
int  luaB_loadfileW (lua_State *L);
int  luaopen_regex (lua_State*);
int  pcall_msg (lua_State* L, int narg, int nret);
void push_flags_table (lua_State *L);

#define DIM(buff) (sizeof(buff)/sizeof(buff[0]))
#define OptHandle(L,i) ((HANDLE)luaL_optinteger (L,i,(INT_PTR)INVALID_HANDLE_VALUE))

const char FarFileFilterType[] = "FarFileFilter";
const char FarTimerType[]      = "FarTimer";
const char FarDialogType[]     = "FarDialog";
const char FAR_KEYINFO[]       = "far.info";
const char FAR_VIRTUALKEYS[]   = "far.virtualkeys";

//-------------------------------------------------------------
// [Must have this global due to DlgProc limitations].
// *  Use: .StructSize (as initialization indicator) and
//         the fields containing FAR service functions.
// *  Don't use: .ModuleName and other plugin-specific fields.
//-------------------------------------------------------------
struct PluginStartupInfo gInfo;   // "global Info"
struct FarStandardFunctions gFSF; // "global FSF"

const char* VirtualKeyStrings[256] = {
  // 0x00
  NULL, "LBUTTON", "RBUTTON", "CANCEL",
  "MBUTTON", "XBUTTON1", "XBUTTON2", NULL,
  "BACK", "TAB", NULL, NULL,
  "CLEAR", "RETURN", NULL, NULL,
  // 0x10
  "SHIFT", "CONTROL", "MENU", "PAUSE",
  "CAPITAL", "KANA", NULL, "JUNJA",
  "FINAL", "HANJA", NULL, "ESCAPE",
  NULL, "NONCONVERT", "ACCEPT", "MODECHANGE",
  // 0x20
  "SPACE", "PRIOR", "NEXT", "END",
  "HOME", "LEFT", "UP", "RIGHT",
  "DOWN", "SELECT", "PRINT", "EXECUTE",
  "SNAPSHOT", "INSERT", "DELETE", "HELP",
  // 0x30
  "0", "1", "2", "3",
  "4", "5", "6", "7",
  "8", "9", NULL, NULL,
  NULL, NULL, NULL, NULL,
  // 0x40
  NULL, "A", "B", "C",
  "D", "E", "F", "G",
  "H", "I", "J", "K",
  "L", "M", "N", "O",
  // 0x50
  "P", "Q", "R", "S",
  "T", "U", "V", "W",
  "X", "Y", "Z", "LWIN",
  "RWIN", "APPS", NULL, "SLEEP",
  // 0x60
  "NUMPAD0", "NUMPAD1", "NUMPAD2", "NUMPAD3",
  "NUMPAD4", "NUMPAD5", "NUMPAD6", "NUMPAD7",
  "NUMPAD8", "NUMPAD9", "MULTIPLY", "ADD",
  "SEPARATOR", "SUBTRACT", "DECIMAL", "DIVIDE",
  // 0x70
  "F1", "F2", "F3", "F4",
  "F5", "F6", "F7", "F8",
  "F9", "F10", "F11", "F12",
  "F13", "F14", "F15", "F16",
  // 0x80
  "F17", "F18", "F19", "F20",
  "F21", "F22", "F23", "F24",
  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL,
  // 0x90
  "NUMLOCK", "SCROLL", "OEM_NEC_EQUAL", "OEM_FJ_MASSHOU",
  "OEM_FJ_TOUROKU", "OEM_FJ_LOYA", "OEM_FJ_ROYA", NULL,
  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL,
  // 0xA0
  "LSHIFT", "RSHIFT", "LCONTROL", "RCONTROL",
  "LMENU", "RMENU", "BROWSER_BACK", "BROWSER_FORWARD",
  "BROWSER_REFRESH", "BROWSER_STOP", "BROWSER_SEARCH", "BROWSER_FAVORITES",
  "BROWSER_HOME", "VOLUME_MUTE", "VOLUME_DOWN", "VOLUME_UP",
  // 0xB0
  "MEDIA_NEXT_TRACK", "MEDIA_PREV_TRACK", "MEDIA_STOP", "MEDIA_PLAY_PAUSE",
  "LAUNCH_MAIL", "LAUNCH_MEDIA_SELECT", "LAUNCH_APP1", "LAUNCH_APP2",
  NULL, NULL, "OEM_1", "OEM_PLUS",
  "OEM_COMMA", "OEM_MINUS", "OEM_PERIOD", "OEM_2",
  // 0xC0
  "OEM_3", NULL, NULL, NULL,
  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL,
  // 0xD0
  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, "OEM_4",
  "OEM_5", "OEM_6", "OEM_7", "OEM_8",
  // 0xE0
  NULL, NULL, "OEM_102", NULL,
  NULL, "PROCESSKEY", NULL, "PACKET",
  NULL, "OEM_RESET", "OEM_JUMP", "OEM_PA1",
  "OEM_PA2", "OEM_PA3", "OEM_WSCTRL", NULL,
  // 0xF0
  NULL, NULL, NULL, NULL,
  NULL, NULL, "ATTN", "CRSEL",
  "EXSEL", "EREOF", "PLAY", "ZOOM",
  "NONAME", "PA1", "OEM_CLEAR", NULL,
};

const wchar_t* GetMsg (PSInfo *Info, int MsgId)
{
  if (MsgId >= 0) // (MsgId < 0) crashes FAR
    return Info->GetMsg (Info->ModuleNumber, MsgId);
  return NULL;
}

BOOL get_env_flag (lua_State *L, int stack_pos, int *trg)
{
  *trg = 0;
  int type = lua_type (L, stack_pos);
  if (type == LUA_TNUMBER) {
    *trg = lua_tointeger (L, stack_pos);
    return TRUE;
  }
  else if (type == LUA_TNONE || type == LUA_TNIL)
    return TRUE;
  if (type == LUA_TSTRING) {
    lua_getfield (L, LUA_ENVIRONINDEX, lua_tostring(L, stack_pos));
    if (lua_isnumber(L, -1)) {
      *trg = lua_tointeger (L, -1);
      lua_pop (L, 1);
      return TRUE;
    }
    lua_pop (L, 1);
  }
  return FALSE;
}

int check_env_flag (lua_State *L, int stack_pos)
{
  int trg;
  if(!get_env_flag (L, stack_pos, &trg))
    luaL_argerror(L, stack_pos, "invalid flag");
  return trg;
}

BOOL GetFlagCombination (lua_State *L, int stack_pos, int *trg)
{
  *trg = 0;
  int type = lua_type (L, stack_pos);
  if (type == LUA_TNUMBER) {
    *trg = lua_tointeger (L, stack_pos);
    return TRUE;
  }
  if (type == LUA_TNONE || type == LUA_TNIL)
    return TRUE;
  if (type == LUA_TSTRING)
    return get_env_flag (L, stack_pos, trg);
  if (type == LUA_TTABLE) {
    stack_pos = abs_index (L, stack_pos);
    lua_pushnil(L);
    while (lua_next(L, stack_pos)) {
      if (lua_type(L,-2)==LUA_TSTRING && lua_toboolean(L,-1)) {
        int flag;
        if (get_env_flag (L, -2, &flag))
          *trg |= flag;
        else
          { lua_pop(L,2); return FALSE; }
      }
      lua_pop(L, 1);
    }
    return TRUE;
  }
  return FALSE;
}

int CheckFlags(lua_State* L, int stackpos)
{
  int Flags;
  if (!GetFlagCombination (L, stackpos, &Flags))
    luaL_error(L, "invalid flag combination");
  return Flags;
}

int far_GetFlags (lua_State *L)
{
  lua_pushvalue (L, LUA_ENVIRONINDEX);
  return 1;
}

int far_GetFileOwner (lua_State *L)
{
  wchar_t Owner[512];
  const wchar_t *Computer = opt_utf8_string (L, 1, NULL);
  const wchar_t *Name = check_utf8_string (L, 2, NULL);
  if (gFSF.GetFileOwner (Computer, Name, Owner, DIM(Owner))) {
    push_utf8_string(L, Owner, -1);
    return 1;
  }
  return 0;
}

int far_GetNumberOfLinks (lua_State *L)
{
  const wchar_t *Name = check_utf8_string (L, 1, NULL);
  int num = gFSF.GetNumberOfLinks (Name);
  return lua_pushinteger (L, num), 1;
}

int far_LuafarVersion (lua_State *L)
{
  if (lua_toboolean(L, 1)) {
    lua_pushinteger(L, VER_MAJOR);
    lua_pushinteger(L, VER_MINOR);
    lua_pushinteger(L, VER_MICRO);
    return 3;
  }
  lua_pushliteral(L, VER_STRING);
  return 1;
}

void GetMouseEvent(lua_State *L, MOUSE_EVENT_RECORD* rec)
{
  rec->dwMousePosition.X = GetOptIntFromTable(L, "dwMousePositionX", 0);
  rec->dwMousePosition.Y = GetOptIntFromTable(L, "dwMousePositionY", 0);
  rec->dwButtonState = GetOptIntFromTable(L, "dwButtonState", 0);
  rec->dwControlKeyState = GetOptIntFromTable(L, "dwControlKeyState", 0);
  rec->dwEventFlags = GetOptIntFromTable(L, "dwEventFlags", 0);
}

void PutMouseEvent(lua_State *L, const MOUSE_EVENT_RECORD* rec, BOOL table_exist)
{
  if (!table_exist)
    lua_createtable(L, 0, 5);
  PutNumToTable(L, "dwMousePositionX", rec->dwMousePosition.X);
  PutNumToTable(L, "dwMousePositionY", rec->dwMousePosition.Y);
  PutNumToTable(L, "dwButtonState", rec->dwButtonState);
  PutNumToTable(L, "dwControlKeyState", rec->dwControlKeyState);
  PutNumToTable(L, "dwEventFlags", rec->dwEventFlags);
}

// convert a string from utf-8 to wide char and put it into a table,
// to prevent stack overflow and garbage collection
const wchar_t* StoreTempString(lua_State *L, int store_stack_pos, int* index)
{
  const wchar_t *s = check_utf8_string(L,-1,NULL);
  lua_rawseti(L, store_stack_pos, ++(*index));
  return s;
}

void PushEditorSetPosition(lua_State *L, const struct EditorSetPosition *esp)
{
  lua_createtable(L, 0, 6);
  PutIntToTable(L, "CurLine",       esp->CurLine);
  PutIntToTable(L, "CurPos",        esp->CurPos);
  PutIntToTable(L, "CurTabPos",     esp->CurTabPos);
  PutIntToTable(L, "TopScreenLine", esp->TopScreenLine);
  PutIntToTable(L, "LeftPos",       esp->LeftPos);
  PutIntToTable(L, "Overtype",      esp->Overtype);
}

void FillEditorSetPosition(lua_State *L, struct EditorSetPosition *esp)
{
  esp->CurLine   = GetOptIntFromTable(L, "CurLine", -1);
  esp->CurPos    = GetOptIntFromTable(L, "CurPos", -1);
  esp->CurTabPos = GetOptIntFromTable(L, "CurTabPos", -1);
  esp->TopScreenLine = GetOptIntFromTable(L, "TopScreenLine", -1);
  esp->LeftPos   = GetOptIntFromTable(L, "LeftPos", -1);
  esp->Overtype  = GetOptIntFromTable(L, "Overtype", -1);
}

void PushFarFindData(lua_State *L, const struct FAR_FIND_DATA *wfd)
{
  lua_createtable(L, 0, 8);
  PutAttrToTable     (L,                       wfd->dwFileAttributes);
  PutNumToTable      (L, "FileSize",           (double)wfd->nFileSize);
  PutFileTimeToTable (L, "LastWriteTime",      wfd->ftLastWriteTime);
  PutFileTimeToTable (L, "LastAccessTime",     wfd->ftLastAccessTime);
  PutFileTimeToTable (L, "CreationTime",       wfd->ftCreationTime);
  PutWStrToTable     (L, "FileName",           wfd->lpwszFileName, -1);
  PutWStrToTable     (L, "AlternateFileName",  wfd->lpwszAlternateFileName, -1);
  PutNumToTable      (L, "PackSize",           (double)wfd->nPackSize);
}

// on entry : the table's on the stack top
// on exit  : 2 strings added to the stack top (don't pop them!)
void GetFarFindData(lua_State *L, struct FAR_FIND_DATA *wfd)
{
  memset(wfd, 0, sizeof(*wfd));

  wfd->dwFileAttributes = GetAttrFromTable(L);
  wfd->nFileSize = GetFileSizeFromTable(L, "FileSize");
  wfd->nPackSize = GetFileSizeFromTable(L, "PackSize");
  wfd->ftLastWriteTime  = GetFileTimeFromTable(L, "LastWriteTime");
  wfd->ftLastAccessTime = GetFileTimeFromTable(L, "LastAccessTime");
  wfd->ftCreationTime   = GetFileTimeFromTable(L, "CreationTime");

  lua_getfield(L, -1, "FileName"); // +1
  wfd->lpwszFileName = opt_utf8_string(L, -1, L""); // +1

  lua_getfield(L, -2, "AlternateFileName"); // +2
  wfd->lpwszAlternateFileName = opt_utf8_string(L, -1, L""); // +2
}
//---------------------------------------------------------------------------

void PushWinFindData (lua_State *L, const WIN32_FIND_DATAW *FData)
{
  lua_createtable(L, 0, 7);
  PutAttrToTable(L,                          FData->dwFileAttributes);
  PutNumToTable(L, "FileSize", FData->nFileSizeLow + 65536.*65536.*FData->nFileSizeHigh);
  PutFileTimeToTable(L, "LastWriteTime",     FData->ftLastWriteTime);
  PutFileTimeToTable(L, "LastAccessTime",    FData->ftLastAccessTime);
  PutFileTimeToTable(L, "CreationTime",      FData->ftCreationTime);
  PutWStrToTable(L, "FileName",              FData->cFileName, -1);
  PutWStrToTable(L, "AlternateFileName",     FData->cAlternateFileName, -1);
}

void PushPanelItem(lua_State *L, const struct PluginPanelItem *PanelItem)
{
  lua_newtable(L); // "PanelItem"
  //-----------------------------------------------------------------------
  PushFarFindData (L, &PanelItem->FindData);
  lua_setfield(L, -2, "FindData");
  //-----------------------------------------------------------------------
  PutNumToTable(L, "Flags", PanelItem->Flags);
  PutNumToTable(L, "NumberOfLinks", PanelItem->NumberOfLinks);
  if (PanelItem->Description)
    PutWStrToTable(L, "Description",  PanelItem->Description, -1);
  if (PanelItem->Owner)
    PutWStrToTable(L, "Owner",  PanelItem->Owner, -1);
  //-----------------------------------------------------------------------
  /* not clear why custom columns are defined on per-file basis */
  if (PanelItem->CustomColumnNumber > 0) {
    int j;
    lua_createtable (L, PanelItem->CustomColumnNumber, 0);
    for(j=0; j < PanelItem->CustomColumnNumber; j++)
      PutWStrToArray(L, j+1, PanelItem->CustomColumnData[j], -1);
    lua_setfield(L, -2, "CustomColumnData");
  }
  //-----------------------------------------------------------------------
  PutNumToTable(L, "UserData", PanelItem->UserData);
  //-----------------------------------------------------------------------
  /* skip PanelItem->Reserved for now */
  //-----------------------------------------------------------------------
}
//---------------------------------------------------------------------------

void PushPanelItems(lua_State *L, const struct PluginPanelItem *PanelItems, int ItemsNumber)
{
  int i;
  lua_createtable(L, ItemsNumber, 0); // "PanelItems"
  for(i=0; i < ItemsNumber; i++) {
    PushPanelItem (L, PanelItems + i);
    lua_rawseti(L, -2, i+1);
  }
}
//---------------------------------------------------------------------------

PSInfo* GetPluginStartupInfo(lua_State* L)
{
  lua_getfield(L, LUA_REGISTRYINDEX, FAR_KEYINFO);
  PSInfo* p = (PSInfo*) lua_touserdata(L, -1);
  if (p)
    lua_pop(L, 1);
  else
    luaL_error (L, "PluginStartupInfo is not available.");
  return p;
}

int far_PluginStartupInfo(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_createtable(L, 0, 3);
  PutWStrToTable(L, "ModuleName", Info->ModuleName, -1);
  PutNumToTable(L, "ModuleNumber", Info->ModuleNumber);
  PutWStrToTable(L, "RootKey", Info->RootKey, -1);
  return 1;
}

int far_GetCurrentDirectory (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int size = Info->FSF->GetCurrentDirectory(0, NULL);
  wchar_t* buf = (wchar_t*)lua_newuserdata(L, size * sizeof(wchar_t));
  Info->FSF->GetCurrentDirectory(size, buf);
  push_utf8_string(L, buf, -1);
  return 1;
}

int push_editor_filename(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int size = Info->EditorControl(ECTL_GETFILENAME, 0);
  if (!size) return 0;

  wchar_t* fname = (wchar_t*)lua_newuserdata(L, size * sizeof(wchar_t));
  if (Info->EditorControl(ECTL_GETFILENAME, fname)) {
    push_utf8_string(L, fname, -1);
    lua_remove(L, -2);
    return 1;
  }
  lua_pop(L,1);
  return 0;
}

int far_EditorGetFileName(lua_State *L) {
  if (!push_editor_filename(L)) lua_pushnil(L);
  return 1;
}

int far_EditorGetInfo(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorInfo ei;
  if (!Info->EditorControl(ECTL_GETINFO, &ei))
    return lua_pushnil(L), 1;
  lua_createtable(L, 0, 18);
  PutNumToTable(L, "EditorID", ei.EditorID);

  if (push_editor_filename(L))
    lua_setfield(L, -2, "FileName");

  PutNumToTable(L, "WindowSizeX", ei.WindowSizeX);
  PutNumToTable(L, "WindowSizeY", ei.WindowSizeY);
  PutNumToTable(L, "TotalLines", ei.TotalLines);
  PutNumToTable(L, "CurLine", ei.CurLine);
  PutNumToTable(L, "CurPos", ei.CurPos);
  PutNumToTable(L, "CurTabPos", ei.CurTabPos);
  PutNumToTable(L, "TopScreenLine", ei.TopScreenLine);
  PutNumToTable(L, "LeftPos", ei.LeftPos);
  PutBoolToTable(L, "Overtype", ei.Overtype);
  PutNumToTable(L, "BlockType", ei.BlockType);
  PutNumToTable(L, "BlockStartLine", ei.BlockStartLine);
  PutNumToTable(L, "Options", ei.Options);
  PutNumToTable(L, "TabSize", ei.TabSize);
  PutNumToTable(L, "BookMarkCount", ei.BookMarkCount);
  PutNumToTable(L, "CurState", ei.CurState);
  PutNumToTable(L, "CodePage", ei.CodePage);
  return 1;
}

/* t-rex:
 * ��� ��� ���� ����� ������� ��������:
 * �������� � ���� ��� ���� ������� ������, ��������� �� ������� ������
 * ���������� ������ ��� ECTL_SETPOSITION, ��� ������������� ����� ������
 * ECTL_* ��� ������� ����� �������� ����� ������ ���� ���� ����� �� -1
 * (�.�. �������� ������) �� ��� ������ ����� ��� ������ � ������ (� ���
 * �������� ������ �������), ������� ���� ���� ������ ��������� ECTL_*
 * (��� ����� ����� ��� �������� �� ������������������ �����
 * i,i+1,i+2,...) �� ����� ������ ECTL_* ���� ������ ECTL_SETPOSITION �
 * ���� ECTL_* �������� � -1.
 */
BOOL FastGetString (PSInfo *Info, struct EditorGetString *egs,
  int string_num)
{
  struct EditorSetPosition esp;
  esp.CurLine   = string_num;
  esp.CurPos    = -1;
  esp.CurTabPos = -1;
  esp.TopScreenLine = -1;
  esp.LeftPos   = -1;
  esp.Overtype  = -1;
  if(!Info->EditorControl(ECTL_SETPOSITION, &esp))
    return FALSE;

  egs->StringNumber = string_num;
  return Info->EditorControl(ECTL_GETSTRING, egs);
}

// LineInfo = EditorGetString (line_num, [fast])
//   line_num:  number of line in the Editor, 0-based; a number
//   fast:      0 = normal;
//              1 = much faster, but changes current position;
//              2 = the fastest: as 1 but returns StringText only;
//   LineInfo:  a table
int far_EditorGetString(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int line_num = luaL_optinteger(L, 1, -1);
  int fast     = luaL_optinteger(L, 2, 0);
  BOOL res;
  struct EditorGetString egs;

  if (fast == 1 || fast == 2)
    res = FastGetString(Info, &egs, line_num);
  else {
    egs.StringNumber = line_num;
    res = Info->EditorControl(ECTL_GETSTRING, &egs);
  }
  if (res) {
    if (fast == 2)
      push_utf8_string (L, egs.StringText, egs.StringLength);
    else {
      lua_createtable(L, 0, 6);
      PutNumToTable (L, "StringNumber", egs.StringNumber);
      PutWStrToTable (L, "StringText",  egs.StringText, egs.StringLength);
      PutWStrToTable (L, "StringEOL",   egs.StringEOL, -1);
      PutNumToTable (L, "StringLength", egs.StringLength);
      PutNumToTable (L, "SelStart",     egs.SelStart);
      PutNumToTable (L, "SelEnd",       egs.SelEnd);
    }
    return 1;
  }
  return 0;
}

int far_EditorSetString(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorSetString ess;
  ess.StringNumber = luaL_optinteger(L, 1, -1);
  ess.StringText = check_utf8_string(L, 2, &ess.StringLength);
  ess.StringEOL = opt_utf8_string(L, 3, NULL);
  if (Info->EditorControl(ECTL_SETSTRING, &ess))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorInsertString(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int indent = lua_toboolean(L, 1);
  if (Info->EditorControl(ECTL_INSERTSTRING, &indent))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorDeleteString(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  if (Info->EditorControl(ECTL_DELETESTRING, NULL))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorInsertText(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t* text = check_utf8_string(L,1,NULL);
  lua_pushboolean(L, Info->EditorControl(ECTL_INSERTTEXT, (wchar_t*)text));
  return 1;
}

int far_EditorDeleteChar(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  if (Info->EditorControl(ECTL_DELETECHAR, NULL))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorDeleteBlock(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  if (Info->EditorControl(ECTL_DELETEBLOCK, NULL))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorUndoRedo(lua_State *L)
{
  struct EditorUndoRedo eur;
  memset(&eur, 0, sizeof(eur));
  eur.Command = check_env_flag(L, 1);
  PSInfo *Info = GetPluginStartupInfo(L);
  return lua_pushboolean (L, Info->EditorControl(ECTL_UNDOREDO, &eur)), 1;
}

int SetKeyBar(lua_State *L, BOOL editor)
{
  void* param;
  struct KeyBarTitles kbt;
  PSInfo *Info = GetPluginStartupInfo(L);

  enum { REDRAW=-1, RESTORE=0 }; // corresponds to FAR API
  BOOL argfail = FALSE;
  if (lua_isstring(L,1)) {
    const char* p = lua_tostring(L,1);
    if (0 == strcmp("redraw", p)) param = (void*)REDRAW;
    else if (0 == strcmp("restore", p)) param = (void*)RESTORE;
    else argfail = TRUE;
  }
  else if (lua_istable(L,1)) {
    param = &kbt;
    memset(&kbt, 0, sizeof(kbt));
    struct { const char* key; wchar_t** trg; } pairs[] = {
      {"Titles",          kbt.Titles},
      {"CtrlTitles",      kbt.CtrlTitles},
      {"AltTitles",       kbt.AltTitles},
      {"ShiftTitles",     kbt.ShiftTitles},
      {"CtrlShiftTitles", kbt.CtrlShiftTitles},
      {"AltShiftTitles",  kbt.AltShiftTitles},
      {"CtrlAltTitles",   kbt.CtrlAltTitles},
    };
    lua_settop(L, 1);
    lua_newtable(L);
    int store = 0;
    size_t i;
    int j;
    for (i=0; i < sizeof(pairs)/sizeof(pairs[0]); i++) {
      lua_getfield (L, 1, pairs[i].key);
      if (lua_istable (L, -1)) {
        for (j=0; j<12; j++) {
          lua_pushinteger(L,j+1);
          lua_gettable(L,-2);
          if (lua_isstring(L,-1))
            pairs[i].trg[j] = (wchar_t*)StoreTempString(L, 2, &store);
          else
            lua_pop(L,1);
        }
      }
      lua_pop (L, 1);
    }
  }
  else
    argfail = TRUE;
  if (argfail)
    return luaL_argerror(L, 1, "must be 'redraw', 'restore', or table");

  int result = editor ? Info->EditorControl(ECTL_SETKEYBAR, param) :
                        Info->ViewerControl(VCTL_SETKEYBAR, param);
  if (result)
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorSetKeyBar(lua_State *L)
{
  return SetKeyBar(L, TRUE);
}

int far_ViewerSetKeyBar(lua_State *L)
{
  return SetKeyBar(L, FALSE);
}

int far_EditorSetParam(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorSetParameter esp;
  memset(&esp, 0, sizeof(esp));
  wchar_t buf[256];
  esp.Type = check_env_flag(L,1);
  //-----------------------------------------------------
  int tp = lua_type(L,2);
  if (tp == LUA_TNUMBER)
    esp.Param.iParam = lua_tointeger(L,2);
  else if (tp == LUA_TBOOLEAN)
    esp.Param.iParam = lua_toboolean(L,2);
  else if (tp == LUA_TSTRING)
    esp.Param.wszParam = (wchar_t*)check_utf8_string(L,2,NULL);
  //-----------------------------------------------------
  if(esp.Type == ESPT_GETWORDDIV) {
    esp.Param.wszParam = buf;
    esp.Size = DIM(buf);
  }
  //-----------------------------------------------------
  int f;
  GetFlagCombination (L, 3, &f);
  esp.Flags = f;
  //-----------------------------------------------------
  int result = Info->EditorControl(ECTL_SETPARAM, &esp);
  lua_pushboolean(L, result);
  if(result && esp.Type == ESPT_GETWORDDIV) {
    push_utf8_string(L,buf,-1); return 2;
  }
  return 1;
}

int far_EditorSetPosition(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorSetPosition esp;
  if (lua_istable(L, 1)) {
    lua_settop(L, 1);
    FillEditorSetPosition(L, &esp);
  }
  else {
    esp.CurLine   = luaL_optinteger(L, 1, -1);
    esp.CurPos    = luaL_optinteger(L, 2, -1);
    esp.CurTabPos = luaL_optinteger(L, 3, -1);
    esp.TopScreenLine = luaL_optinteger(L, 4, -1);
    esp.LeftPos   = luaL_optinteger(L, 5, -1);
    esp.Overtype  = luaL_optinteger(L, 6, -1);
  }
  if (Info->EditorControl(ECTL_SETPOSITION, &esp) != 0)
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorRedraw(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  if (Info->EditorControl(ECTL_REDRAW, NULL))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorExpandTabs(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int line_num = luaL_optinteger(L, 1, -1);
  if (Info->EditorControl(ECTL_EXPANDTABS, &line_num))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int PushBookmarks(lua_State *L, int count, int command)
{
  if (count <= 0)
    return 0;

  struct EditorBookMarks ebm;
  ebm.Line = (long*)lua_newuserdata(L, 4 * count * sizeof(long));
  ebm.Cursor     = ebm.Line + count;
  ebm.ScreenLine = ebm.Cursor + count;
  ebm.LeftPos    = ebm.ScreenLine + count;
  PSInfo *Info = GetPluginStartupInfo(L);
  if (!Info->EditorControl(command, &ebm))
    return 0;

  int i;
  lua_createtable(L, count, 0);
  for (i=0; i < count; i++) {
    lua_pushinteger(L, i+1);
    lua_createtable(L, 0, 4);
    PutIntToTable (L, "Line", ebm.Line[i]);
    PutIntToTable (L, "Cursor", ebm.Cursor[i]);
    PutIntToTable (L, "ScreenLine", ebm.ScreenLine[i]);
    PutIntToTable (L, "LeftPos", ebm.LeftPos[i]);
    lua_rawset(L, -3);
  }
  return 1;
}

int far_EditorGetBookmarks(lua_State *L)
{
  struct EditorInfo ei;
  PSInfo *Info = GetPluginStartupInfo(L);
  if (!Info->EditorControl(ECTL_GETINFO, &ei))
    return 0;
  return PushBookmarks(L, ei.BookMarkCount, ECTL_GETBOOKMARKS);
}

int far_EditorGetStackBookmarks(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int count = Info->EditorControl(ECTL_GETSTACKBOOKMARKS, NULL);
  return PushBookmarks(L, count, ECTL_GETSTACKBOOKMARKS);
}

int far_EditorAddStackBookmark(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushboolean(L, Info->EditorControl(ECTL_ADDSTACKBOOKMARK, NULL));
  return 1;
}

int far_EditorClearStackBookmarks(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushinteger(L, Info->EditorControl(ECTL_CLEARSTACKBOOKMARKS, NULL));
  return 1;
}

int far_EditorDeleteStackBookmark(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  INT_PTR num = luaL_optinteger(L, 1, -1);
  lua_pushboolean(L, Info->EditorControl(ECTL_DELETESTACKBOOKMARK, (void*)num));
  return 1;
}

int far_EditorNextStackBookmark(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushboolean(L, Info->EditorControl(ECTL_NEXTSTACKBOOKMARK, NULL));
  return 1;
}

int far_EditorPrevStackBookmark(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushboolean(L, Info->EditorControl(ECTL_PREVSTACKBOOKMARK, NULL));
  return 1;
}

int far_EditorSetTitle(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t* text = opt_utf8_string(L, 1, NULL);
  if (Info->EditorControl(ECTL_SETTITLE, (wchar_t*)text))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorQuit(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  if (Info->EditorControl(ECTL_QUIT, NULL))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int SetEditorSelect(lua_State *L, int pos_table, struct EditorSelect *es)
{
  lua_getfield(L, pos_table, "BlockType");
  if (!get_env_flag(L, -1, &es->BlockType)) {
    lua_pop(L,1);
    return 0;
  }
  lua_pushvalue(L, pos_table);
  es->BlockStartLine = GetOptIntFromTable(L, "BlockStartLine", -1);
  es->BlockStartPos  = GetOptIntFromTable(L, "BlockStartPos", -1);
  es->BlockWidth     = GetOptIntFromTable(L, "BlockWidth", -1);
  es->BlockHeight    = GetOptIntFromTable(L, "BlockHeight", -1);
  lua_pop(L,2);
  return 1;
}

int far_EditorSelect(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorSelect es;
  if (lua_istable(L, 1)) {
    if (!SetEditorSelect(L, 1, &es))
      return 0;
  }
  else {
    if (!get_env_flag(L, 1, &es.BlockType))
      return 0;
    es.BlockStartLine = luaL_optinteger(L, 2, -1);
    es.BlockStartPos  = luaL_optinteger(L, 3, -1);
    es.BlockWidth     = luaL_optinteger(L, 4, -1);
    es.BlockHeight    = luaL_optinteger(L, 5, -1);
  }
  if (Info->EditorControl(ECTL_SELECT, &es))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

// This function is that long because FAR API does not supply needed
// information directly.
int far_EditorGetSelection(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorInfo EI;
  Info->EditorControl(ECTL_GETINFO, &EI);
  if (EI.BlockType == BTYPE_NONE)
    return lua_pushnil(L), 1;

  lua_createtable (L, 0, 5);
  PutIntToTable (L, "BlockType", EI.BlockType);
  PutIntToTable (L, "StartLine", EI.BlockStartLine);

  struct EditorGetString egs;
  if(!FastGetString(Info, &egs, EI.BlockStartLine))
    return lua_pushnil(L), 1;

  int BlockStartPos = egs.SelStart;
  PutIntToTable (L, "StartPos", BlockStartPos);

  // binary search for a non-block line
  int h = 100; // arbitrary small number
  int from = EI.BlockStartLine;
  int to;
  for (to = from+h; to < EI.TotalLines; to = from + (h*=2)) {
    if(!FastGetString(Info, &egs, to))
      return lua_pushnil(L), 1;
    if (egs.SelStart < 0 || egs.SelEnd == 0)
      break;
  }
  if (to >= EI.TotalLines)
    to = EI.TotalLines - 1;

  // binary search for the last block line
  while (from != to) {
    int curr = (from + to + 1) / 2;
    if(!FastGetString(Info, &egs, curr))
      return lua_pushnil(L), 1;
    if (egs.SelStart < 0 || egs.SelEnd == 0) {
      if (curr == to)
        break;
      to = curr;      // curr was not selected
    }
    else {
      from = curr;    // curr was selected
    }
  }

  if(!FastGetString(Info, &egs, from))
    return lua_pushnil(L), 1;

  PutIntToTable (L, "EndLine", from);
  PutIntToTable (L, "EndPos", egs.SelEnd);

  // restore current position, since FastGetString() changed it
  struct EditorSetPosition esp;
  esp.CurLine       = EI.CurLine;
  esp.CurPos        = EI.CurPos;
  esp.CurTabPos     = EI.CurTabPos;
  esp.TopScreenLine = EI.TopScreenLine;
  esp.LeftPos       = EI.LeftPos;
  esp.Overtype      = EI.Overtype;
  Info->EditorControl(ECTL_SETPOSITION, &esp);
  return 1;
}

int _EditorTabConvert(lua_State *L, int Operation)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorConvertPos ecp;
  ecp.StringNumber = luaL_optinteger(L, 1, -1);
  ecp.SrcPos = luaL_checkinteger(L, 2);
  if (Info->EditorControl(Operation, &ecp))
    return lua_pushinteger(L, ecp.DestPos), 1;
  return 0;
}

int far_EditorTabToReal(lua_State *L)
{
  return _EditorTabConvert(L, ECTL_TABTOREAL);
}

int far_EditorRealToTab(lua_State *L)
{
  return _EditorTabConvert(L, ECTL_REALTOTAB);
}

int far_EditorTurnOffMarkingBlock(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  Info->EditorControl(ECTL_TURNOFFMARKINGBLOCK, NULL);
  return 0;
}

int far_EditorAddColor(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorColor ec;
  ec.StringNumber = luaL_optinteger(L, 1, -1);
  ec.StartPos     = luaL_checkinteger(L, 2);
  ec.EndPos       = luaL_checkinteger(L, 3);
  ec.Color        = luaL_checkinteger(L, 4);
  ec.ColorItem    = 0;
  if (Info->EditorControl(ECTL_ADDCOLOR, &ec))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorGetColor(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorColor ec;
  ec.StringNumber = luaL_optinteger(L, 1, -1);
  ec.StartPos     = luaL_checkinteger(L, 2);
  ec.EndPos       = luaL_checkinteger(L, 3);
  ec.ColorItem    = luaL_checkinteger(L, 4);
  ec.Color        = 0;
  if (Info->EditorControl(ECTL_GETCOLOR, &ec))
    return lua_pushinteger(L, ec.Color), 1;
  return 0;
}

int far_EditorSaveFile(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct EditorSaveFile esf;
  esf.FileName = opt_utf8_string(L, 1, L"");
  esf.FileEOL = opt_utf8_string(L, 2, NULL);
  if (Info->EditorControl(ECTL_SAVEFILE, &esf))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorReadInput(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  INPUT_RECORD ir;
  if (!Info->EditorControl(ECTL_READINPUT, &ir))
    return 0;
  lua_newtable(L);
  switch(ir.EventType) {
    case KEY_EVENT:
      PutStrToTable(L, "EventType", "KEY_EVENT");
      PutBoolToTable(L,"bKeyDown", ir.Event.KeyEvent.bKeyDown);
      PutNumToTable(L, "wRepeatCount", ir.Event.KeyEvent.wRepeatCount);
      PutNumToTable(L, "wVirtualKeyCode", ir.Event.KeyEvent.wVirtualKeyCode);
      PutNumToTable(L, "wVirtualScanCode", ir.Event.KeyEvent.wVirtualScanCode);
      PutNumToTable(L, "UnicodeChar", ir.Event.KeyEvent.uChar.UnicodeChar);
      PutNumToTable(L, "AsciiChar", ir.Event.KeyEvent.uChar.AsciiChar);
      PutNumToTable(L, "dwControlKeyState", ir.Event.KeyEvent.dwControlKeyState);
      break;

    case MOUSE_EVENT:
      PutStrToTable(L, "EventType", "MOUSE_EVENT");
      PutMouseEvent(L, &ir.Event.MouseEvent, TRUE);
      break;

    case WINDOW_BUFFER_SIZE_EVENT:
      PutStrToTable(L, "EventType", "WINDOW_BUFFER_SIZE_EVENT");
      PutNumToTable(L, "dwSizeX", ir.Event.WindowBufferSizeEvent.dwSize.X);
      PutNumToTable(L, "dwSizeY", ir.Event.WindowBufferSizeEvent.dwSize.Y);
      break;

    case MENU_EVENT:
      PutStrToTable(L, "EventType", "MENU_EVENT");
      PutNumToTable(L, "dwCommandId", ir.Event.MenuEvent.dwCommandId);
      break;

    case FOCUS_EVENT:
      PutStrToTable(L, "EventType", "FOCUS_EVENT");
      PutBoolToTable(L,"bSetFocus", ir.Event.FocusEvent.bSetFocus);
      break;

    default:
      return 0;
  }
  return 1;
}

void FillInputRecord(lua_State *L, int pos, INPUT_RECORD *ir)
{
  pos = abs_index(L, pos);
  luaL_checktype(L, pos, LUA_TTABLE);
  memset(ir, 0, sizeof(INPUT_RECORD));

  BOOL hasKey;
  // determine event type
  lua_getfield(L, pos, "EventType");
  int temp;
  if(!get_env_flag(L, -1, &temp))
    luaL_argerror(L, pos, "EventType field is missing or invalid");
  lua_pop(L, 1);

  lua_pushvalue(L, pos);
  ir->EventType = temp;
  switch(ir->EventType) {
    case KEY_EVENT:
      ir->Event.KeyEvent.bKeyDown = GetOptBoolFromTable(L, "bKeyDown", FALSE);
      ir->Event.KeyEvent.wRepeatCount = GetOptIntFromTable(L, "wRepeatCount", 1);
      ir->Event.KeyEvent.wVirtualKeyCode = GetOptIntFromTable(L, "wVirtualKeyCode", 0);
      ir->Event.KeyEvent.wVirtualScanCode = GetOptIntFromTable(L, "wVirtualScanCode", 0);
      // prevent simultaneous setting of both UnicodeChar and AsciiChar
      lua_getfield(L, -1, "UnicodeChar");
      hasKey = !(lua_isnil(L, -1));
      lua_pop(L, 1);
      if(hasKey)
        ir->Event.KeyEvent.uChar.UnicodeChar = GetOptIntFromTable(L, "UnicodeChar", 0);
      else {
        ir->Event.KeyEvent.uChar.AsciiChar = GetOptIntFromTable(L, "AsciiChar", 0);
      }
      ir->Event.KeyEvent.dwControlKeyState = GetOptIntFromTable(L, "dwControlKeyState", 0);
      break;

    case MOUSE_EVENT:
      GetMouseEvent(L, &ir->Event.MouseEvent);
      break;

    case WINDOW_BUFFER_SIZE_EVENT:
      ir->Event.WindowBufferSizeEvent.dwSize.X = GetOptIntFromTable(L, "dwSizeX", 0);
      ir->Event.WindowBufferSizeEvent.dwSize.Y = GetOptIntFromTable(L, "dwSizeY", 0);
      break;

    case MENU_EVENT:
      ir->Event.MenuEvent.dwCommandId = GetOptIntFromTable(L, "dwCommandId", 0);
      break;

    case FOCUS_EVENT:
      ir->Event.FocusEvent.bSetFocus = GetOptBoolFromTable(L, "bSetFocus", FALSE);
      break;
  }
  lua_pop(L, 1);
}

int far_EditorProcessInput(lua_State *L)
{
  if (!lua_istable(L, 1))
    return 0;
  PSInfo *Info = GetPluginStartupInfo(L);
  INPUT_RECORD ir;
  FillInputRecord(L, 1, &ir);
  if (Info->EditorControl(ECTL_PROCESSINPUT, &ir))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_EditorProcessKey(lua_State *L)
{
  INT_PTR key = luaL_checkinteger(L,1);
  PSInfo *Info = GetPluginStartupInfo(L);
  Info->EditorControl(ECTL_PROCESSKEY, (void*)key);
  return 0;
}

// Item, Position = Menu (Properties, Items [, Breakkeys])
// Parameters:
//   Properties -- a table
//   Items      -- an array of tables
//   BreakKeys  -- an array of strings with special syntax
// Return value:
//   Item:
//     a table  -- the table of selected item (or of breakkey) is returned
//     a nil    -- menu canceled by the user
//   Position:
//     a number -- position of selected menu item
//     a nil    -- menu canceled by the user
int far_Menu(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int X = -1, Y = -1, MaxHeight = 0;
  int Flags = FMENU_WRAPMODE | FMENU_AUTOHIGHLIGHT;
  const wchar_t *Title = L"Menu", *Bottom = NULL, *HelpTopic = NULL;
  int SelectIndex = 0;

  lua_settop (L, 3);    // cut unneeded parameters; make stack predictable
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checktype(L, 2, LUA_TTABLE);
  if (!lua_isnil(L,3) && !lua_istable(L,3))
    return luaL_argerror(L, 3, "must be table or nil");

  lua_newtable(L); // temporary store; at stack position 4
  int store = 0;

  // Properties
  lua_pushvalue (L,1);  // push Properties on top (stack index 5)
  X = GetOptIntFromTable(L, "X", -1);
  Y = GetOptIntFromTable(L, "Y", -1);
  MaxHeight = GetOptIntFromTable(L, "MaxHeight", 0);
  lua_getfield(L, 1, "Flags");
  Flags = CheckFlags(L, -1);
  lua_getfield(L, 1, "Title");
  if(lua_isstring(L,-1))    Title = StoreTempString(L, 4, &store);
  lua_getfield(L, 1, "Bottom");
  if(lua_isstring(L,-1))    Bottom = StoreTempString(L, 4, &store);
  lua_getfield(L, 1, "HelpTopic");
  if(lua_isstring(L,-1))    HelpTopic = StoreTempString(L, 4, &store);
  lua_getfield(L, 1, "SelectIndex");
  SelectIndex = lua_tointeger(L,-1);

  // Items
  int i;
  int ItemsNumber = lua_objlen(L, 2);
  struct FarMenuItemEx* Items = (struct FarMenuItemEx*)
    lua_newuserdata(L, ItemsNumber*sizeof(struct FarMenuItemEx));
  memset(Items, 0, ItemsNumber*sizeof(struct FarMenuItemEx));
  struct FarMenuItemEx* pItem = Items;
  for(i=0; i < ItemsNumber; i++,pItem++,lua_pop(L,1)) {
    lua_pushinteger(L, i+1);
    lua_gettable(L, 2);
    if (!lua_istable(L, -1))
      return luaLF_SlotError (L, i+1, "table");
    //-------------------------------------------------------------------------
    const char *key = "text";
    lua_getfield(L, -1, key);
    if (lua_isstring(L,-1))  pItem->Text = StoreTempString(L, 4, &store);
    else if(!lua_isnil(L,-1)) return luaLF_FieldError (L, key, "string");
    if (!pItem->Text)
      lua_pop(L, 1);
    //-------------------------------------------------------------------------
    lua_getfield(L,-1,"checked");
    if (lua_type(L,-1) == LUA_TSTRING) {
      const wchar_t* s = utf8_to_utf16(L,-1,NULL);
      if (s) pItem->Flags |= s[0];
    }
    else if (lua_toboolean(L,-1)) pItem->Flags |= MIF_CHECKED;
    lua_pop(L,1);
    //-------------------------------------------------------------------------
    if (GetBoolFromTable(L, "separator")) pItem->Flags |= MIF_SEPARATOR;
    if (GetBoolFromTable(L, "disable"))   pItem->Flags |= MIF_DISABLE;
    if (GetBoolFromTable(L, "grayed"))    pItem->Flags |= MIF_GRAYED;
    if (GetBoolFromTable(L, "hidden"))    pItem->Flags |= MIF_HIDDEN;
    //-------------------------------------------------------------------------
    lua_getfield(L, -1, "AccelKey");
    if (lua_isnumber(L,-1)) pItem->AccelKey = lua_tointeger(L,-1);
    lua_pop(L, 1);
  }
  if (SelectIndex > 0 && SelectIndex <= ItemsNumber)
    Items[SelectIndex-1].Flags |= MIF_SELECTED;

  // Break Keys
  int BreakCode;
  int *pBreakKeys=NULL, *pBreakCode=NULL;
  int NumBreakCodes = lua_istable(L,3) ? lua_objlen(L,3) : 0;
  if (NumBreakCodes) {
    int* BreakKeys = (int*)lua_newuserdata(L, (1+NumBreakCodes)*sizeof(int));
    // get virtualkeys table from the registry; push it on top
    lua_pushstring(L, FAR_VIRTUALKEYS);
    lua_rawget(L, LUA_REGISTRYINDEX);
    // push breakkeys table on top
    lua_pushvalue(L, 3);              // vk=-2; bk=-1;
    char buf[32];
    int ind; // used outside the following loop
    for(ind=0; ind < NumBreakCodes; ind++) {
      // get next break key (optional modifier plus virtual key)
      lua_pushinteger(L,ind+1);       // vk=-3; bk=-2;
      lua_gettable(L,-2);             // vk=-3; bk=-2;
      if(!lua_istable(L,-1)) break;
      lua_getfield(L, -1, "BreakKey");// vk=-4; bk=-3;
      if(!lua_isstring(L,-1)) break;
      // separate modifier and virtual key strings
      int mod = 0;
      const char* s = lua_tostring(L,-1);
      if(strlen(s) >= sizeof(buf)) break;
      strcpy(buf, s);
      char* vk = strchr(buf, '+');  // virtual key
      if (vk) {
        *vk++ = '\0';
        if(strchr(buf,'C')) mod |= PKF_CONTROL;
        if(strchr(buf,'A')) mod |= PKF_ALT;
        if(strchr(buf,'S')) mod |= PKF_SHIFT;
        mod <<= 16;
        // replace on stack: break key name with virtual key name
        lua_pop(L, 1);
        lua_pushstring(L, vk);
      }
      // get virtual key and break key values
      lua_rawget(L,-4);               // vk=-4; bk=-3;
      BreakKeys[ind] = lua_tointeger(L,-1) | mod;
      lua_pop(L,2);                   // vk=-2; bk=-1;
    }
    BreakKeys[ind] = 0; // required by FAR API
    pBreakKeys = BreakKeys;
    pBreakCode = &BreakCode;
  }

  int ret = Info->Menu(
    Info->ModuleNumber, X, Y, MaxHeight, Flags|FMENU_USEEXT,
    Title, Bottom, HelpTopic, pBreakKeys, pBreakCode,
    (const struct FarMenuItem *)Items, ItemsNumber);

  if (NumBreakCodes && (BreakCode != -1)) {
    lua_pushinteger(L, BreakCode+1);
    lua_gettable(L, 3);
  }
  else if (ret == -1)
    return 0;
  else {
    lua_pushinteger(L, ret+1);
    lua_gettable(L, 2);
  }
  lua_pushinteger(L, ret+1);
  return 2;
}

// Return:   -1 if escape pressed, else - button number chosen (0 based).
int LF_Message(PSInfo *Info,
  const wchar_t* aMsg,      // if multiline, then lines must be separated by '\n'
  const wchar_t* aTitle,
  const wchar_t* aButtons,  // if multiple, then captions must be separated by ';'
  const char* aFlags,
  const wchar_t* aHelpTopic)
{
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE hnd = GetStdHandle(STD_OUTPUT_HANDLE);
  int ret = GetConsoleScreenBufferInfo(hnd, &csbi);
  const int MAXLEN    = ret ? csbi.srWindow.Right - csbi.srWindow.Left+1-14 : 66;
  const int MAX_ITEMS = ret ? csbi.srWindow.Bottom - csbi.srWindow.Top+1-3 : 22;
  const wchar_t** items = (const wchar_t**) malloc(MAX_ITEMS * sizeof(wchar_t*));
  const wchar_t** pItems = items;
  int num_items = 0, num_buttons = 0;
  unsigned Flags = 0;

  // Title
  *pItems++ = aTitle;
  num_items++;

  // Buttons
  wchar_t *BtnCopy = NULL, *ptr = NULL;
  if (*aButtons == L';') {
    const wchar_t* p = aButtons + 1;
    if      (!_wcsicmp(p, L"Ok"))               Flags = FMSG_MB_OK;
    else if (!_wcsicmp(p, L"OkCancel"))         Flags = FMSG_MB_OKCANCEL;
    else if (!_wcsicmp(p, L"AbortRetryIgnore")) Flags = FMSG_MB_ABORTRETRYIGNORE;
    else if (!_wcsicmp(p, L"YesNo"))            Flags = FMSG_MB_YESNO;
    else if (!_wcsicmp(p, L"YesNoCancel"))      Flags = FMSG_MB_YESNOCANCEL;
    else if (!_wcsicmp(p, L"RetryCancel"))      Flags = FMSG_MB_RETRYCANCEL;
  }
  else {
    // Buttons: 1-st pass, determining number of buttons
    // (giving buttons priority over message lines).
    BtnCopy = wcsdup(aButtons);
    ptr = BtnCopy;
    while (*ptr && (num_buttons < MAX_ITEMS-2)) {
      while (*ptr == L';')
        ptr++; // skip semicolons
      if (*ptr) {
        ++num_buttons;
        ptr = wcschr(ptr, L';');
        if (!ptr) break;
      }
    }
    num_items += num_buttons;
  }

  // Message lines
  wchar_t* allocLines[MAX_ITEMS];       // array of pointers to allocated lines
  int nAlloc = 0;                       // number of allocated lines
  int lastSpace = -1, lastDelim = -1;   // positions; -1 stands for "invalid"

  int pos;
  wchar_t* MsgCopy = wcsdup(aMsg);
  ptr = MsgCopy;
  for (pos=0; num_items < MAX_ITEMS; ) {
    if (ptr[pos] == 0) {     // end of the entire message
      *pItems++ = ptr;
      ++num_items;
      break;
    }
    if (ptr[pos] == '\n') {     // end of a message line
      *pItems++ = ptr;
      ptr[pos] = '\0';
      ++num_items;
      ptr += pos+1;
      pos = 0;
      lastSpace = lastDelim = -1;
    }
    else if (pos < MAXLEN) {    // characters inside the message
      if (ptr[pos] == L' ' || ptr[pos] == L'\t') lastSpace = pos;
      else if (!isalnum(ptr[pos]) && ptr[pos] != L'_') lastDelim = pos;
      pos++;
    }
    else {                      // the 1-st character beyond the message
      if (ptr[pos] == L' ' || ptr[pos] == L'\t') {    // is it a space?
        *pItems++ = ptr;                              // -> split here
        ptr[pos] = 0;
        ++num_items;
        ptr += pos+1;
        pos = 0;
        lastSpace = lastDelim = -1;
      }
      else if (lastSpace != -1) {                   // is lastSpace valid?
        *pItems++ = ptr;                            // -> split at lastSpace
        ptr[lastSpace] = 0;
        ++num_items;
        ptr += lastSpace+1;
        pos = 0;
        lastSpace = lastDelim = -1;
      }
      else {                                        // line allocation is needed
        int len = lastDelim != -1 ? lastDelim+1 : pos;
        wchar_t** q = &allocLines[nAlloc++];
        *pItems++ = *q = (wchar_t*) malloc((len+1)*sizeof(wchar_t));
        wcsncpy(*q, ptr, len);
        (*q)[len] = '\0';
        ++num_items;
        ptr += len;
        pos = 0;
        lastSpace = lastDelim = -1;
      }
    }
  }

  if (*aButtons != L';') {
    // Buttons: 2-nd pass.
    int i;
    ptr = BtnCopy;
    for (i=0; i < num_buttons; i++) {
      while (*ptr == ';')
        ++ptr;
      if (*ptr) {
        *pItems++ = ptr;
        ptr = wcschr(ptr, L';');
        if (ptr)
          *ptr++ = 0;
        else
          break;
      }
      else break;
    }
  }

  // Flags
  if (aFlags) {
    if(strchr(aFlags, 'w')) Flags |= FMSG_WARNING;
    if(strchr(aFlags, 'e')) Flags |= FMSG_ERRORTYPE;
    if(strchr(aFlags, 'k')) Flags |= FMSG_KEEPBACKGROUND;
    if(strchr(aFlags, 'l')) Flags |= FMSG_LEFTALIGN;
  }

  ret = Info->Message (Info->ModuleNumber, Flags, aHelpTopic, items,
                       num_items, num_buttons);
  free(BtnCopy);
  while(nAlloc) free(allocLines[--nAlloc]);
  free(MsgCopy);
  free(items);
  return ret;
}

void LF_Error(lua_State *L, const wchar_t* aMsg)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  if (!aMsg) aMsg = L"<non-string error message>";
  lua_pushlstring(L, (const char*)Info->ModuleName, wcslen(Info->ModuleName)*sizeof(wchar_t));
  lua_pushlstring(L, (const char*)L":\n", 4);
  lua_pushlstring(L, (const char*)aMsg, wcslen(aMsg)*sizeof(wchar_t));
  lua_pushlstring(L, "\0\0", 2);
  lua_concat(L, 4);
  LF_Message(Info, (const wchar_t*)lua_tostring(L,-1), L"Error", L"OK", "w", NULL);
  lua_pop(L, 1);
}

int SplitToTable(lua_State *L, const wchar_t *Text, wchar_t Delim, int StartIndex)
{
  int count = StartIndex;
  const wchar_t *p = Text;
  do {
    const wchar_t *q = wcschr(p, Delim);
    if (q == NULL) q = wcschr(p, L'\0');
    lua_pushinteger(L, ++count);
    lua_pushlstring(L, (const char*)p, (q-p)*sizeof(wchar_t));
    lua_rawset(L, -3);
    p = *q ? q+1 : NULL;
  } while(p);
  return count - StartIndex;
}

// 1-st param: message text (if multiline, then lines must be separated by '\n')
// 2-nd param: message title (if absent or nil, then "Message" is used)
// 3-rd param: buttons (if multiple, then captions must be separated by ';';
//             if absent or nil, then one button "OK" is used).
// 4-th param: flags
// 5-th param: help topic
// Return: -1 if escape pressed, else - button number chosen (0 based).
int far_Message(lua_State *L)
{
  luaL_checkany(L,1);
  lua_settop(L,5);
  const wchar_t *Msg = NULL;
  if (lua_isstring(L, 1))
    Msg = check_utf8_string(L, 1, NULL);
  else {
    lua_getglobal(L, "tostring");
    if (lua_isfunction(L,-1)) {
      lua_pushvalue(L,1);
      lua_call(L,1,1);
      Msg = check_utf8_string(L,-1,NULL);
    }
    if (Msg == NULL) luaL_argerror(L, 1, "cannot convert to string");
    lua_replace(L,1);
  }
  const wchar_t *Title   = opt_utf8_string(L, 2, L"Message");
  const wchar_t *Buttons = opt_utf8_string(L, 3, L";OK");
  const char    *Flags   = luaL_optstring(L, 4, "");
  const wchar_t *HelpTopic = opt_utf8_string(L, 5, NULL);

  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushinteger(L, LF_Message(Info, Msg, Title, Buttons, Flags, HelpTopic));
  return 1;
}

int far_CmpName(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t *Pattern = check_utf8_string(L, 1, NULL);
  const wchar_t *String  = check_utf8_string(L, 2, NULL);
  int SkipPath  = (lua_gettop(L) >= 3 && lua_toboolean(L,3)) ? 1:0;
  lua_pushboolean(L, Info->CmpName(Pattern, String, SkipPath));
  return 1;
}

int far_CtrlCheckPanelsExist(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (Info->Control(handle, FCTL_CHECKPANELSEXIST, 0, 0))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_CtrlClosePlugin(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  const wchar_t *dir = opt_utf8_string(L, 2, L".");
  if (Info->Control(handle, FCTL_CLOSEPLUGIN, 0, (LONG_PTR)dir))
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_CtrlGetPanelInfo(lua_State *L /*, BOOL ShortInfo*/)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }

  struct PanelInfo pi;
  int ret = Info->Control(handle, FCTL_GETPANELINFO, 0, (LONG_PTR)&pi);
  if(ret == 0)
    return lua_pushnil(L), 1;

  lua_createtable(L, 0, 13);
  //-------------------------------------------------------------------------
  PutIntToTable(L, "PanelType", pi.PanelType);
  PutBoolToTable(L, "Plugin", pi.Plugin != 0);
  //-------------------------------------------------------------------------
  lua_createtable(L, 0, 4); // "PanelRect"
  PutIntToTable(L, "left", pi.PanelRect.left);
  PutIntToTable(L, "top", pi.PanelRect.top);
  PutIntToTable(L, "right", pi.PanelRect.right);
  PutIntToTable(L, "bottom", pi.PanelRect.bottom);
  lua_setfield(L, -2, "PanelRect");
  //-------------------------------------------------------------------------
  PutIntToTable(L, "ItemsNumber", pi.ItemsNumber);
  PutIntToTable(L, "SelectedItemsNumber", pi.SelectedItemsNumber);
  //-------------------------------------------------------------------------
  PutIntToTable(L, "CurrentItem", pi.CurrentItem + 1);
  PutIntToTable(L, "TopPanelItem", pi.TopPanelItem + 1);
  PutBoolToTable(L, "Visible", pi.Visible);
  PutBoolToTable(L, "Focus", pi.Focus);
  PutIntToTable(L, "ViewMode", pi.ViewMode);
  PutBoolToTable(L, "ShortNames", pi.ShortNames);
  PutIntToTable(L, "SortMode", pi.SortMode);
  PutIntToTable(L, "Flags", pi.Flags);
  //-------------------------------------------------------------------------
  return 1;
}

int get_panel_item(lua_State *L, int command)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }
  int index = luaL_optinteger(L,3,1) - 1;
  if (index < 0) index = 0;
  int size = Info->Control(handle, command, index, 0);
  if (size) {
    struct PluginPanelItem* item = (struct PluginPanelItem*)lua_newuserdata(L, size);
    if (Info->Control(handle, command, index, (LONG_PTR)item)) {
      PushPanelItem(L, item);
      return 1;
    }
  }
  return lua_pushnil(L), 1;
}

int far_CtrlGetPanelItem(lua_State *L) {
  return get_panel_item(L, FCTL_GETPANELITEM);
}

int far_CtrlGetSelectedPanelItem(lua_State *L) {
  return get_panel_item(L, FCTL_GETSELECTEDPANELITEM);
}

int far_CtrlGetCurrentPanelItem(lua_State *L) {
  return get_panel_item(L, FCTL_GETCURRENTPANELITEM);
}

int get_string_info(lua_State *L, int command)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }
  int size = Info->Control(handle, command, 0, 0);
  if (size) {
    wchar_t *buf = (wchar_t*)lua_newuserdata(L, size * sizeof(wchar_t));
    if (Info->Control(handle, command, size, (LONG_PTR)buf)) {
      push_utf8_string(L, buf, -1);
      return 1;
    }
  }
  return lua_pushnil(L), 1;
}

int far_CtrlGetPanelDir(lua_State *L) {
  return get_string_info(L, FCTL_GETPANELDIR);
}

int far_CtrlGetPanelFormat(lua_State *L) {
  return get_string_info(L, FCTL_GETPANELFORMAT);
}

int far_CtrlGetPanelHostFile(lua_State *L) {
  return get_string_info(L, FCTL_GETPANELHOSTFILE);
}

int far_CtrlGetColumnTypes(lua_State *L) {
  return get_string_info(L, FCTL_GETCOLUMNTYPES);
}

int far_CtrlGetColumnWidths(lua_State *L) {
  return get_string_info(L, FCTL_GETCOLUMNWIDTHS);
}

int far_CtrlRedrawPanel(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }
  LONG_PTR param2 = 0;
  struct PanelRedrawInfo pri;
  if (lua_istable(L, 3)) {
    param2 = (LONG_PTR)&pri;
    lua_getfield(L, 3, "CurrentItem");
    pri.CurrentItem = lua_tointeger(L, -1) - 1;
    lua_getfield(L, 3, "TopPanelItem");
    pri.TopPanelItem = lua_tointeger(L, -1) - 1;
  }
  lua_pushboolean(L, Info->Control(handle, FCTL_REDRAWPANEL, 0, param2));
  return 1;
}

int SetPanelBooleanProperty(lua_State *L, int command)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }
  int param1 = lua_toboolean(L,3);
  lua_pushboolean(L, Info->Control(handle, command, param1, 0));
  return 1;
}

int SetPanelIntegerProperty(lua_State *L, int command)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }
  int param1 = check_env_flag(L,3);
  lua_pushboolean(L, Info->Control(handle, command, param1, 0));
  return 1;
}

int far_CtrlSetCaseSensitiveSort(lua_State *L) {
  return SetPanelBooleanProperty(L, FCTL_SETCASESENSITIVESORT);
}

int far_CtrlSetNumericSort(lua_State *L) {
  return SetPanelBooleanProperty(L, FCTL_SETNUMERICSORT);
}

int far_CtrlSetSortOrder(lua_State *L) {
  return SetPanelBooleanProperty(L, FCTL_SETSORTORDER);
}

int far_CtrlUpdatePanel(lua_State *L) {
  return SetPanelBooleanProperty(L, FCTL_UPDATEPANEL);
}

int far_CtrlSetSortMode(lua_State *L) {
  return SetPanelIntegerProperty(L, FCTL_SETSORTMODE);
}

int far_CtrlSetViewMode(lua_State *L) {
  return SetPanelIntegerProperty(L, FCTL_SETVIEWMODE);
}

int far_CtrlSetPanelDir(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }
  LONG_PTR param2 = 0;
  if (lua_isstring(L, 3)) {
    const wchar_t* dir = check_utf8_string(L, 3, NULL);
    param2 = (LONG_PTR)dir;
  }
  lua_pushboolean(L, Info->Control(handle, FCTL_SETPANELDIR, 0, param2));
  return 1;
}

int far_CtrlGetCmdLine(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  int size = Info->Control(handle, FCTL_GETCMDLINE, 0, 0);
  wchar_t *buf = (wchar_t*) malloc(size*sizeof(wchar_t));
  Info->Control(handle, FCTL_GETCMDLINE, size, (LONG_PTR)buf);
  push_utf8_string(L, buf, -1);
  free(buf);
  return 1;
}

int far_CtrlSetCmdLine(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  const wchar_t* str = check_utf8_string(L, 2, NULL);
  lua_pushboolean(L, Info->Control(handle, FCTL_SETCMDLINE, 0, (LONG_PTR)str));
  return 1;
}

int far_CtrlGetCmdLinePos(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  int pos;
  Info->Control(handle, FCTL_GETCMDLINEPOS, 0, (LONG_PTR)&pos) ?
    lua_pushinteger(L, pos+1) : lua_pushnil(L);
  return 1;
}

int far_CtrlSetCmdLinePos(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  int pos = luaL_checkinteger(L, 2) - 1;
  int ret = Info->Control(handle, FCTL_SETCMDLINEPOS, pos, 0);
  return lua_pushboolean(L, ret), 1;
}

int far_CtrlInsertCmdLine(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  const wchar_t* str = check_utf8_string(L, 2, NULL);
  lua_pushboolean(L, Info->Control(handle, FCTL_INSERTCMDLINE, 0, (LONG_PTR)str));
  return 1;
}

int far_CtrlGetCmdLineSelection(lua_State *L)
{
  struct CmdLineSelect cms;
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (Info->Control(handle, FCTL_GETCMDLINESELECTION, 0, (LONG_PTR)&cms)) {
    if (cms.SelStart < 0) cms.SelStart = 0;
    if (cms.SelEnd < 0) cms.SelEnd = 0;
    lua_pushinteger(L, cms.SelStart + 1);
    lua_pushinteger(L, cms.SelEnd);
    return 2;
  }
  return lua_pushnil(L), 1;
}

int far_CtrlSetCmdLineSelection(lua_State *L)
{
  struct CmdLineSelect cms;
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  cms.SelStart = luaL_checkinteger(L, 2) - 1;
  cms.SelEnd = luaL_checkinteger(L, 3);
  if (cms.SelStart < -1) cms.SelStart = -1;
  if (cms.SelEnd < -1) cms.SelEnd = -1;
  int ret = Info->Control(handle, FCTL_SETCMDLINESELECTION, 0, (LONG_PTR)&cms);
  return lua_pushboolean(L, ret), 1;
}

// CtrlSetSelection   (handle, whatpanel, items, selection)
// CtrlClearSelection (handle, whatpanel, items)
//   handle:       handle
//   whatpanel:    1=active_panel, 0=inactive_panel
//   items:        either number of an item, or a list of item numbers
//   selection:    boolean
int ChangePanelSelection(lua_State *L, BOOL op_set)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  if (handle == INVALID_HANDLE_VALUE) {
    handle = (luaL_checkinteger(L,2) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  }
  int itemindex = -1;
  if (lua_isnumber(L,3)) {
    itemindex = lua_tointeger(L,3) - 1;
    if (itemindex < 0) return luaL_argerror(L, 3, "non-positive index");
  }
  else if (!lua_istable(L,3))
    return luaL_typerror(L, 3, "number or table");
  int state = op_set ? lua_toboolean(L,4) : 0;

  // get panel info
  struct PanelInfo pi;
  if (!Info->Control(handle, FCTL_GETPANELINFO, 0, (LONG_PTR)&pi) ||
     (pi.PanelType != PTYPE_FILEPANEL))
    return 0;
  //---------------------------------------------------------------------------
  int numItems = op_set ? pi.ItemsNumber : pi.SelectedItemsNumber;
  int command  = op_set ? FCTL_SETSELECTION : FCTL_CLEARSELECTION;
  Info->Control(handle, FCTL_BEGINSELECTION, 0, 0);
  if (itemindex >= 0 && itemindex < numItems)
    Info->Control(handle, command, itemindex, state);
  else {
    int i, len = lua_objlen(L,3);
    for (i=1; i<=len; i++) {
      lua_pushinteger(L, i);
      lua_gettable(L,3);
      if (lua_isnumber(L,-1)) {
        itemindex = lua_tointeger(L,-1) - 1;
        if (itemindex >= 0 && itemindex < numItems)
          Info->Control(handle, command, itemindex, state);
      }
      lua_pop(L,1);
    }
  }
  Info->Control(handle, FCTL_ENDSELECTION, 0, 0);
  //---------------------------------------------------------------------------
  return lua_pushboolean(L,1), 1;
}

int far_CtrlSetSelection(lua_State *L) {
  return ChangePanelSelection(L, TRUE);
}

int far_CtrlClearSelection(lua_State *L) {
  return ChangePanelSelection(L, FALSE);
}

// CtrlSetUserScreen (handle)
//   handle:       FALSE=INVALID_HANDLE_VALUE, TRUE=lua_State*
int far_CtrlSetUserScreen(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  int ret = Info->Control(handle, FCTL_SETUSERSCREEN, 0, 0);
  if(ret)
    return lua_pushboolean(L, 1), 1;
  return 0;
}

// CtrlGetUserScreen (handle)
//   handle:       FALSE=INVALID_HANDLE_VALUE, TRUE=lua_State*
int far_CtrlGetUserScreen(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  int ret = Info->Control(handle, FCTL_GETUSERSCREEN, 0, 0);
  if(ret)
    return lua_pushboolean(L, 1), 1;
  return 0;
}

int far_CtrlIsActivePanel(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  return lua_pushboolean(L, Info->Control(handle, FCTL_ISACTIVEPANEL, 0, 0)), 1;
}

// GetDirList (Dir)
//   Dir:     Name of the directory to scan (full pathname).
int far_GetDirList (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t *Dir = check_utf8_string (L, 1, NULL);
  struct FAR_FIND_DATA *PanelItems;
  int ItemsNumber;
  int ret = Info->GetDirList (Dir, &PanelItems, &ItemsNumber);
  if(ret) {
    int i;
    lua_createtable(L, ItemsNumber, 0); // "PanelItems"
    for(i=0; i < ItemsNumber; i++) {
      PushFarFindData (L, PanelItems + i);
      lua_rawseti(L, -2, i+1);
    }
    Info->FreeDirList (PanelItems, ItemsNumber);
    return 1;
  }
  return 0;
}

// GetPluginDirList (PluginNumber, hPlugin, Dir)
//   PluginNumber:    Number of plugin module.
//   hPlugin:         Current plugin instance handle.
//   Dir:             Name of the directory to scan (full pathname).
int far_GetPluginDirList (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int PluginNumber = luaL_checkinteger (L, 1);
  HANDLE handle = OptHandle (L, 2);
  const wchar_t *Dir = check_utf8_string (L, 3, NULL);
  struct PluginPanelItem *PanelItems;
  int ItemsNumber;
  int ret = Info->GetPluginDirList (PluginNumber, handle, Dir, &PanelItems, &ItemsNumber);
  if(ret) {
    PushPanelItems (L, PanelItems, ItemsNumber);
    Info->FreePluginDirList (PanelItems, ItemsNumber);
    return 1;
  }
  return 0;
}

// RestoreScreen (handle)
//   handle:    handle of saved screen.
int far_RestoreScreen (lua_State *L)
{
  if (lua_type(L,1) == LUA_TLIGHTUSERDATA) {
    PSInfo *Info = GetPluginStartupInfo(L);
    Info->RestoreScreen ((HANDLE)lua_touserdata (L, 1));
    return lua_pushboolean(L, 1), 1;
  }
  return 0;
}

// handle = SaveScreen (X1,Y1,X2,Y2)
//   handle:    handle of saved screen, [lightuserdata]
int far_SaveScreen (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int X1 = luaL_optinteger(L,1,0);
  int Y1 = luaL_optinteger(L,2,0);
  int X2 = luaL_optinteger(L,3,-1);
  int Y2 = luaL_optinteger(L,4,-1);
  void* handle = Info->SaveScreen(X1,Y1,X2,Y2);
  if (handle) {
    return lua_pushlightuserdata(L, handle), 1;
  }
  return 0;
}

int GetDialogItemType(lua_State* L, int key, int item)
{
  lua_pushinteger(L, key);
  lua_gettable(L, -2);
  int iType;
  if(!get_env_flag(L, -1, &iType)) {
    const char* sType = lua_tostring(L, -1);
    return luaL_error(L, "%s - unsupported type in dialog item %d", sType, item);
  }
  lua_pop(L, 1);
  return iType;
}

// the table is on lua stack top
int GetItemFlags(lua_State* L, int flag_index, int item_index)
{
  int flags;
  lua_pushinteger(L, flag_index);
  lua_gettable(L, -2);
  if (!GetFlagCombination (L, -1, &flags))
    return luaL_error(L, "unsupported flag in dialog item %d", item_index);
  lua_pop(L, 1);
  return flags;
}

// list table is on Lua stack top
struct FarList* CreateList (lua_State *L, int historyindex)
{
  int n = lua_objlen(L,-1);
  char* ptr = (char*)lua_newuserdata(L,
    sizeof(struct FarList) + n*sizeof(struct FarListItem)); // +2
  size_t len = lua_objlen(L, historyindex);
  lua_rawseti (L, historyindex, ++len); // +1; put into "histories" table to avoid being gc'ed
  struct FarList* list = (struct FarList*) ptr;
  list->ItemsNumber = n;
  list->Items = (struct FarListItem*)(ptr + sizeof(struct FarList));
  int i;
  for (i=0; i<n; i++) {
    lua_pushinteger(L, i+1); // +2
    lua_gettable(L,-2);      // +2
    if (lua_type(L,-1) != LUA_TTABLE)
      luaL_error (L, "value at index %d is not a table", i+1);
    struct FarListItem *p = list->Items + i;
    p->Text = NULL;
    lua_getfield(L, -1, "Text"); // +3
    if (lua_isstring(L,-1)) {
      lua_pushvalue(L,-1);       // +4
      p->Text = check_utf8_string(L,-1,NULL); // +4
      lua_rawseti(L, historyindex, ++len);  // +3
    }
    lua_pop(L, 1);                 // +2
    lua_getfield(L, -1, "Flags");  // +3
    p->Flags = CheckFlags(L,-1);
    lua_pop(L,2);                  // +1
  }
  return list;
}

// item table is on Lua stack top
void SetFarDialogItem(lua_State *L, struct FarDialogItem* Item, int itemindex,
  int historyindex)
{
  memset(Item, 0, sizeof(struct FarDialogItem));
  Item->Type  = GetDialogItemType (L, 1, itemindex+1);
  Item->X1    = GetIntFromArray   (L, 2);
  Item->Y1    = GetIntFromArray   (L, 3);
  Item->X2    = GetIntFromArray   (L, 4);
  Item->Y2    = GetIntFromArray   (L, 5);
  Item->Focus = GetIntFromArray   (L, 6);
  Item->Flags = GetItemFlags      (L, 8, itemindex+1);
  if (Item->Type==DI_LISTBOX || Item->Type==DI_COMBOBOX) {
    lua_pushinteger(L, 7);   // +1
    lua_gettable(L, -2);     // +1
    if (lua_type(L,-1) != LUA_TTABLE)
      luaLF_SlotError (L, 7, "table");
    Item->ListItems = CreateList(L, historyindex);
    int SelectIndex = GetOptIntFromTable(L, "SelectIndex", -1);
    if (SelectIndex > 0 && SelectIndex <= (int)lua_objlen(L,-1))
      Item->ListItems->Items[SelectIndex-1].Flags |= LIF_SELECTED;
    lua_pop(L,1);                    // 0
  }
  else if (Item->Flags & DIF_HISTORY) {
    lua_pushinteger(L, 7);   // +1
    lua_gettable(L, -2);     // +1
    if (!lua_isstring(L,-1))
      luaLF_SlotError (L, 7, "string");
    Item->History = check_utf8_string (L, -1, NULL); // +1
    size_t len = lua_objlen(L, historyindex);
    lua_rawseti (L, historyindex, len+1); // +0; put into "histories" table to avoid being gc'ed
  }
  else
    Item->Selected = GetIntFromArray(L, 7);
  Item->DefaultButton = GetIntFromArray(L, 9);

  Item->MaxLen = GetOptIntFromArray(L, 11, 0);
  lua_pushinteger(L, 10); // +1
  lua_gettable(L, -2);    // +1
  if (lua_isstring(L, -1)) {
    Item->PtrData = check_utf8_string (L, -1, NULL); // +1
    size_t len = lua_objlen(L, historyindex);
    lua_rawseti (L, historyindex, len+1); // +0; put into "histories" table to avoid being gc'ed
  }
  else
    lua_pop(L, 1);
}

void PushDlgItem (lua_State *L, const struct FarDialogItem* pItem, BOOL table_exist)
{
  if (! table_exist) {
    lua_createtable(L, 11, 0);
    if (pItem->Type == DI_LISTBOX || pItem->Type == DI_COMBOBOX) {
      lua_createtable(L, 0, 1);
      lua_rawseti(L, -2, 7);
    }
  }
  PutIntToArray  (L, 1, pItem->Type);
  PutIntToArray  (L, 2, pItem->X1);
  PutIntToArray  (L, 3, pItem->Y1);
  PutIntToArray  (L, 4, pItem->X2);
  PutIntToArray  (L, 5, pItem->Y2);
  PutIntToArray  (L, 6, pItem->Focus);

  if (pItem->Type == DI_LISTBOX || pItem->Type == DI_COMBOBOX) {
    lua_rawgeti(L, -1, 7);
    lua_pushinteger(L, pItem->ListPos+1);
    lua_setfield(L, -2, "SelectIndex");
    lua_pop(L,1);
  }
  else
    PutIntToArray(L, 7, pItem->Selected);

  PutIntToArray  (L, 8, pItem->Flags);
  PutIntToArray  (L, 9, pItem->DefaultButton);
  lua_pushinteger(L, 10);
  push_utf8_string(L, pItem->PtrData, -1);
  lua_settable(L, -3);
  PutIntToArray  (L, 11, pItem->MaxLen);
}

void PushDlgItemNum (lua_State *L, HANDLE hDlg, int numitem, int pos_table,
  PSInfo *Info)
{
  int size = Info->SendDlgMessage(hDlg, DM_GETDLGITEM, numitem, 0);
  if (size > 0) {
    struct FarDialogItem* pItem = (struct FarDialogItem*) lua_newuserdata(L, size);
    Info->SendDlgMessage(hDlg, DM_GETDLGITEM, numitem, (LONG_PTR)pItem);

    BOOL table_exist = lua_istable(L, pos_table);
    if (table_exist)
      lua_pushvalue(L, pos_table);
    PushDlgItem(L, pItem, table_exist);
    lua_remove(L, -2);
  }
  else
    lua_pushnil(L);
}

int SetDlgItem (lua_State *L, HANDLE hDlg, int numitem, int pos_table,
  PSInfo *Info)
{
  struct FarDialogItem DialogItem;
  lua_newtable(L);
  lua_replace(L,1);
  luaL_checktype(L, pos_table, LUA_TTABLE);
  lua_pushvalue(L, pos_table);
  SetFarDialogItem(L, &DialogItem, numitem, 1);
  if (Info->SendDlgMessage(hDlg, DM_SETDLGITEM, numitem, (LONG_PTR)&DialogItem))
    lua_pushboolean(L,1);
  else
    lua_pushboolean(L,0);
  return 1;
}

TDialogData* NewDialogData(lua_State* L, PSInfo *Info, HANDLE hDlg,
                           BOOL isOwned)
{
  TDialogData *dd = (TDialogData*) lua_newuserdata(L, sizeof(TDialogData));
  dd->L        = L;
  dd->Info     = Info;
  dd->hDlg     = hDlg;
  dd->isOwned  = isOwned;
  dd->wasError = FALSE;
  luaL_getmetatable(L, FarDialogType);
  lua_setmetatable(L, -2);
  if (isOwned) {
    lua_newtable(L);
    lua_setfenv(L, -2);
  }
  return dd;
}

TDialogData* CheckDialog(lua_State* L, int pos)
{
  return (TDialogData*)luaL_checkudata(L, pos, FarDialogType);
}

TDialogData* CheckValidDialog(lua_State* L, int pos)
{
  TDialogData* dd = CheckDialog(L, pos);
  luaL_argcheck(L, dd->hDlg != INVALID_HANDLE_VALUE, pos, "closed dialog");
  return dd;
}

HANDLE CheckDialogHandle (lua_State* L, int pos)
{
  return CheckValidDialog(L, pos)->hDlg;
}

int far_SendDlgMessage (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int Msg, Param1, Param2=0, res, res_incr=0;
  wchar_t buf[512];
  //---------------------------------------------------------------------------
  COORD                      coord;
  struct DialogInfo          dlg_info;
  struct EditorSelect        es;
  struct EditorSetPosition   esp;
  struct FarDialogItemData   fdid;
  struct FarListDelete       fld;
  struct FarListFind         flf;
  struct FarListGetItem      flgi;
  struct FarListInfo         fli;
  struct FarListInsert       flins;
  struct FarListPos          flp;
  struct FarListTitles       flt;
  struct FarListUpdate       flu;
  struct FarListItemData     flid;
  SMALL_RECT                 small_rect;
  //---------------------------------------------------------------------------
  lua_settop(L, 4);
  HANDLE hDlg = CheckDialogHandle(L, 1);
  get_env_flag (L, 2, &Msg);
  Param1 = luaL_optinteger(L, 3, 0);

  switch(Msg) {
    default:
      luaL_argerror(L, 2, "operation not implemented");
      break;

    case DM_CLOSE:
    case DM_EDITUNCHANGEDFLAG:
    case DM_ENABLE:
    case DM_ENABLEREDRAW:
    case DM_GETCHECK:
    case DM_GETCOMBOBOXEVENT:
    case DM_GETCURSORSIZE:
    case DM_GETDLGDATA:
    case DM_GETDROPDOWNOPENED:
    case DM_GETFOCUS:
    case DM_GETITEMDATA:
    case DM_GETTEXTLENGTH:
    case DM_LISTGETDATASIZE:
    case DM_LISTSORT:
    case DM_REDRAW:               // alias: DM_SETREDRAW
    case DM_SET3STATE:
    case DM_SETCURSORSIZE:
    case DM_SETDLGDATA:
    case DM_SETDROPDOWNOPENED:
    case DM_SETFOCUS:
    case DM_SETITEMDATA:
    case DM_SETMAXTEXTLENGTH:     // alias: DM_SETTEXTLENGTH
    case DM_SETMOUSEEVENTNOTIFY:
    case DM_SHOWDIALOG:
    case DM_SHOWITEM:
    case DM_USER:
      Param2 = luaL_optlong(L, 4, 0);
      break;

    case DM_LISTADDSTR: res_incr=1;
    case DM_ADDHISTORY:
    case DM_SETHISTORY:
    case DM_SETTEXTPTR:
      Param2 = (LONG_PTR) opt_utf8_string(L, 4, NULL);
      break;

    case DM_LISTSETMOUSEREACTION:
    case DM_SETCHECK:
      get_env_flag (L, 4, &Param2);
      break;

    case DM_GETCURSORPOS:
      if (Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&coord)) {
        lua_createtable(L,0,2);
        PutNumToTable(L, "X", coord.X);
        PutNumToTable(L, "Y", coord.Y);
        return 1;
      }
      return lua_pushnil(L), 1;

    case DM_GETDIALOGINFO:
      dlg_info.StructSize = sizeof(dlg_info);
      if (Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&dlg_info)) {
        lua_createtable(L,0,2);
        PutNumToTable(L, "StructSize", dlg_info.StructSize);
        PutLStrToTable(L, "Id", (const char*)&dlg_info.Id, sizeof(dlg_info.Id));
        return 1;
      }
      return lua_pushnil(L), 1;

    case DM_GETDLGRECT:
    case DM_GETITEMPOSITION:
      if (Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&small_rect)) {
        lua_createtable(L,0,4);
        PutNumToTable(L, "Left", small_rect.Left);
        PutNumToTable(L, "Top", small_rect.Top);
        PutNumToTable(L, "Right", small_rect.Right);
        PutNumToTable(L, "Bottom", small_rect.Bottom);
        return 1;
      }
      return lua_pushnil(L), 1;

    case DM_GETEDITPOSITION:
      if (Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&esp))
        return PushEditorSetPosition(L, &esp), 1;
      return lua_pushnil(L), 1;

    case DM_GETSELECTION:
      if (Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&es)) {
        lua_createtable(L,0,5);
        PutNumToTable(L, "BlockType", es.BlockType);
        PutNumToTable(L, "BlockStartLine", es.BlockStartLine);
        PutNumToTable(L, "BlockStartPos", es.BlockStartPos);
        PutNumToTable(L, "BlockWidth", es.BlockWidth);
        PutNumToTable(L, "BlockHeight", es.BlockHeight);
        return 1;
      }
      return lua_pushnil(L), 1;

    case DM_SETSELECTION:
      luaL_checktype(L, 4, LUA_TTABLE);
      if (SetEditorSelect(L, 4, &es)) {
        Param2 = (LONG_PTR)&es;
        break;
      }
      return lua_pushinteger(L,0), 1;

    case DM_GETTEXT:
      fdid.PtrData = buf;
      fdid.PtrLength = sizeof(buf)/sizeof(buf[0]) - 1;
      Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&fdid);
      push_utf8_string(L, fdid.PtrData, -1);
      return 1;

    case DM_GETCONSTTEXTPTR:
      push_utf8_string(L, (wchar_t*)Info->SendDlgMessage (hDlg, Msg, Param1, 0), -1);
      return 1;

    case DM_SETTEXT:
      fdid.PtrData = (wchar_t*)check_utf8_string(L, 4, NULL);
      fdid.PtrLength = 0; // wcslen(fdid.PtrData);
      Param2 = (LONG_PTR)&fdid;
      break;

    case DM_KEY: {
      luaL_checktype(L, 4, LUA_TTABLE);
      res = lua_objlen(L, 4);
      DWORD* arr = (DWORD*)lua_newuserdata(L, res * sizeof(DWORD));
      int i;
      for(i=0; i<res; i++) {
        lua_pushinteger(L,i+1);
        lua_gettable(L,4);
        arr[i] = lua_tointeger(L,-1);
        lua_pop(L,1);
      }
      res = Info->SendDlgMessage (hDlg, Msg, res, (LONG_PTR)arr);
      return lua_pushinteger(L, res), 1;
    }

    case DM_LISTADD:
    case DM_LISTSET: {
      luaL_checktype(L, 4, LUA_TTABLE);
      lua_createtable(L,1,0); // "history table"
      lua_replace(L,1);
      lua_settop(L,4);
      struct FarList *list = CreateList(L, 1);
      Param2 = (LONG_PTR)list;
      break;
    }

    case DM_LISTDELETE:
      luaL_checktype(L, 4, LUA_TTABLE);
      fld.StartIndex = GetOptIntFromTable(L, "StartIndex", 1) - 1;
      fld.Count = GetOptIntFromTable(L, "Count", 1);
      Param2 = (LONG_PTR)&fld;
      break;

    case DM_LISTFINDSTRING:
      luaL_checktype(L, 4, LUA_TTABLE);
      flf.StartIndex = GetOptIntFromTable(L, "StartIndex", 1) - 1;
      lua_getfield(L, 4, "Pattern");
      flf.Pattern = check_utf8_string(L, -1, NULL);
      lua_getfield(L, 4, "Flags");
      get_env_flag(L, -1, (int*)&flf.Flags);
      res = Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&flf);
      res < 0 ? lua_pushnil(L) : lua_pushinteger (L, res+1);
      return 1;

    case DM_LISTGETCURPOS:
      Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&flp);
      lua_createtable(L,0,2);
      PutIntToTable(L, "SelectPos", flp.SelectPos+1);
      PutIntToTable(L, "TopPos", flp.TopPos+1);
      return 1;

    case DM_LISTGETITEM:
      flgi.ItemIndex = luaL_checkinteger(L, 4) - 1;
      res = Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&flgi);
      if (res) {
        lua_createtable(L,0,2);
        PutIntToTable(L, "Flags", flgi.Item.Flags);
        PutWStrToTable(L, "Text", flgi.Item.Text, -1);
        return 1;
      }
      return lua_pushnil(L), 1;

    case DM_LISTGETTITLES:
      flt.Title = buf;
      flt.Bottom = buf + sizeof(buf)/2;
      flt.TitleLen = sizeof(buf)/2;
      flt.BottomLen = sizeof(buf)/2;
      res = Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&flt);
      if (res) {
        lua_createtable(L,0,2);
        PutWStrToTable(L, "Title", flt.Title, -1);
        PutWStrToTable(L, "Bottom", flt.Bottom, -1);
        return 1;
      }
      return lua_pushnil(L), 1;

    case DM_LISTSETTITLES:
      luaL_checktype(L, 4, LUA_TTABLE);
      lua_getfield(L, 4, "Title");
      flt.Title = lua_isstring(L,-1) ? check_utf8_string(L,-1,NULL) : NULL;
      lua_getfield(L, 4, "Bottom");
      flt.Bottom = lua_isstring(L,-1) ? check_utf8_string(L,-1,NULL) : NULL;
      Param2 = (LONG_PTR)&flt;
      break;

    case DM_LISTINFO:
      res = Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&fli);
      if (res) {
        lua_createtable(L,0,6);
        PutIntToTable(L, "Flags", fli.Flags);
        PutIntToTable(L, "ItemsNumber", fli.ItemsNumber);
        PutIntToTable(L, "SelectPos", fli.SelectPos+1);
        PutIntToTable(L, "TopPos", fli.TopPos+1);
        PutIntToTable(L, "MaxHeight", fli.MaxHeight);
        PutIntToTable(L, "MaxLength", fli.MaxLength);
        return 1;
      }
      return lua_pushnil(L), 1;

    case DM_LISTINSERT:
      luaL_checktype(L, 4, LUA_TTABLE);
      flins.Index = GetOptIntFromTable(L, "Index", 1) - 1;
      lua_getfield(L, 4, "Text");
      flins.Item.Text = lua_isstring(L,-1) ? check_utf8_string(L,-1,NULL) : NULL;
      lua_getfield(L, 4, "Flags"); //+1
      flins.Item.Flags = CheckFlags(L, -1);
      res = Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&flins);
      res < 0 ? lua_pushnil(L) : lua_pushinteger (L, res);
      return 1;

    case DM_LISTUPDATE:
      luaL_checktype(L, 4, LUA_TTABLE);
      flu.Index = GetOptIntFromTable(L, "Index", 1) - 1;
      lua_getfield(L, 4, "Text");
      flu.Item.Text = lua_isstring(L,-1) ? check_utf8_string(L,-1,NULL) : NULL;
      lua_getfield(L, 4, "Flags"); //+1
      flu.Item.Flags = CheckFlags(L, -1);
      lua_pushboolean(L, Info->SendDlgMessage (hDlg, Msg, Param1, (LONG_PTR)&flu));
      return 1;

    case DM_LISTSETCURPOS:
      luaL_checktype(L, 4, LUA_TTABLE);
      flp.SelectPos = GetOptIntFromTable(L, "SelectPos", 1) - 1;
      flp.TopPos = GetOptIntFromTable(L, "TopPos", 1) - 1;
      Param2 = (LONG_PTR)&flp;
      break;

    case DM_LISTSETDATA:
      memset(&flid, 0, sizeof(flid));
      luaL_checktype(L, 4, LUA_TTABLE);
      flid.Index = GetOptIntFromTable(L, "Index", 1) - 1;
      lua_getfenv(L, 1);
      lua_getfield(L, 4, "Data");
      flid.Data = (void*)(INT_PTR)luaL_ref(L, -2);
      flid.DataSize = sizeof(DWORD);
      Param2 = (LONG_PTR)&flid;
      break;

    case DM_LISTGETDATA:
      res = Info->SendDlgMessage (hDlg, Msg, Param1, luaL_checkinteger(L, 4)-1);
      if (res) {
        lua_getfenv(L, 1);
        lua_rawgeti(L, -1, res);
      }
      else lua_pushnil(L);
      return 1;

    case DM_GETDLGITEM:
      return PushDlgItemNum(L, hDlg, Param1, 4, Info), 1;

    case DM_SETDLGITEM:
      return SetDlgItem(L, hDlg, Param1, 4, Info);

    case DM_MOVEDIALOG:
    case DM_RESIZEDIALOG:
    case DM_SETCURSORPOS: {
      luaL_checktype(L, 4, LUA_TTABLE);
      coord.X = GetOptIntFromTable(L, "X", 0);
      coord.Y = GetOptIntFromTable(L, "Y", 0);
      Param2 = (LONG_PTR)&coord;
      if (Msg == DM_SETCURSORPOS)
        break;
      COORD* c = (COORD*) Info->SendDlgMessage (hDlg, Msg, Param1, Param2);
      lua_createtable(L, 0, 2);
      PutIntToTable(L, "X", c->X);
      PutIntToTable(L, "Y", c->Y);
      return 1;
    }

    case DM_SETITEMPOSITION:
      luaL_checktype(L, 4, LUA_TTABLE);
      small_rect.Left = GetOptIntFromTable(L, "Left", 0);
      small_rect.Top = GetOptIntFromTable(L, "Top", 0);
      small_rect.Right = GetOptIntFromTable(L, "Right", 0);
      small_rect.Bottom = GetOptIntFromTable(L, "Bottom", 0);
      Param2 = (LONG_PTR)&small_rect;
      break;

    case DM_SETCOMBOBOXEVENT:
      Param2 = CheckFlags(L, 4);
      break;

    case DM_SETEDITPOSITION:
      luaL_checktype(L, 4, LUA_TTABLE);
      lua_settop(L, 4);
      FillEditorSetPosition(L, &esp);
      Param2 = (LONG_PTR)&esp;
      break;

    //~ case DM_GETTEXTPTR:
  }
  res = Info->SendDlgMessage (hDlg, Msg, Param1, Param2);
  lua_pushinteger (L, res + res_incr);
  return 1;
}

LONG_PTR WINAPI DlgProc(HANDLE hDlg, int Msg, int Param1, LONG_PTR Param2)
{
  TDialogData *dd = (TDialogData*) gInfo.SendDlgMessage(hDlg,DM_GETDLGDATA,0,0);
  if (dd->wasError)
    return dd->Info->DefDlgProc(hDlg, Msg, Param1, Param2);
  lua_State *L = dd->L;
  PSInfo *Info = dd->Info;

  lua_pushlightuserdata (L, dd);       //+1   retrieve the table
  lua_rawget (L, LUA_REGISTRYINDEX);   //+1
  lua_rawgeti(L, -1, 2);               //+2   retrieve the procedure
  lua_rawgeti(L, -2, 3);               //+3   retrieve the handle
  lua_pushinteger (L, Msg);            //+4
  lua_pushinteger (L, Param1);         //+5

  if (Msg == DN_CTLCOLORDLGLIST) {
    struct FarListColors* flc = (struct FarListColors*) Param2;
    lua_createtable(L, flc->ColorCount, 1);
    PutIntToTable(L, "Flags", flc->Flags);
    int i;
    for (i=0; i < flc->ColorCount; i++)
      PutIntToArray(L, i+1, flc->Colors[i]);
  }

  else if (Msg == DN_DRAWDLGITEM)
    PushDlgItem (L, (struct FarDialogItem*)Param2, FALSE);

  else if (Msg == DN_EDITCHANGE)
    PushDlgItem (L, (struct FarDialogItem*)Param2, FALSE);

  else if (Msg == DN_HELP)
    push_utf8_string (L, Param2 ? (wchar_t*)Param2 : L"", -1);

  else if (Msg == DN_INITDIALOG)
    lua_pushnil(L);

  else if (Msg == DN_LISTCHANGE || Msg == DN_LISTHOTKEY)
    lua_pushinteger (L, Param2+1); // make list positions 1-based

  else if (Msg == DN_MOUSECLICK || Msg == DN_MOUSEEVENT)
    PutMouseEvent (L, (const MOUSE_EVENT_RECORD*)Param2, FALSE);

  else if (Msg == DN_RESIZECONSOLE) {
    COORD* coord = (COORD*)Param2;
    lua_createtable(L, 0, 2);
    PutIntToTable(L, "X", coord->X);
    PutIntToTable(L, "Y", coord->Y);
  }

  else
    lua_pushinteger (L, Param2); //+6

  //---------------------------------------------------------------------------
  LONG_PTR ret = pcall_msg (L, 4, 1); //+2
  if (ret) {
    lua_pop(L, 1);
    dd->wasError = TRUE;
    Info->SendDlgMessage(hDlg, DM_CLOSE, -1, 0);
    return Info->DefDlgProc(hDlg, Msg, Param1, Param2);
  }
  //---------------------------------------------------------------------------

  if (lua_isnil(L, -1))
    ret = Info->DefDlgProc(hDlg, Msg, Param1, Param2);

  else if (Msg == DN_CTLCOLORDLGLIST) {
    struct FarListColors* flc = (struct FarListColors*) Param2;
    if ((ret = lua_istable(L,-1)) != 0) {
      int i;
      for (i=0; i < flc->ColorCount; i++)
        flc->Colors[i] = GetIntFromArray(L, i+1);
    }
  }

  else if (Msg == DN_GETDIALOGINFO) {
    ret = lua_isstring(L,-1) && lua_objlen(L,-1) >= sizeof(GUID);
    if (ret) {
      struct DialogInfo* di = (struct DialogInfo*) Param2;
      //di->StructSize = sizeof(DialogInfo);
      memcpy(&di->Id, lua_tostring(L,-1), sizeof(GUID));
    }
  }

  else if (Msg == DN_HELP) {
    if ((ret = (LONG_PTR)utf8_to_utf16(L, -1, NULL)) != 0) {
      lua_pushvalue(L, -1);                // keep stack balanced
      lua_setfield(L, -3, "helpstring");   // protect from garbage collector
    }
  }

  else if (lua_isnumber(L, -1))
    ret = lua_tointeger (L, -1);
  else
    ret = lua_toboolean(L, -1);

  lua_pop (L, 2);
  return ret;
}

int far_DialogInit(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);

  int X1 = luaL_checkinteger(L, 1);
  int Y1 = luaL_checkinteger(L, 2);
  int X2 = luaL_checkinteger(L, 3);
  int Y2 = luaL_checkinteger(L, 4);
  const wchar_t *HelpTopic = opt_utf8_string(L, 5, NULL);
  luaL_checktype(L, 6, LUA_TTABLE);

  lua_newtable (L); // create a "histories" table, to prevent history strings
                    // from being garbage collected too early
  lua_replace (L, 1);

  int ItemsNumber = lua_objlen(L, 6);
  struct FarDialogItem* Items = (struct FarDialogItem*)
    lua_newuserdata (L, ItemsNumber * sizeof(struct FarDialogItem));
  lua_replace (L, 2);
  int i;
  for(i=0; i < ItemsNumber; i++) {
    lua_pushinteger(L, i+1);
    lua_gettable(L, 6);
    int type = lua_type(L, -1);
    if (type == LUA_TTABLE) {
      SetFarDialogItem(L, Items+i, i, 1);
    }
    lua_pop(L, 1);
    if(type == LUA_TNIL)
      break;
    if(type != LUA_TTABLE)
      return luaL_error(L, "Items[%d] is not a table", i+1);
  }

  // 7-th parameter (flags)
  int Flags = CheckFlags(L,7);

  TDialogData* dd = NewDialogData(L, Info, INVALID_HANDLE_VALUE, TRUE);

  // 8-th parameter (DlgProc function)
  FARAPIDEFDLGPROC Proc = NULL;
  LONG_PTR Param = 0;
  if (lua_isfunction(L, 8)) {
    Proc = DlgProc;
    Param = (LONG_PTR)dd;
  }

  dd->hDlg = Info->DialogInit(Info->ModuleNumber, X1, Y1, X2, Y2, HelpTopic,
                              Items, ItemsNumber, 0, Flags, Proc, Param);

  if (dd->hDlg != INVALID_HANDLE_VALUE) {
    // Put some values into the registry
    lua_pushlightuserdata(L, dd); // important: index it with dd
    lua_createtable(L, 3, 0);
    lua_pushvalue (L, 1);     // store the "histories" table
    lua_rawseti(L, -2, 1);
    if (lua_isfunction(L, 8)) {
      lua_pushvalue (L, 8);   // store the procedure
      lua_rawseti(L, -2, 2);
      lua_pushvalue (L, -3);  // store the handle
      lua_rawseti(L, -2, 3);
    }
    lua_rawset (L, LUA_REGISTRYINDEX);
  }
  else
    lua_pushnil(L);
  return 1;
}

void free_dialog (TDialogData* dd)
{
  lua_State* L = dd->L;
  if (dd->isOwned && dd->hDlg != INVALID_HANDLE_VALUE) {
    dd->Info->DialogFree(dd->hDlg);
    dd->hDlg = INVALID_HANDLE_VALUE;
    lua_pushlightuserdata(L, dd);
    lua_pushnil (L);
    lua_rawset (L, LUA_REGISTRYINDEX);
  }
}

int far_DialogRun (lua_State *L)
{
  TDialogData* dd = CheckValidDialog(L, 1);
  int result = dd->Info->DialogRun(dd->hDlg);
  if (dd->wasError) {
    free_dialog(dd);
    luaL_error(L, "error occured in dialog procedure");
  }
  lua_pushinteger(L, result);
  return 1;
}

int far_DialogFree (lua_State *L)
{
  free_dialog(CheckDialog(L, 1));
  return 0;
}

int dialog_gc (lua_State *L)
{
  free_dialog(CheckDialog(L, 1));
  return 0;
}

int dialog_tostring (lua_State *L)
{
  TDialogData* dd = CheckDialog(L, 1);
  if (dd->hDlg != INVALID_HANDLE_VALUE)
    lua_pushfstring(L, "%s (%p)", FarDialogType, dd->hDlg);
  else
    lua_pushfstring(L, "%s (closed)", FarDialogType);
  return 1;
}

int far_DefDlgProc(lua_State *L)
{
  int Msg, Param1, Param2;

  luaL_checktype(L, 1, LUA_TLIGHTUSERDATA);
  HANDLE hDlg = lua_touserdata(L, 1);
  get_env_flag(L, 2, &Msg);
  Param1 = luaL_checkinteger(L, 3);
  Param2 = luaL_checkinteger(L, 4);

  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushinteger(L, Info->DefDlgProc(hDlg, Msg, Param1, Param2));
  return 1;
}

int far_GetDlgItem(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE hDlg = CheckDialogHandle(L,1);
  int numitem = luaL_checkinteger(L,2);
  PushDlgItemNum(L, hDlg, numitem, 3, Info);
  return 1;
}

int far_SetDlgItem(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE hDlg = CheckDialogHandle(L,1);
  int numitem = luaL_checkinteger(L,2);
  return SetDlgItem(L, hDlg, numitem, 3, Info);
}

int far_Editor(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t* FileName = check_utf8_string(L, 1, NULL);
  const wchar_t* Title    = opt_utf8_string(L, 2, FileName);
  int X1 = luaL_optinteger(L, 3, 0);
  int Y1 = luaL_optinteger(L, 4, 0);
  int X2 = luaL_optinteger(L, 5, -1);
  int Y2 = luaL_optinteger(L, 6, -1);
  int Flags = CheckFlags(L,7);
  int StartLine = luaL_optinteger(L, 8, -1);
  int StartChar = luaL_optinteger(L, 9, -1);
  int CodePage  = luaL_optinteger(L, 10, CP_AUTODETECT);
  int ret = Info->Editor(FileName, Title, X1, Y1, X2, Y2, Flags,
                         StartLine, StartChar, CodePage);
  lua_pushinteger(L, ret);
  return 1;
}

int far_Viewer(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t* FileName = check_utf8_string(L, 1, NULL);
  const wchar_t* Title    = opt_utf8_string(L, 2, FileName);
  int X1 = luaL_optinteger(L, 3, 0);
  int Y1 = luaL_optinteger(L, 4, 0);
  int X2 = luaL_optinteger(L, 5, -1);
  int Y2 = luaL_optinteger(L, 6, -1);
  int Flags = CheckFlags(L, 7);
  int CodePage = luaL_optinteger(L, 8, CP_AUTODETECT);
  int ret = Info->Viewer(FileName, Title, X1, Y1, X2, Y2, Flags, CodePage);
  lua_pushboolean(L, ret);
  return 1;
}

int far_ViewerGetInfo(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct ViewerInfo vi;
  vi.StructSize = sizeof(vi);
  if (!Info->ViewerControl(VCTL_GETINFO, &vi))
    return 0;
  lua_createtable(L, 0, 10);
  PutNumToTable(L,  "ViewerID",    vi.ViewerID);
  PutWStrToTable(L, "FileName",    vi.FileName, -1);
  PutNumToTable(L,  "FileSize",    vi.FileSize);
  PutNumToTable(L,  "FilePos",     vi.FilePos);
  PutNumToTable(L,  "WindowSizeX", vi.WindowSizeX);
  PutNumToTable(L,  "WindowSizeY", vi.WindowSizeY);
  PutNumToTable(L,  "Options",     vi.Options);
  PutNumToTable(L,  "TabSize",     vi.TabSize);
  PutNumToTable(L,  "LeftPos",     vi.LeftPos);
  lua_createtable(L, 0, 4);
  PutNumToTable (L, "CodePage",    vi.CurMode.CodePage);
  PutBoolToTable(L, "Wrap",        vi.CurMode.Wrap);
  PutNumToTable (L, "WordWrap",    vi.CurMode.WordWrap);
  PutBoolToTable(L, "Hex",         vi.CurMode.Hex);
  lua_setfield(L, -2, "CurMode");
  return 1;
}

int far_ViewerQuit(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  Info->ViewerControl(VCTL_QUIT, NULL);
  return 0;
}

int far_ViewerRedraw(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  Info->ViewerControl(VCTL_REDRAW, NULL);
  return 0;
}

int far_ViewerSelect(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct ViewerSelect vs;
  vs.BlockStartPos = (long long int)luaL_checknumber(L,1);
  vs.BlockLen = luaL_checkinteger(L,2);
  lua_pushboolean(L, Info->ViewerControl(VCTL_SELECT, &vs));
  return 1;
}

int far_ViewerSetPosition(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  struct ViewerSetPosition vsp;
  if (lua_istable(L, 1)) {
    lua_settop(L, 1);
    vsp.StartPos = (__int64)GetOptNumFromTable(L, "StartPos", 0);
    vsp.LeftPos = (__int64)GetOptNumFromTable(L, "LeftPos", 0);
    vsp.Flags   = GetOptIntFromTable(L, "Flags", 0);
  }
  else {
    vsp.StartPos = (__int64)luaL_optnumber(L,1,0);
    vsp.LeftPos = (__int64)luaL_optnumber(L,2,0);
    vsp.Flags = luaL_optinteger(L,3,0);
  }
  if (Info->ViewerControl(VCTL_SETPOSITION, &vsp))
    lua_pushnumber(L, (double)vsp.StartPos);
  else
    lua_pushnil(L);
  return 1;
}

int far_ViewerSetMode(lua_State *L)
{
  struct ViewerSetMode vsm;
  memset(&vsm, 0, sizeof(struct ViewerSetMode));
  luaL_checktype(L, 1, LUA_TTABLE);

  lua_getfield(L, 1, "Type");
  if (!get_env_flag (L, -1, &vsm.Type))
    return lua_pushboolean(L,0), 1;

  lua_getfield(L, 1, "iParam");
  if (lua_isnumber(L, -1))
    vsm.Param.iParam = lua_tointeger(L, -1);
  else
    return lua_pushboolean(L,0), 1;

  int flags;
  lua_getfield(L, 1, "Flags");
  if (!get_env_flag (L, -1, &flags))
    return lua_pushboolean(L,0), 1;
  vsm.Flags = flags;

  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushboolean(L, Info->ViewerControl(VCTL_SETMODE, &vsm));
  return 1;
}

int far_ShowHelp(lua_State *L)
{
  const wchar_t *ModuleName = check_utf8_string (L,1,NULL);
  const wchar_t *HelpTopic = opt_utf8_string (L,2,NULL);
  int Flags = CheckFlags(L,3);
  PSInfo *Info = GetPluginStartupInfo(L);
  BOOL ret = Info->ShowHelp (ModuleName, HelpTopic, Flags);
  return lua_pushboolean(L, ret), 1;
}

// DestText = far.InputBox(Title,Prompt,HistoryName,SrcText,DestLength,HelpTopic,Flags)
// all arguments are optional
int far_InputBox(lua_State *L)
{
  const wchar_t *Title       = opt_utf8_string (L, 1, L"Input Box");
  const wchar_t *Prompt      = opt_utf8_string (L, 2, L"Enter the text:");
  const wchar_t *HistoryName = opt_utf8_string (L, 3, NULL);
  const wchar_t *SrcText     = opt_utf8_string (L, 4, L"");
  int DestLength             = luaL_optinteger (L, 5, 1024);
  const wchar_t *HelpTopic   = opt_utf8_string (L, 6, NULL);
  DWORD Flags = luaL_optinteger (L, 7, FIB_ENABLEEMPTY|FIB_BUTTONS|FIB_NOAMPERSAND);

  if (DestLength < 1) DestLength = 1;
  wchar_t *DestText = (wchar_t*) malloc(sizeof(wchar_t)*DestLength);
  PSInfo *Info = GetPluginStartupInfo(L);
  int res = Info->InputBox(Title, Prompt, HistoryName, SrcText, DestText,
                           DestLength, HelpTopic, Flags);

  if (res) push_utf8_string (L, DestText, -1);
  else lua_pushnil(L);

  free(DestText);
  return 1;
}

int far_GetMsg(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t* msg = GetMsg (Info, luaL_checkinteger(L, 1));
  if (msg)
    return push_utf8_string(L, msg, -1), 1;
  return 0;
}

int far_Text(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  int X = luaL_checkinteger(L, 1);
  int Y = luaL_checkinteger(L, 2);
  int Color = 0xFF & luaL_checkinteger(L, 3);
  const wchar_t* Str = opt_utf8_string(L, 4, L"");
  Info->Text(X, Y, Color, Str);
  return 0;
}

void OneLevelUp(const wchar_t *src, wchar_t *trg)
{
  wchar_t *p;
  wcscpy(trg, src);
  p = wcsrchr(trg, L'\\');
  *(p ? p : trg) = L'\0';
}

// SetRegKey (DataType, Key, ValueName, ValueData)
//   DataType:        "string","expandstring","multistring","dword" or "binary", [string]
//   Key:             registry key, [string]
//   ValueName:       registry value name, [string]
//   ValueData:       registry value data, [string | number | lstring]
// Returns:
//   nothing.
int far_SetRegKey(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const char* DataType    = luaL_checkstring(L, 1);
  wchar_t* Key            = (wchar_t*)check_utf8_string(L, 2, NULL);
  wchar_t* ValueName      = (wchar_t*)check_utf8_string(L, 3, NULL);
  wchar_t farkey[512];
  size_t len;
  OneLevelUp(Info->RootKey, farkey);

  if (!strcmp ("string", DataType)) {
    SetRegKeyStr(HKEY_CURRENT_USER, farkey, Key, ValueName,
              (wchar_t*)check_utf8_string(L, 4, NULL));
  }
  else if (!strcmp ("dword", DataType)) {
    SetRegKeyDword(HKEY_CURRENT_USER, farkey, Key, ValueName,
              luaL_checkinteger(L, 4));
  }
  else if (!strcmp ("binary", DataType)) {
    BYTE *data = (BYTE*)luaL_checklstring(L, 4, &len);
    SetRegKeyArr(HKEY_CURRENT_USER, farkey, Key, ValueName, data, len);
  }
  else if (!strcmp ("expandstring", DataType)) {
    const wchar_t* data = check_utf8_string(L, 4, NULL);
    HKEY hKey = CreateRegKey(HKEY_CURRENT_USER, farkey, Key);
    RegSetValueExW(hKey, ValueName, 0, REG_EXPAND_SZ, (BYTE*)data, 1+wcslen(data));
    RegCloseKey(hKey);
  }
  else if (!strcmp ("multistring", DataType)) {
    const char* data = luaL_checklstring(L, 4, &len);
    HKEY hKey = CreateRegKey(HKEY_CURRENT_USER, farkey, Key);
    RegSetValueExW(hKey, ValueName, 0, REG_MULTI_SZ, (BYTE*)data, len);
    RegCloseKey(hKey);
  }
  else
    luaL_argerror (L, 1, "unsupported value type");
  return 0;
}

// ValueData, DataType = GetRegKey (Key, ValueName)
//   Key:             registry key, [string]
//   ValueName:       registry value name, [string]
//   ValueData:       registry value data, [string | number | lstring]
//   DataType:        "string", "expandstring", "multistring", "dword"
//                    or "binary", [string]
int far_GetRegKey(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  wchar_t* Key = (wchar_t*)check_utf8_string(L, 1, NULL);
  const wchar_t* ValueName = check_utf8_string(L, 2, NULL);
  wchar_t farkey[512];
  OneLevelUp(Info->RootKey, farkey);

  HKEY hKey = OpenRegKey(HKEY_CURRENT_USER, farkey, Key);
  if (hKey == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "OpenRegKey failed.");
    return 2;
  }

  DWORD datatype, datasize;
  RegQueryValueExW(hKey, ValueName, NULL, &datatype, NULL, &datasize);

  char* data = (char*) malloc(datasize);
  LONG ret = RegQueryValueExW(hKey, ValueName, NULL, &datatype, (BYTE*)data, &datasize);
  RegCloseKey(hKey);

  if (ret != ERROR_SUCCESS) {
    lua_pushnil(L);
    lua_pushstring(L, "RegQueryValueEx failed.");
  }
  else {
    switch (datatype) {
      case REG_BINARY:
        lua_pushlstring (L, data, datasize);
        lua_pushstring (L, "binary");
        break;
      case REG_DWORD:
        lua_pushinteger (L, *(int*)data);
        lua_pushstring (L, "dword");
        break;
      case REG_SZ:
        push_utf8_string (L, (wchar_t*)data, -1);
        lua_pushstring (L, "string");
        break;
      case REG_EXPAND_SZ:
        push_utf8_string (L, (wchar_t*)data, -1);
        lua_pushstring (L, "expandstring");
        break;
      case REG_MULTI_SZ:
        push_utf8_string (L, (wchar_t*)data, datasize/sizeof(wchar_t));
        lua_pushstring (L, "multistring");
        break;
      default:
        lua_pushnil(L);
        lua_pushstring(L, "unsupported value type");
        break;
    }
  }
  free(data);
  return 2;
}

// Result = DeleteRegKey (Key)
//   Key:             registry key, [string]
//   Result:          TRUE if success, FALSE if failure, [boolean]
int far_DeleteRegKey(lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t* Key = check_utf8_string(L, 1, NULL);
  wchar_t farkey[512];
  OneLevelUp(Info->RootKey, farkey);
  wcscat(farkey, L"\\");
  wcscat(farkey, Key);
  long res = RegDeleteKeyW (HKEY_CURRENT_USER, farkey);
  lua_pushboolean (L, res==ERROR_SUCCESS);
  return 1;
}

// Based on "CheckForEsc" function, by Ivan Sintyurin (spinoza@mail.ru)
WORD ExtractKey()
{
  INPUT_RECORD rec;
  DWORD ReadCount;
  HANDLE hConInp=GetStdHandle(STD_INPUT_HANDLE);
  while (PeekConsoleInput(hConInp,&rec,1,&ReadCount), ReadCount) {
    ReadConsoleInput(hConInp,&rec,1,&ReadCount);
    if (rec.EventType==KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
      return rec.Event.KeyEvent.wVirtualKeyCode;
  }
  return 0;
}

// result = ExtractKey()
// -- general purpose function; not FAR dependent
int far_ExtractKey(lua_State *L)
{
  WORD vKey = ExtractKey() & 0xff;
  if (vKey && VirtualKeyStrings[vKey])
    lua_pushstring(L, VirtualKeyStrings[vKey]);
  else
    lua_pushnil(L);
  return 1;
}

int far_CopyToClipboard (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  const wchar_t *str = check_utf8_string(L,1,NULL);
  int r = Info->FSF->CopyToClipboard(str);
  return lua_pushboolean(L, r), 1;
}

int far_PasteFromClipboard (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  wchar_t* str = Info->FSF->PasteFromClipboard();
  if (str) {
    push_utf8_string(L, str, -1);
    Info->FSF->DeleteBuffer(str);
  }
  else lua_pushnil(L);
  return 1;
}

int far_FarKeyToName (lua_State *L)
{
  wchar_t buf[256];
  int Key = luaL_checkinteger(L,1);
  BOOL result = gFSF.FarKeyToName(Key, buf, DIM(buf)-1);
  if (result) push_utf8_string(L, buf, -1);
  else lua_pushnil(L);
  return 1;
}

int far_FarNameToKey (lua_State *L)
{
  const wchar_t* str = check_utf8_string(L,1,NULL);
  int Key = gFSF.FarNameToKey(str);
  if (Key == -1) lua_pushnil(L);
  else lua_pushinteger(L, Key);
  return 1;
}

int far_FarInputRecordToKey (lua_State *L)
{
  INPUT_RECORD ir;
  FillInputRecord(L, 1, &ir);
  lua_pushinteger(L, gFSF.FarInputRecordToKey(&ir));
  return 1;
}

int far_LStricmp (lua_State *L)
{
  const wchar_t* s1 = check_utf8_string(L, 1, NULL);
  const wchar_t* s2 = check_utf8_string(L, 2, NULL);
  lua_pushinteger(L, gFSF.LStricmp(s1, s2));
  return 1;
}

int far_LStrnicmp (lua_State *L)
{
  const wchar_t* s1 = check_utf8_string(L, 1, NULL);
  const wchar_t* s2 = check_utf8_string(L, 2, NULL);
  int num = luaL_checkinteger(L, 3);
  if (num < 0) num = 0;
  lua_pushinteger(L, gFSF.LStrnicmp(s1, s2, num));
  return 1;
}

int far_ProcessName (lua_State *L)
{
  const wchar_t* param1 = check_utf8_string(L, 1, NULL);
  const wchar_t* param2 = check_utf8_string(L, 2, NULL);
  int flags = CheckFlags(L, 3);

  const int BUFSIZE = 1024;
  wchar_t* buf = (wchar_t*)lua_newuserdata(L, BUFSIZE * sizeof(wchar_t));
  wcsncpy(buf, param2, BUFSIZE-1);
  buf[BUFSIZE-1] = 0;

  int result = gFSF.ProcessName(param1, buf, BUFSIZE, flags);
  if (flags == PN_GENERATENAME && result != 0)
    push_utf8_string(L, buf, -1);
  else
    lua_pushboolean(L, result);
  return 1;
}

int far_GetReparsePointInfo (lua_State *L)
{
  const wchar_t* Src = check_utf8_string(L, 1, NULL);
  int size = gFSF.GetReparsePointInfo(Src, NULL, 0);
  if (size <= 0)
    return lua_pushnil(L), 1;
  wchar_t* Dest = (wchar_t*)lua_newuserdata(L, size * sizeof(wchar_t));
  gFSF.GetReparsePointInfo(Src, Dest, size);
  return push_utf8_string(L, Dest, -1), 1;
}

int far_LIsAlpha (lua_State *L)
{
  const wchar_t* str = check_utf8_string(L, 1, NULL);
  return lua_pushboolean(L, gFSF.LIsAlpha(*str)), 1;
}

int far_LIsAlphanum (lua_State *L)
{
  const wchar_t* str = check_utf8_string(L, 1, NULL);
  return lua_pushboolean(L, gFSF.LIsAlphanum(*str)), 1;
}

int far_LIsLower (lua_State *L)
{
  const wchar_t* str = check_utf8_string(L, 1, NULL);
  return lua_pushboolean(L, gFSF.LIsLower(*str)), 1;
}

int far_LIsUpper (lua_State *L)
{
  const wchar_t* str = check_utf8_string(L, 1, NULL);
  return lua_pushboolean(L, gFSF.LIsUpper(*str)), 1;
}

int convert_buf (lua_State *L, int command)
{
  const wchar_t* src = check_utf8_string(L, 1, NULL);
  int len;
  if (lua_isnoneornil(L,2))
    len = wcslen(src);
  else if (lua_isnumber(L,2)) {
    len = lua_tointeger(L,2);
    if (len < 0) len = 0;
  }
  else
    return luaL_typerror(L, 3, "optional number");
  wchar_t* dest = (wchar_t*)lua_newuserdata(L, (len+1)*sizeof(wchar_t));
  wcsncpy(dest, src, len+1);
  if (command=='l')
    gFSF.LLowerBuf(dest,len);
  else
    gFSF.LUpperBuf(dest,len);
  return push_utf8_string(L, dest, -1), 1;
}

int far_LLowerBuf (lua_State *L) {
  return convert_buf(L, 'l');
}

int far_LUpperBuf (lua_State *L) {
  return convert_buf(L, 'u');
}

int far_MkTemp (lua_State *L)
{
  const wchar_t* prefix = opt_utf8_string(L, 1, NULL);
  const int dim = 4096;
  wchar_t* dest = (wchar_t*)lua_newuserdata(L, dim * sizeof(wchar_t));
  if (gFSF.MkTemp(dest, dim, prefix))
    push_utf8_string(L, dest, -1);
  else
    lua_pushnil(L);
  return 1;
}

int far_MkLink (lua_State *L)
{
  const wchar_t* src = check_utf8_string(L, 1, NULL);
  const wchar_t* dst = check_utf8_string(L, 2, NULL);
  DWORD flags = CheckFlags(L, 3);
  return lua_pushboolean(L, gFSF.MkLink(src, dst, flags)), 1;
}

int far_GetPathRoot (lua_State *L)
{
  const wchar_t* Path = check_utf8_string(L, 1, NULL);
  wchar_t* Root = (wchar_t*)lua_newuserdata(L, 4096 * sizeof(wchar_t));
  *Root = L'\0';
  gFSF.GetPathRoot(Path, Root, 4096);
  return push_utf8_string(L, Root, -1), 1;
}

int truncstring (lua_State *L, int op)
{
  const wchar_t* Src = check_utf8_string(L, 1, NULL);
  int MaxLen = luaL_checkinteger(L, 2);
  int SrcLen = wcslen(Src);
  if (MaxLen < 0) MaxLen = 0;
  else if (MaxLen > SrcLen) MaxLen = SrcLen;
  wchar_t* Trg = (wchar_t*)lua_newuserdata(L, (1 + SrcLen) * sizeof(wchar_t));
  wcscpy(Trg, Src);
  const wchar_t* ptr = (op == 'p') ?
    gFSF.TruncPathStr(Trg, MaxLen) : gFSF.TruncStr(Trg, MaxLen);
  return push_utf8_string(L, ptr, -1), 1;
}

int far_TruncPathStr (lua_State *L)
{
  return truncstring(L, 'p');
}

int far_TruncStr (lua_State *L)
{
  return truncstring(L, 's');
}

int WINAPI FrsUserFunc (const struct FAR_FIND_DATA *FData, const wchar_t *FullName,
  void *Param)
{
  lua_State *L = (lua_State*) Param;
  lua_pushvalue(L, 3); // push the Lua function
  PushFarFindData(L, FData);
  push_utf8_string(L, FullName, -1);
  int err = lua_pcall(L, 2, 1, 0);
  int proceed = !err && lua_toboolean(L, -1);
  if (err)
    LF_Error(L, check_utf8_string(L, -1, NULL));
  lua_pop(L, 1);
  return proceed;
}

int far_FarRecursiveSearch (lua_State *L)
{
  const wchar_t *InitDir = check_utf8_string(L, 1, NULL);
  const wchar_t *Mask = check_utf8_string(L, 2, NULL);
  luaL_checktype(L, 3, LUA_TFUNCTION);
  DWORD Flags = CheckFlags(L, 4);
  gFSF.FarRecursiveSearch(InitDir, Mask, FrsUserFunc, Flags, L);
  return 0;
}

int far_ConvertPath (lua_State *L)
{
  const wchar_t *Src = check_utf8_string(L, 1, NULL);
  enum CONVERTPATHMODES Mode = lua_isnoneornil(L,2) ?
    CPM_FULL : (enum CONVERTPATHMODES)check_env_flag(L,2);
  PSInfo *Info = GetPluginStartupInfo(L);
  size_t Size = Info->FSF->ConvertPath(Mode, Src, NULL, 0);
  wchar_t* Target = (wchar_t*)lua_newuserdata(L, Size*sizeof(wchar_t));
  Info->FSF->ConvertPath(Mode, Src, Target, Size);
  push_utf8_string(L, Target, -1);
  return 1;
}

int far_GetFileInfo (lua_State *L)
{
  WIN32_FIND_DATAW fd;
  const wchar_t *fname = check_utf8_string(L, 1, NULL);
  HANDLE h = FindFirstFileW(fname, &fd);
  if (h == INVALID_HANDLE_VALUE)
    lua_pushnil(L);
  else {
    PushWinFindData(L, &fd);
    FindClose(h);
  }
  return 1;
}

// os.getenv does not always work correctly, hence the following.
int far_GetEnv (lua_State *L)
{
  const wchar_t* name = check_utf8_string(L, 1, NULL);
  wchar_t buf[256];
  DWORD res = GetEnvironmentVariableW (name, buf, DIM(buf));
  if (res == 0) return 0;
  if (res < DIM(buf)) return push_utf8_string(L, buf, -1), 1;

  DWORD size = res + 1;
  wchar_t* p = (wchar_t*)lua_newuserdata(L, size * sizeof(wchar_t));
  res = GetEnvironmentVariableW (name, p, size);
  if (res > 0 && res < size)
    return push_utf8_string(L, p, -1), 1;
  return 0;
}

int far_SetEnv (lua_State *L)
{
  const wchar_t* name = check_utf8_string(L, 1, NULL);
  const wchar_t* value = opt_utf8_string(L, 2, NULL);
  BOOL res = SetEnvironmentVariableW (name, value);
  return lua_pushboolean (L, res), 1;
}

int far_AdvControl (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_settop(L,2);  /* for proper calling GetOptIntFromTable and the like */
  int Command = check_env_flag (L, 1);
  void *Param = NULL;
  wchar_t buf[300];
  struct ActlEjectMedia em;
  struct ActlKeyMacro km;
  struct KeySequence ks;
  struct FarSetColors fsc;
  struct PROGRESSVALUE pv;
  SMALL_RECT sr;
  COORD coord;

  switch (Command) {
    default:
      return luaL_argerror(L, 1, "command not supported");

    case ACTL_COMMIT:
    case ACTL_GETFARHWND:
    case ACTL_GETCONFIRMATIONS:
    case ACTL_GETDESCSETTINGS:
    case ACTL_GETDIALOGSETTINGS:
    case ACTL_GETINTERFACESETTINGS:
    case ACTL_GETPANELSETTINGS:
    case ACTL_GETPLUGINMAXREADDATA:
    case ACTL_GETSYSTEMSETTINGS:
    case ACTL_GETWINDOWCOUNT:
    case ACTL_PROGRESSNOTIFY:
    case ACTL_QUIT:
    case ACTL_REDRAWALL:
      break;

    case ACTL_GETCOLOR:
    case ACTL_SETCURRENTWINDOW:
    case ACTL_WAITKEY:
      Param = (void*) luaL_checkinteger(L, 2);
      break;

    case ACTL_SYNCHRO: {
      int p = luaL_checkinteger(L, 2);
      Param = malloc(sizeof(TSynchroData));
      InitSynchroData((TSynchroData*)Param, 0, 0, 0, p);
      break;
    }

    case ACTL_SETPROGRESSSTATE:
      Param = (void*)(INT_PTR) check_env_flag(L, 2);
      break;

    case ACTL_SETPROGRESSVALUE:
      luaL_checktype(L, 2, LUA_TTABLE);
      pv.Completed = (unsigned __int64)GetOptNumFromTable(L, "Completed", 0.0);
      pv.Total = (unsigned __int64)GetOptNumFromTable(L, "Total", 100.0);
      Param = &pv;
      break;

    case ACTL_GETSYSWORDDIV:
      Info->AdvControl(Info->ModuleNumber, Command, buf);
      return push_utf8_string(L,buf,-1), 1;

    case ACTL_EJECTMEDIA:
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_getfield(L, 2, "Letter");
      em.Letter = lua_isstring(L,-1) ? lua_tostring(L,-1)[0] : '\0';
      lua_getfield(L, 2, "Flags");
      em.Flags = CheckFlags(L,-1);
      Param = &em;
      break;

    case ACTL_KEYMACRO:
      memset(&km, 0, sizeof(km));
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_getfield(L, 2, "Command");
      get_env_flag(L, -1, &km.Command);
      lua_getfield(L, 2, "Flags");
      GetFlagCombination(L, -1, (int*)&km.Param.PlainText.Flags);
      if (km.Command == MCMD_POSTMACROSTRING || km.Command == MCMD_CHECKMACRO) {
        lua_getfield(L, 2, "SequenceText");
        if (lua_isstring(L,-1))
          km.Param.PlainText.SequenceText = check_utf8_string(L,-1,NULL);
        else
          return luaL_error(L, "SequenceText field must be a string");

        if (km.Command == MCMD_POSTMACROSTRING) {
          lua_getfield(L, 2, "AKey");
          km.Param.PlainText.AKey = lua_tointeger(L, -1);
        }
        else if (km.Command == MCMD_CHECKMACRO) {
          lua_pushinteger(L, Info->AdvControl(Info->ModuleNumber, Command, &km));
          lua_createtable(L, 0, 4);
          PutIntToTable(L, "ErrCode", km.Param.MacroResult.ErrCode);
          PutIntToTable(L, "ErrPosX", km.Param.MacroResult.ErrPos.X);
          PutIntToTable(L, "ErrPosY", km.Param.MacroResult.ErrPos.Y);
          PutWStrToTable(L, "ErrSrc", km.Param.MacroResult.ErrSrc, -1);
          return 2;
        }
      }
      Param = &km;
      break;

    case ACTL_GETARRAYCOLOR: {
      int size = Info->AdvControl(Info->ModuleNumber, Command, NULL);
      void *p = lua_newuserdata(L, size);
      Info->AdvControl(Info->ModuleNumber, Command, p);
      lua_createtable(L, size, 0);
      int i;
      for (i=0; i < size; i++) {
        lua_pushinteger(L, i+1);
        lua_pushinteger(L, ((BYTE*)p)[i]);
        lua_rawset(L,-3);
      }
      return 1;
    }

    case ACTL_GETFARVERSION: {
      DWORD n = Info->AdvControl(Info->ModuleNumber, Command, 0);
      int v1 = (n >> 8) & 0xff;
      int v2 = n & 0xff;
      int v3 = n >> 16;
      if (lua_toboolean(L, 2)) {
        lua_pushinteger(L, v1);
        lua_pushinteger(L, v2);
        lua_pushinteger(L, v3);
        return 3;
      }
      lua_pushfstring(L, "%d.%d.%d", v1, v2, v3);
      return 1;
    }

    case ACTL_GETWINDOWINFO:
    case ACTL_GETSHORTWINDOWINFO: {
      struct WindowInfo wi;
      memset(&wi, 0, sizeof(wi));
      wi.Pos = luaL_checkinteger(L,2);

      if (Command == ACTL_GETWINDOWINFO) {
        int r = Info->AdvControl(Info->ModuleNumber, Command, &wi);
        if (!r)
          return lua_pushinteger(L,0), 1;
        wi.TypeName = (wchar_t*)
          lua_newuserdata(L, (wi.TypeNameSize + wi.NameSize) * sizeof(wchar_t));
        wi.Name = wi.TypeName + wi.TypeNameSize;
      }

      int r = Info->AdvControl(Info->ModuleNumber, Command, &wi);
      if (!r)
        return lua_pushinteger(L,0), 1;
      lua_createtable(L,0,4);
      PutIntToTable(L, "Pos", wi.Pos);
      PutIntToTable(L, "Type", wi.Type);
      PutBoolToTable(L, "Modified", wi.Modified);
      PutBoolToTable(L, "Current", wi.Current);
      if (Command == ACTL_GETWINDOWINFO) {
        PutWStrToTable(L, "TypeName", wi.TypeName, -1);
        PutWStrToTable(L, "Name", wi.Name, -1);
      }
      return 1;
    }

    case ACTL_POSTKEYSEQUENCE: {
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_getfield(L, 2, "Flags");
      GetFlagCombination(L, -1, (int*)&ks.Flags);
      ks.Count = lua_objlen(L,2);
      DWORD* sequence = (DWORD*)lua_newuserdata(L, sizeof(DWORD)*ks.Count);
      ks.Sequence = sequence;
      int i;
      for (i=0; i < ks.Count; i++) {
        lua_pushinteger(L,i+1);
        lua_gettable(L,2);
        sequence[i] = lua_tointeger(L,-1);
        lua_pop(L,1);
      }
      Param = &ks;
      break;
    }

    case ACTL_SETARRAYCOLOR:
      luaL_checktype(L, 2, LUA_TTABLE);
      fsc.StartIndex = GetOptIntFromTable(L, "StartIndex", 0);
      lua_getfield(L, 2, "Flags");
      GetFlagCombination(L, -1, (int*)&fsc.Flags);
      fsc.ColorCount = lua_objlen(L, 2);
      fsc.Colors = (BYTE*)lua_newuserdata(L, fsc.ColorCount);
    int i;
      for (i=0; i < fsc.ColorCount; i++) {
        lua_pushinteger(L,i+1);
        lua_gettable(L,2);
        fsc.Colors[i] = lua_tointeger(L,-1);
        lua_pop(L,1);
      }
      Param = &fsc;
      break;

    case ACTL_GETFARRECT:
      if (!Info->AdvControl(Info->ModuleNumber, Command, &sr))
        return 0;
      lua_createtable(L, 0, 4);
      PutIntToTable(L, "Left",   sr.Left);
      PutIntToTable(L, "Top",    sr.Top);
      PutIntToTable(L, "Right",  sr.Right);
      PutIntToTable(L, "Bottom", sr.Bottom);
      return 1;

    case ACTL_GETCURSORPOS:
      if (!Info->AdvControl(Info->ModuleNumber, Command, &coord))
        return 0;
      lua_createtable(L, 0, 2);
      PutIntToTable(L, "X", coord.X);
      PutIntToTable(L, "Y", coord.Y);
      return 1;

    case ACTL_SETCURSORPOS:
      luaL_checktype(L, 2, LUA_TTABLE);
      lua_getfield(L, 2, "X");
      coord.X = lua_tointeger(L, -1);
      lua_getfield(L, 2, "Y");
      coord.Y = lua_tointeger(L, -1);
      Param = &coord;
      break;
  }
  lua_pushinteger(L, Info->AdvControl(Info->ModuleNumber, Command, Param));
  return 1;
}

int far_CPluginStartupInfo(lua_State *L)
{
  return lua_pushlightuserdata(L, (void*)GetPluginStartupInfo(L)), 1;
}

int far_GetTimeZoneInformation (lua_State *L)
{
  TIME_ZONE_INFORMATION tzi;
  DWORD res = GetTimeZoneInformation(&tzi);
  if (res == 0xFFFFFFFF)
    return lua_pushnil(L), 1;

  lua_createtable(L, 0, 5);
  PutNumToTable(L, "Bias", tzi.Bias);
  PutNumToTable(L, "StandardBias", tzi.StandardBias);
  PutNumToTable(L, "DaylightBias", tzi.DaylightBias);
  PutLStrToTable(L, "StandardName", tzi.StandardName, sizeof(WCHAR)*wcslen(tzi.StandardName));
  PutLStrToTable(L, "DaylightName", tzi.DaylightName, sizeof(WCHAR)*wcslen(tzi.DaylightName));

  lua_pushnumber(L, res);
  return 2;
}

void pushSystemTime (lua_State *L, const SYSTEMTIME *st)
{
  lua_createtable(L, 0, 8);
  PutIntToTable(L, "wYear", st->wYear);
  PutIntToTable(L, "wMonth", st->wMonth);
  PutIntToTable(L, "wDayOfWeek", st->wDayOfWeek);
  PutIntToTable(L, "wDay", st->wDay);
  PutIntToTable(L, "wHour", st->wHour);
  PutIntToTable(L, "wMinute", st->wMinute);
  PutIntToTable(L, "wSecond", st->wSecond);
  PutIntToTable(L, "wMilliseconds", st->wMilliseconds);
}

void pushFileTime (lua_State *L, const FILETIME *ft)
{
  long long llFileTime = ft->dwLowDateTime + 0x100000000ll * ft->dwHighDateTime;
  llFileTime /= 10000;
  lua_pushnumber(L, (double)llFileTime);
}

int far_GetSystemTime (lua_State *L)
{
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  pushFileTime(L, &ft);
  return 1;
}

int far_FileTimeToSystemTime (lua_State *L)
{
  FILETIME ft;
  SYSTEMTIME st;
  long long llFileTime = 10000 * (long long) luaL_checknumber(L, 1);
  ft.dwLowDateTime = llFileTime & 0xFFFFFFFF;
  ft.dwHighDateTime = llFileTime >> 32;
  if (! FileTimeToSystemTime(&ft, &st))
    return lua_pushnil(L), 1;
  pushSystemTime(L, &st);
  return 1;
}

int far_SystemTimeToFileTime (lua_State *L)
{
  FILETIME ft;
  SYSTEMTIME st;
  memset(&st, 0, sizeof(st));
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 1);
  st.wYear         = GetOptIntFromTable(L, "wYear", 0);
  st.wMonth        = GetOptIntFromTable(L, "wMonth", 0);
  st.wDayOfWeek    = GetOptIntFromTable(L, "wDayOfWeek", 0);
  st.wDay          = GetOptIntFromTable(L, "wDay", 0);
  st.wHour         = GetOptIntFromTable(L, "wHour", 0);
  st.wMinute       = GetOptIntFromTable(L, "wMinute", 0);
  st.wSecond       = GetOptIntFromTable(L, "wSecond", 0);
  st.wMilliseconds = GetOptIntFromTable(L, "wMilliseconds", 0);
  if (! SystemTimeToFileTime(&st, &ft))
    return lua_pushnil(L), 1;
  pushFileTime(L, &ft);
  return 1;
}

int far_CompareString (lua_State *L)
{
  int len1, len2;
  const wchar_t *ws1  = check_utf8_string(L, 1, &len1);
  const wchar_t *ws2  = check_utf8_string(L, 2, &len2);
  const char *sLocale = luaL_optstring(L, 3, "");
  const char *sFlags  = luaL_optstring(L, 4, "");

  LCID Locale = LOCALE_USER_DEFAULT;
  if      (!strcmp(sLocale, "s")) Locale = LOCALE_SYSTEM_DEFAULT;
  else if (!strcmp(sLocale, "n")) Locale = LOCALE_NEUTRAL;

  DWORD dwFlags = 0;
  if (strchr(sFlags, 'c')) dwFlags |= NORM_IGNORECASE;
  if (strchr(sFlags, 'k')) dwFlags |= NORM_IGNOREKANATYPE;
  if (strchr(sFlags, 'n')) dwFlags |= NORM_IGNORENONSPACE;
  if (strchr(sFlags, 's')) dwFlags |= NORM_IGNORESYMBOLS;
  if (strchr(sFlags, 'w')) dwFlags |= NORM_IGNOREWIDTH;
  if (strchr(sFlags, 'S')) dwFlags |= SORT_STRINGSORT;

  int result = CompareStringW(Locale, dwFlags, ws1, len1, ws2, len2) - 2;
  (result == -2) ? lua_pushnil(L) : lua_pushinteger(L, result);
  return 1;
}

int far_wcscmp (lua_State *L)
{
  const wchar_t *ws1  = check_utf8_string(L, 1, NULL);
  const wchar_t *ws2  = check_utf8_string(L, 2, NULL);
  int insens = lua_toboolean(L, 3);
  lua_pushinteger(L, (insens ? wcsicmp : wcscmp)(ws1, ws2));
  return 1;
}

int far_MakeMenuItems (lua_State *L)
{
  int argn = lua_gettop(L);
  lua_createtable(L, argn, 0);
  if (argn > 0) {
    wchar_t delim[] = { 9474, L'\0' };
    wchar_t fmt1[64], fmt2[64], wbuf[64];
    int maxno = (int)floor(log10(argn)) + 1;
    wsprintfW(fmt1, L"%%%dd%ls ", maxno, delim);
    wsprintfW(fmt2, L"%%%dls%ls ", maxno, delim);
    int item = 1, i;
    for (i=1; i<=argn; i++) {
      lua_getglobal(L, "tostring");
      if (i == 1 && lua_type(L,-1) != LUA_TFUNCTION)
        luaL_error(L, "global `tostring' is not function");
      lua_pushvalue(L, i);
      if (0 != lua_pcall(L, 1, 1, 0))
        luaL_error(L, lua_tostring(L, -1));
      int len;
      wchar_t *str = check_utf8_string(L, -1, &len), *start = str;
      int j;
      for (j=0; j<len; j++)
        if (str[j] == 0) str[j] = L' ';
      do {
        wchar_t* nl = wcschr(start, L'\n');
        if (nl) *nl = L'\0';
        start == str ? wsprintfW(wbuf, fmt1, i) : wsprintfW(wbuf, fmt2, L"");
        lua_newtable(L);
        push_utf8_string(L, wbuf, -1);
        push_utf8_string(L, start, nl ? (nl++) - start : len - (start-str));
        lua_concat(L, 2);
        lua_setfield(L, -2, "text");
        lua_rawseti(L, argn+1, item++);
        start = nl;
      } while (start);
      lua_pop(L, 1);
    }
  }
  return 1;
}

int far_Show (lua_State *L)
{
  int argn = lua_gettop(L);
  far_MakeMenuItems(L);

  const char* f =
  "local items,n=...\n"
  "local bottom=n==0 and 'No arguments' or n==1 and '1 argument' or n..' arguments'\n"
  "far.Menu({Title='',Bottom=bottom,Flags={FMENU_SHOWAMPERSAND=1}},\n"
    "items,{{BreakKey='RETURN'},{BreakKey='SPACE'}})";

  if (luaL_loadstring(L, f) != 0)
    luaL_error(L, lua_tostring(L, -1));
  lua_pushvalue(L, -2);
  lua_pushinteger(L, argn);
  if (lua_pcall(L, 2, 0, 0) != 0)
    luaL_error(L, lua_tostring(L, -1));
  return 0;
}

void NewVirtualKeyTable(lua_State* L, BOOL twoways)
{
  int i;
  lua_createtable(L, 0, twoways ? 360:180);
  for (i=0; i<256; i++) {
    const char* str = VirtualKeyStrings[i];
    if (str != NULL) {
      lua_pushinteger(L, i);
      lua_setfield(L, -2, str);
      if (twoways) {
        lua_pushstring(L, str);
        lua_rawseti(L, -2, i);
      }
    }
  }
}

int far_GetVirtualKeys (lua_State *L)
{
  NewVirtualKeyTable(L, TRUE);
  return 1;
}

HANDLE* CheckFileFilter(lua_State* L, int pos)
{
  return (HANDLE*)luaL_checkudata(L, pos, FarFileFilterType);
}

HANDLE CheckValidFileFilter(lua_State* L, int pos)
{
  HANDLE h = *CheckFileFilter(L, pos);
  luaL_argcheck(L,h != INVALID_HANDLE_VALUE,pos,"attempt to access invalid file filter");
  return h;
}

int far_CreateFileFilter (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE hHandle = (luaL_checkinteger(L,1) % 2) ? PANEL_ACTIVE:PANEL_PASSIVE;
  int filterType = check_env_flag(L,2);
  HANDLE* pOutHandle = (HANDLE*)lua_newuserdata(L, sizeof(HANDLE));
  if (Info->FileFilterControl(hHandle, FFCTL_CREATEFILEFILTER, filterType,
    (LONG_PTR)pOutHandle))
  {
    luaL_getmetatable(L, FarFileFilterType);
    lua_setmetatable(L, -2);
  }
  else
    lua_pushnil(L);
  return 1;
}

int filefilter_Free (lua_State *L)
{
  HANDLE *h = CheckFileFilter(L, 1);
  if (*h != INVALID_HANDLE_VALUE) {
    PSInfo *Info = GetPluginStartupInfo(L);
    lua_pushboolean(L, Info->FileFilterControl(*h, FFCTL_FREEFILEFILTER, 0, 0));
    *h = INVALID_HANDLE_VALUE;
  }
  else
    lua_pushboolean(L,0);
  return 1;
}

int filefilter_gc (lua_State *L)
{
  filefilter_Free(L);
  return 0;
}

int filefilter_tostring (lua_State *L)
{
  HANDLE *h = CheckFileFilter(L, 1);
  if (*h != INVALID_HANDLE_VALUE)
    lua_pushfstring(L, "%s (%p)", FarFileFilterType, h);
  else
    lua_pushfstring(L, "%s (closed)", FarFileFilterType);
  return 1;
}

int filefilter_OpenMenu (lua_State *L)
{
  HANDLE h = CheckValidFileFilter(L, 1);
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushboolean(L, Info->FileFilterControl(h, FFCTL_OPENFILTERSMENU, 0, 0));
  return 1;
}

int filefilter_Starting (lua_State *L)
{
  HANDLE h = CheckValidFileFilter(L, 1);
  PSInfo *Info = GetPluginStartupInfo(L);
  lua_pushboolean(L, Info->FileFilterControl(h, FFCTL_STARTINGTOFILTER, 0, 0));
  return 1;
}

int filefilter_IsFileInFilter (lua_State *L)
{
  struct FAR_FIND_DATA ffd;
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE h = CheckValidFileFilter(L, 1);
  luaL_checktype(L, 2, LUA_TTABLE);
  lua_settop(L, 2);         // +2
  GetFarFindData(L, &ffd);  // +4
  lua_pushboolean(L, Info->FileFilterControl(h, FFCTL_ISFILEINFILTER, 0, (LONG_PTR)&ffd));
  return 1;
}

int far_PluginsControl (lua_State *L)
{
  PSInfo *Info = GetPluginStartupInfo(L);
  HANDLE handle = OptHandle (L, 1);
  int command = check_env_flag(L, 2);
  int param1;
  LONG_PTR param2;
  if (command==PCTL_LOADPLUGIN || command==PCTL_UNLOADPLUGIN ||
      command==PCTL_FORCEDLOADPLUGIN)
  {
    param1 = check_env_flag(L, 3);
    param2 = (LONG_PTR)check_utf8_string(L, 4, NULL);
    lua_pushboolean(L, Info->PluginsControl(handle, command, param1, param2));
  }
  else
    lua_pushnil(L);
  return 1;
}

int far_XLat (lua_State *L)
{
  int size;
  wchar_t *Line = check_utf8_string(L, 1, &size);
  int StartPos = luaL_checkinteger(L, 2) - 1;
  int EndPos = luaL_checkinteger(L, 3);
  DWORD Flags = CheckFlags(L, 4);

  if (StartPos < 0) StartPos = 0;
  if (EndPos > size) EndPos = size;
  if (StartPos > EndPos)
    return lua_pushnil(L), 1;

  PSInfo *Info = GetPluginStartupInfo(L);
  wchar_t* str = Info->FSF->XLat(Line, StartPos, EndPos, Flags);
  str ? (void)push_utf8_string(L, str, -1) : lua_pushnil(L);
  return 1;
}

DWORD WINAPI TimerThreadFunc (LPVOID data)
{
  FILETIME tCurrent;
  LARGE_INTEGER lStart, lCurrent;
  TSynchroData *sd;
  TTimerData* td = (TTimerData*)data;

  int interval_copy = td->interval;
  td->interval_changed = 0;

  memcpy(&lStart, &td->tStart, sizeof(lStart));
  for ( ; !td->needClose; lStart.QuadPart += interval_copy * 10000ll) {
    while (!td->needClose) {
      GetSystemTimeAsFileTime(&tCurrent);
      memcpy(&lCurrent, &tCurrent, sizeof(lCurrent));
      if (td->interval_changed) {
        interval_copy = td->interval;
        lStart.QuadPart = lCurrent.QuadPart;
        td->interval_changed = 0;
      }
      long long llElapsed = lCurrent.QuadPart - lStart.QuadPart;
      int iElapsed = llElapsed / 10000;
      if (iElapsed + 5 < interval_copy) {
        Sleep(10);
      }
      else break;
    }
    if (!td->needClose && td->enabled) {
      sd = (TSynchroData*) malloc(sizeof(TSynchroData));
      InitSynchroData(sd, LUAFAR_TIMER_CALL, td->objRef, td->funcRef, 0);
      td->Info->AdvControl(td->Info->ModuleNumber, ACTL_SYNCHRO, sd);
    }
  }
  sd = (TSynchroData*) malloc(sizeof(TSynchroData));
  InitSynchroData(sd, LUAFAR_TIMER_UNREF, td->objRef, td->funcRef, 0);
  td->Info->AdvControl(td->Info->ModuleNumber, ACTL_SYNCHRO, sd);
  return 0;
}

int far_Timer (lua_State *L)
{
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft); // do this immediately, for greater accuracy

  TTimerData *td = (TTimerData*)lua_newuserdata(L, sizeof(TTimerData));
  td->Info = GetPluginStartupInfo(L);
  {
    FARPROC proc = NULL;
    HINSTANCE hnd = LoadLibraryW(td->Info->ModuleName);
    if (hnd) {
      proc = GetProcAddress(hnd, "ProcessSynchroEventW");
      FreeLibrary(hnd);
    }
    if (proc == NULL) luaL_error(L, "ProcessSynchroEventW not available");
  }
  td->interval = luaL_checkinteger(L, 1);
  if (td->interval < 0) td->interval = 0;

  lua_pushvalue(L, -1);
  td->objRef = luaL_ref(L, LUA_REGISTRYINDEX);

  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  td->funcRef = luaL_ref(L, LUA_REGISTRYINDEX);

  td->tStart = ft;
  td->interval_changed = 1;
  td->needClose = 0;
  td->enabled = 1;

  td->hThread = CreateThread(NULL, 0, TimerThreadFunc, td, 0, &td->threadId);
  if (td->hThread != NULL) {
    luaL_getmetatable(L, FarTimerType);
    lua_setmetatable(L, -2);
    return 1;
  }
  luaL_unref(L, LUA_REGISTRYINDEX, td->objRef);
  luaL_unref(L, LUA_REGISTRYINDEX, td->funcRef);
  return lua_pushnil(L), 1;
}

TTimerData* CheckTimer(lua_State* L, int pos)
{
  return (TTimerData*)luaL_checkudata(L, pos, FarTimerType);
}

TTimerData* CheckValidTimer(lua_State* L, int pos)
{
  TTimerData* td = CheckTimer(L, pos);
  luaL_argcheck(L, td->needClose == 0, pos, "attempt to access closed timer");
  return td;
}

int timer_Close (lua_State *L)
{
  TTimerData* td = CheckTimer(L, 1);
  td->needClose = 1;
  return 0;
}

int timer_tostring (lua_State *L)
{
  TTimerData* td = CheckTimer(L, 1);
  if (td->needClose == 0)
    lua_pushfstring(L, "%s (%p)", FarTimerType, td);
  else
    lua_pushfstring(L, "%s (closed)", FarTimerType);
  return 1;
}

int timer_index (lua_State *L)
{
  TTimerData* td = CheckTimer(L, 1);
  const char* method = luaL_checkstring(L, 2);
  if      (!strcmp(method, "Close"))       lua_pushcfunction(L, timer_Close);
  else if (!strcmp(method, "Enabled"))     lua_pushboolean(L, td->enabled);
  else if (!strcmp(method, "Interval")) {
    while (td->interval_changed)
      SwitchToThread();
    lua_pushinteger(L, td->interval);
  }
  else if (!strcmp(method, "OnTimer"))     lua_rawgeti(L, LUA_REGISTRYINDEX, td->funcRef);
  else if (!strcmp(method, "Closed"))      lua_pushboolean(L, td->needClose);
  else                                     luaL_error(L, "attempt to call non-existent method");
  return 1;
}

int timer_newindex (lua_State *L)
{
  TTimerData* td = CheckValidTimer(L, 1);
  const char* method = luaL_checkstring(L, 2);
  if (!strcmp(method, "Enabled")) {
    luaL_checkany(L, 3);
    td->enabled = lua_toboolean(L, 3);
  }
  else if (!strcmp(method, "Interval")) {
    int interval = luaL_checkinteger(L, 3);
    if (interval < 0) interval = 0;
    while (td->interval_changed)
      SwitchToThread();
    td->interval = interval;
    td->interval_changed = 1;
  }
  else if (!strcmp(method, "OnTimer")) {
    luaL_checktype(L, 3, LUA_TFUNCTION);
    lua_pushvalue(L, 3);
    lua_rawseti(L, LUA_REGISTRYINDEX, td->funcRef);
  }
  else luaL_error(L, "attempt to call non-existent method");
  return 0;
}

int far_GetConsoleScreenBufferInfo (lua_State* L)
{
  CONSOLE_SCREEN_BUFFER_INFO info;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleScreenBufferInfo(h, &info))
    return lua_pushnil(L), 1;
  lua_createtable(L, 0, 11);
  PutIntToTable(L, "dwSizeX",              info.dwSize.X);
  PutIntToTable(L, "dwSizeY",              info.dwSize.Y);
  PutIntToTable(L, "dwCursorPositionX",    info.dwCursorPosition.X);
  PutIntToTable(L, "dwCursorPositionY",    info.dwCursorPosition.Y);
  PutIntToTable(L, "wAttributes",          info.wAttributes);
  PutIntToTable(L, "srWindowLeft",         info.srWindow.Left);
  PutIntToTable(L, "srWindowTop",          info.srWindow.Top);
  PutIntToTable(L, "srWindowRight",        info.srWindow.Right);
  PutIntToTable(L, "srWindowBottom",       info.srWindow.Bottom);
  PutIntToTable(L, "dwMaximumWindowSizeX", info.dwMaximumWindowSize.X);
  PutIntToTable(L, "dwMaximumWindowSizeY", info.dwMaximumWindowSize.Y);
  return 1;
}

int far_CopyFile (lua_State *L)
{
  const wchar_t* src = check_utf8_string(L, 1, NULL);
  const wchar_t* trg = check_utf8_string(L, 2, NULL);

  BOOL fail_if_exists = FALSE; // default = overwrite the target
  if(lua_gettop(L) > 2)
    fail_if_exists = lua_toboolean(L,3);

  if (CopyFileW(src, trg, fail_if_exists))
    return lua_pushboolean(L, 1), 1;
  return SysErrorReturn(L);
}

int far_MoveFile (lua_State *L)
{
  const wchar_t* src = check_utf8_string(L, 1, NULL);
  const wchar_t* trg = check_utf8_string(L, 2, NULL);
  const char* sFlags = luaL_optstring(L, 3, NULL);
  int flags = 0;
  if (sFlags) {
    if      (strchr(sFlags, 'c')) flags |= MOVEFILE_COPY_ALLOWED;
    else if (strchr(sFlags, 'd')) flags |= MOVEFILE_DELAY_UNTIL_REBOOT;
    else if (strchr(sFlags, 'r')) flags |= MOVEFILE_REPLACE_EXISTING;
    else if (strchr(sFlags, 'w')) flags |= MOVEFILE_WRITE_THROUGH;
  }
  if (MoveFileExW(src, trg, flags))
    return lua_pushboolean(L, 1), 1;
  return SysErrorReturn(L);
}

int far_DeleteFile (lua_State *L)
{
  if (DeleteFileW(check_utf8_string(L, 1, NULL)))
    return lua_pushboolean(L, 1), 1;
  return SysErrorReturn(L);
}

BOOL dir_exist(const wchar_t* path)
{
  DWORD attr = GetFileAttributesW(path);
  return (attr != 0xFFFFFFFF) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL mkdir (const wchar_t* path)
{
  BOOL result = FALSE;
  const wchar_t* src = path;
  wchar_t *p = wcsdup(path), *trg = p;
  while (*src) {
    if (*src == L'\\' || *src == L'/') {
      *trg++ = L'\\';
      do src++; while (*src == L'\\' || *src == L'/');
    }
    else *trg++ = *src++;
  }
  if (trg > p && trg[-1] == '\\') trg--;
  *trg = 0;

  wchar_t* q;
  for (q=p; *q; *q++=L'\\') {
    q = wcschr(q, L'\\');
    if (q != NULL)  *q = 0;
    if (q != p && !dir_exist(p) && !CreateDirectoryW(p, NULL)) break;
    if (q == NULL) { result=TRUE; break; }
  }
  free(p);
  return result;
}

int far_CreateDir (lua_State *L)
{
  const wchar_t* path = check_utf8_string(L, 1, NULL);
  BOOL tolerant = lua_toboolean(L, 2);
  if (dir_exist(path)) {
    if (tolerant) return lua_pushboolean(L,1), 1;
    return lua_pushnil(L), lua_pushliteral(L, "directory already exists"), 2;
  }
  if (mkdir(path))
    return lua_pushboolean(L, 1), 1;
  return SysErrorReturn(L);
}

int far_RemoveDir (lua_State *L)
{
  if (RemoveDirectoryW(check_utf8_string(L, 1, NULL)))
    return lua_pushboolean(L, 1), 1;
  return SysErrorReturn(L);
}

const luaL_reg timer_methods[] = {
  {"__gc",             timer_Close},
  {"__tostring",       timer_tostring},
  {"__index",          timer_index},
  {"__newindex",       timer_newindex},
  {NULL, NULL},
};

const luaL_reg filefilter_methods[] = {
  {"__gc",             filefilter_gc},
  {"__tostring",       filefilter_tostring},
  {"FreeFileFilter",   filefilter_Free},
  {"OpenFiltersMenu",  filefilter_OpenMenu},
  {"StartingToFilter", filefilter_Starting},
  {"IsFileInFilter",   filefilter_IsFileInFilter},
  {NULL, NULL},
};

const luaL_reg dialog_methods[] = {
  {"__gc",             dialog_gc},
  {"__tostring",       dialog_tostring},
  {NULL, NULL},
};

const luaL_reg far_funcs[] = {
  {"PluginStartupInfo",   far_PluginStartupInfo},

  {"EditorAddColor",      far_EditorAddColor},
  {"EditorAddStackBookmark",    far_EditorAddStackBookmark},
  {"EditorClearStackBookmarks", far_EditorClearStackBookmarks},
  {"EditorDeleteStackBookmark", far_EditorDeleteStackBookmark},
  {"EditorGetStackBookmarks",   far_EditorGetStackBookmarks},
  {"EditorNextStackBookmark",   far_EditorNextStackBookmark},
  {"EditorPrevStackBookmark",   far_EditorPrevStackBookmark},
  {"EditorDeleteBlock",   far_EditorDeleteBlock},
  {"EditorDeleteChar",    far_EditorDeleteChar},
  {"EditorDeleteString",  far_EditorDeleteString},
  {"EditorExpandTabs",    far_EditorExpandTabs},
  {"EditorGetBookmarks",  far_EditorGetBookmarks},
  {"EditorGetColor",      far_EditorGetColor},
  {"EditorGetInfo",       far_EditorGetInfo},
  {"EditorGetString",     far_EditorGetString},
  {"EditorInsertString",  far_EditorInsertString},
  {"EditorInsertText",    far_EditorInsertText},
  {"EditorProcessInput",  far_EditorProcessInput},
  {"EditorProcessKey",    far_EditorProcessKey},
  {"EditorQuit",          far_EditorQuit},
  {"EditorReadInput",     far_EditorReadInput},
  {"EditorRealToTab",     far_EditorRealToTab},
  {"EditorRedraw",        far_EditorRedraw},
  {"EditorSaveFile",      far_EditorSaveFile},
  {"EditorSelect",        far_EditorSelect},
  {"EditorSetKeyBar",     far_EditorSetKeyBar},
  {"EditorSetParam",      far_EditorSetParam},
  {"EditorSetPosition",   far_EditorSetPosition},
  {"EditorSetString",     far_EditorSetString},
  {"EditorSetTitle",      far_EditorSetTitle},
  {"EditorTabToReal",     far_EditorTabToReal},
  {"EditorTurnOffMarkingBlock", far_EditorTurnOffMarkingBlock},
  {"EditorGetFileName",   far_EditorGetFileName},
  {"EditorUndoRedo",      far_EditorUndoRedo},

  {"CmpName",             far_CmpName},

//{"Control",             far_Control}, // done as multiple functions
  {"CtrlCheckPanelsExist",far_CtrlCheckPanelsExist},
  {"CtrlClosePlugin",     far_CtrlClosePlugin},

  {"CtrlGetCmdLine",      far_CtrlGetCmdLine},
  {"CtrlGetCmdLinePos",   far_CtrlGetCmdLinePos},
  {"CtrlGetCmdLineSelection", far_CtrlGetCmdLineSelection},
  {"CtrlInsertCmdLine",   far_CtrlInsertCmdLine},
  {"CtrlSetCmdLine",      far_CtrlSetCmdLine},
  {"CtrlSetCmdLinePos",   far_CtrlSetCmdLinePos},
  {"CtrlSetCmdLineSelection", far_CtrlSetCmdLineSelection},

  {"CtrlGetPanelInfo",    far_CtrlGetPanelInfo},
  {"CtrlGetUserScreen",   far_CtrlGetUserScreen},
  {"CtrlRedrawPanel",     far_CtrlRedrawPanel},
  {"CtrlSetNumericSort",  far_CtrlSetNumericSort},
  {"CtrlSetCaseSensitiveSort",  far_CtrlSetCaseSensitiveSort},
  {"CtrlSetPanelDir",     far_CtrlSetPanelDir},
  {"CtrlSetSelection",    far_CtrlSetSelection},
  {"CtrlClearSelection",  far_CtrlClearSelection},
  {"CtrlSetSortMode",     far_CtrlSetSortMode},
  {"CtrlSetSortOrder",    far_CtrlSetSortOrder},
  {"CtrlSetUserScreen",   far_CtrlSetUserScreen},
  {"CtrlSetViewMode",     far_CtrlSetViewMode},
  {"CtrlUpdatePanel",     far_CtrlUpdatePanel},
  {"CtrlGetPanelItem",    far_CtrlGetPanelItem},
  {"CtrlGetSelectedPanelItem", far_CtrlGetSelectedPanelItem},
  {"CtrlGetCurrentPanelItem",  far_CtrlGetCurrentPanelItem},
  {"CtrlGetPanelDir",     far_CtrlGetPanelDir},
  {"CtrlGetPanelFormat",  far_CtrlGetPanelFormat},
  {"CtrlGetPanelHostFile",far_CtrlGetPanelHostFile},
  {"CtrlGetColumnTypes",  far_CtrlGetColumnTypes},
  {"CtrlGetColumnWidths", far_CtrlGetColumnWidths},
  {"CtrlIsActivePanel",   far_CtrlIsActivePanel},

  {"DialogInit",          far_DialogInit},
  {"DialogRun",           far_DialogRun},
  {"DialogFree",          far_DialogFree},
  {"SendDlgMessage",      far_SendDlgMessage},
  {"GetDlgItem",          far_GetDlgItem},
  {"SetDlgItem",          far_SetDlgItem},
  {"Editor",              far_Editor},
  {"GetDirList",          far_GetDirList},
  {"GetMsg",              far_GetMsg},
  {"GetPluginDirList",    far_GetPluginDirList},
  {"Menu",                far_Menu},
  {"Message",             far_Message},
  {"RestoreScreen",       far_RestoreScreen},
  {"SaveScreen",          far_SaveScreen},
  {"Text",                far_Text},
  {"Viewer",              far_Viewer},
  {"ViewerGetInfo",       far_ViewerGetInfo},
  {"ViewerQuit",          far_ViewerQuit},
  {"ViewerRedraw",        far_ViewerRedraw},
  {"ViewerSelect",        far_ViewerSelect},
  {"ViewerSetKeyBar",     far_ViewerSetKeyBar},
  {"ViewerSetPosition",   far_ViewerSetPosition},
  {"ViewerSetMode",       far_ViewerSetMode},
  {"ShowHelp",            far_ShowHelp},
  {"InputBox",            far_InputBox},
  {"AdvControl",          far_AdvControl},
  {"DefDlgProc",          far_DefDlgProc},
  {"CreateFileFilter",    far_CreateFileFilter},
  {"PluginsControl",      far_PluginsControl},

  /* FUNCTIONS ADDED FOR VARIOUS REASONS */
  {"SetRegKey",           far_SetRegKey},
  {"GetRegKey",           far_GetRegKey},
  {"DeleteRegKey",        far_DeleteRegKey},
  {"EditorGetSelection",  far_EditorGetSelection},
  {"ExtractKey",          far_ExtractKey},
  {"GetFlags",            far_GetFlags},
  {"CopyToClipboard",     far_CopyToClipboard},
  {"PasteFromClipboard",  far_PasteFromClipboard},
  {"FarKeyToName",        far_FarKeyToName},
  {"FarNameToKey",        far_FarNameToKey},
  {"FarInputRecordToKey", far_FarInputRecordToKey},
  {"LStricmp",            far_LStricmp},
  {"LStrnicmp",           far_LStrnicmp},
  {"ProcessName",         far_ProcessName},
  {"GetPathRoot",         far_GetPathRoot},
  {"GetReparsePointInfo", far_GetReparsePointInfo},
  {"LIsAlpha",            far_LIsAlpha},
  {"LIsAlphanum",         far_LIsAlphanum},
  {"LIsLower",            far_LIsLower},
  {"LIsUpper",            far_LIsUpper},
  {"LLowerBuf",           far_LLowerBuf},
  {"LUpperBuf",           far_LUpperBuf},
  {"MkTemp",              far_MkTemp},
  {"MkLink",              far_MkLink},
  {"TruncPathStr",        far_TruncPathStr},
  {"TruncStr",            far_TruncStr},
  {"FarRecursiveSearch",  far_FarRecursiveSearch},
  {"ConvertPath",         far_ConvertPath},
  {"XLat",                far_XLat},

  {"CPluginStartupInfo",  far_CPluginStartupInfo},
  {"GetEnv",              far_GetEnv},
  {"SetEnv",              far_SetEnv},
  {"GetCurrentDirectory", far_GetCurrentDirectory},
  {"GetFileOwner",        far_GetFileOwner},
  {"GetNumberOfLinks",    far_GetNumberOfLinks},
  {"LuafarVersion",       far_LuafarVersion},
  {"GetTimeZoneInformation", far_GetTimeZoneInformation},
  {"GetFileInfo",         far_GetFileInfo},
  {"FileTimeToSystemTime",far_FileTimeToSystemTime},
  {"SystemTimeToFileTime",far_SystemTimeToFileTime},
  {"GetSystemTime",       far_GetSystemTime},
  {"CompareString",       far_CompareString},
  {"wcscmp",              far_wcscmp},
  {"MakeMenuItems",       far_MakeMenuItems},
  {"Show",                far_Show},
  {"GetVirtualKeys",      far_GetVirtualKeys},
  {"LoadFile",            luaB_loadfileW},
  {"Timer",               far_Timer},
  {"GetConsoleScreenBufferInfo", far_GetConsoleScreenBufferInfo},
  {"CopyFile",            far_CopyFile},
  {"DeleteFile",          far_DeleteFile},
  {"MoveFile",            far_MoveFile},
  {"RenameFile",          far_MoveFile}, // alias
  {"CreateDir",           far_CreateDir},
  {"RemoveDir",           far_RemoveDir},
  {"regex",               far_Regex},
  {"gsub",                far_Gsub},
  {"gmatch",              far_Gmatch},
  {"find",                far_Find},
  {"match",               far_Match},
  {"EnumSystemCodePages", ustring_EnumSystemCodePages },
  {"GetACP",              ustring_GetACP},
  {"GetCPInfo",           ustring_GetCPInfo},
  {"GetDriveType",        ustring_GetDriveType},
  {"GetLogicalDriveStrings",ustring_GetLogicalDriveStrings},
  {"GetOEMCP",            ustring_GetOEMCP},
  {"MultiByteToWideChar", ustring_MultiByteToWideChar },
  {"OemToUtf8",           ustring_OemToUtf8},
  {"Utf16ToUtf8",         ustring_Utf16ToUtf8},
  {"Utf8ToOem",           ustring_Utf8ToOem},
  {"Utf8ToUtf16",         ustring_Utf8ToUtf16},

  {NULL, NULL}
};

const char far_Dialog[] =
"function far.Dialog (X1,Y1,X2,Y2,HelpTopic,Items,Flags,DlgProc)\n\
  local hDlg = far.DialogInit(X1,Y1,X2,Y2,HelpTopic,Items,Flags,DlgProc)\n\
  if hDlg == nil then return nil end\n\
\n\
  local ret = far.DialogRun(hDlg)\n\
  for i, item in ipairs(Items) do\n\
    local newitem = far.GetDlgItem(hDlg, i-1)\n\
    if type(item[7]) == 'table' then\n\
      item[7].SelectIndex = newitem[7].SelectIndex\n\
    else\n\
      item[7] = newitem[7]\n\
    end\n\
    item[10] = newitem[10]\n\
  end\n\
\n\
  far.DialogFree(hDlg)\n\
  return ret\n\
end";

int luaopen_far (lua_State *L)
{
  NewVirtualKeyTable(L, FALSE);
  lua_setfield(L, LUA_REGISTRYINDEX, FAR_VIRTUALKEYS);
  push_flags_table (L);
  lua_replace (L, LUA_ENVIRONINDEX);

  luaL_register(L, "far", far_funcs);
  luaopen_regex(L);

  luaL_newmetatable(L, FarFileFilterType);
  lua_pushvalue(L,-1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, filefilter_methods);

  luaL_newmetatable(L, FarTimerType);
  luaL_register(L, NULL, timer_methods);

  luaL_newmetatable(L, FarDialogType);
  lua_pushvalue(L,-1);
  lua_setfield(L, -2, "__index");
  luaL_register(L, NULL, dialog_methods);

  (void) luaL_dostring(L, far_Dialog);
  return 0;
}

// Run default script
BOOL LF_RunDefaultScript(lua_State* L)
{
  int pos = lua_gettop (L);

  // First: try to load the default script embedded into the plugin
  lua_getglobal(L, "require");
  lua_pushliteral(L, "<boot");
  int status = lua_pcall(L,1,1,0);
  if (status == 0) {
    status = pcall_msg(L,0,0);
    lua_settop (L, pos);
    return (status == 0);
  }

  // Second: try to load the default script from a disk file
  PSInfo *Info = GetPluginStartupInfo(L);
  wchar_t* defscript = (wchar_t*)lua_newuserdata (L,
    sizeof(wchar_t) * (wcslen(Info->ModuleName) + 5));
  wcscpy(defscript, Info->ModuleName);

  FILE *fp = NULL;
  const wchar_t delims[] = L".-";
  int i;
  for (i=0; delims[i]; i++) {
    wchar_t *end = wcsrchr(defscript, delims[i]);
    if (end) {
      wcscpy(end, L".lua");
      if ((fp = _wfopen(defscript, L"r")) != NULL)
        break;
    }
  }
  if (fp) {
    fclose(fp);
    status = LF_LoadFile(L, defscript);
    if (status == 0)
      status = pcall_msg(L,0,0);
    else
      LF_Error(L, utf8_to_utf16 (L, -1, NULL));
  }
  else
    LF_Error(L, L"Default script not found");

  lua_settop (L, pos);
  return (status == 0);
}

void ProcessEnvVars (lua_State *L, const wchar_t* aEnvPrefix, PSInfo *aInfo)
{
  wchar_t bufName[256];
  const int VALSIZE = 16384;
  wchar_t* bufVal = NULL;

  if (aEnvPrefix && wcslen(aEnvPrefix) <=  DIM(bufName) - 8)
    bufVal = (wchar_t*)lua_newuserdata(L, VALSIZE*sizeof(wchar_t));

  if (bufVal) {
    wcscpy(bufName, aEnvPrefix);
    wcscat(bufName, L"_PATH");
    int size = GetEnvironmentVariableW (bufName, bufVal, VALSIZE);
    if (size && size < VALSIZE) {
      lua_getglobal(L, "package");
      push_utf8_string(L, bufVal, -1);
      lua_setfield(L, -2, "path");
      lua_pop(L,1);
    }
  }

  // prepend <plugin directory>\?.lua; to package.path
  const wchar_t* p = aInfo->ModuleName;
  lua_getglobal(L, "package");  //+1
  push_utf8_string(L, p, wcsrchr(p, L'\\') + 1 - p); //+2
  lua_pushliteral(L, "?.lua;"); //+3
  lua_getfield(L, -3, "path");  //+4
  lua_concat(L, 3);             //+2
  lua_setfield(L, -2, "path");  //+1
  lua_pop(L, 1);

  if (bufVal) {
    wcscpy(bufName, aEnvPrefix);
    wcscat(bufName, L"_CPATH");
    int size = GetEnvironmentVariableW (bufName, bufVal, VALSIZE);
    if (size && size < VALSIZE) {
      lua_getglobal(L, "package");
      push_utf8_string(L, bufVal, -1);
      lua_setfield(L, -2, "cpath");
      lua_pop(L,1);
    }

    wcscpy(bufName, aEnvPrefix);
    wcscat(bufName, L"_INIT");
    size = GetEnvironmentVariableW (bufName, bufVal, VALSIZE);
    if (size && size < VALSIZE) {
      int status;
      if (*bufVal==L'@') {
        status = LF_LoadFile(L, bufVal+1) || lua_pcall(L,0,0,0);
      }
      else {
        push_utf8_string(L, bufVal, -1);
        status = luaL_loadstring(L, lua_tostring(L,-1)) || lua_pcall(L,0,0,0);
        lua_remove(L, status ? -2 : -1);
      }
      if (status) {
        LF_Error (L, check_utf8_string(L, -1, NULL));
        lua_pop(L,1);
      }
    }

    lua_pop(L, 1); // bufVal
  }
}

void LF_InitLuaState (lua_State *L, PSInfo *aInfo,
                      lua_CFunction aOpenLibs, const wchar_t* aEnvPrefix)
{
  // place pointer to PSInfo in the L's registry -
  // DON'T MAKE IT GLOBAL IN THIS DLL!
  lua_pushlightuserdata(L, aInfo);
  lua_setfield(L, LUA_REGISTRYINDEX, FAR_KEYINFO);

  // open Lua libraries
  if (aOpenLibs) aOpenLibs(L);
  else luaL_openlibs(L);

  // open "uio", "far", "bit" and "unicode" libraries
  lua_pushcfunction(L, luaopen_uio);
  lua_call(L, 0, 0);
  lua_pushcfunction(L, luaopen_far);
  lua_call(L, 0, 0);
  lua_pushcfunction(L, luaopen_bit);
  lua_call(L, 0, 0);
  lua_pushcfunction(L, luaopen_unicode);
  lua_call(L, 0, 0);

  // open "upackage" library in "far" namespace
  lua_pushcfunction(L, luaopen_upackage);
  lua_pushliteral(L, "far");
  lua_call(L, 1, 0);

  ProcessEnvVars(L, aEnvPrefix, aInfo);
}

// Initialize the interpreter
lua_State* LF_LuaOpen (PSInfo *aInfo, lua_CFunction aOpenLibs,
                       const wchar_t* aEnvPrefix)
{
  if (gInfo.StructSize == 0) {
    gInfo = *aInfo;
    gFSF = *aInfo->FSF;
    gInfo.FSF = &gFSF;
  }

  // create Lua State
  lua_State *L = lua_open();
  if (L == NULL) return NULL;

  LF_InitLuaState(L, aInfo, aOpenLibs, aEnvPrefix);
  return L;
}
