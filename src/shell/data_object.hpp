#pragma once
#include "folder.hpp"
#include <vector>
namespace crackhood {
HRESULT CreateDataObject(std::vector<Node>,REFIID,void**,DWORD effect=DROPEFFECT_COPY);
HRESULT CreateDropTarget(Node,REFIID,void**);
HRESULT CreateContextMenu(Node parent,std::vector<Node>,PCIDLIST_ABSOLUTE absolute,REFIID,void**);
}
