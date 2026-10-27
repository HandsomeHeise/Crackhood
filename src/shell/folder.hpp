#pragma once
#include <windows.h>
#include <shlobj.h>
#include "core/model.hpp"
namespace crackhood {
inline constexpr CLSID CLSID_Crackhood={0x5b4b37d2,0xed9f,0x4a50,{0x8a,0x76,0x8d,0xfe,0x13,0xda,0x73,0x18}};
HRESULT CreateFolder(const Node&,REFIID,void**);
void InvalidateFolder(ConsoleId,RemotePath);
inline long objects=0;
}
