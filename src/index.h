#pragma once
#include "common.h"

// "Tags" entry in the Explorer navigation pane, like the Tags section of the Finder sidebar.
// Backed by a real folder (%LOCALAPPDATA%\FolderTags\Tags) holding one sub-folder per color
// with shortcuts to every tagged folder.
namespace index {

std::wstring DataDir();  // %LOCALAPPDATA%\FolderTags
std::wstring IconPath(const wchar_t* name);
std::wstring OverlayIconPath(int tag);
// Hidden folder pre-tagged with `tag`; querying its icon makes a process load that overlay image.
std::wstring WarmupDir(int tag);

void Update(const std::wstring& path, unsigned oldMask, unsigned newMask);

// Creates the current user's icons and Tags folder tree, and registers the navigation-pane
// entry for every user (HKLM).
HRESULT Setup();
// Creates the current user's icons and Tags folder tree if missing (users other than the one
// who installed FolderTags). Runs once per process.
void EnsureUserData();
void Teardown();

}  // namespace index
