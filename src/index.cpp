#include "index.h"
#include "draw.h"
#include "i18n.h"
#include "tags.h"
#include <knownfolders.h>
#include <propkey.h>
#include <cmath>

using namespace Gdiplus;

namespace index {

namespace {

const wchar_t kNavClsid[] = L"{B6E4CD56-987A-4C8F-8729-7FD3D4D8EBF0}";

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

std::wstring TagsRoot() { return DataDir() + L"\\Tags"; }
// Stable on-disk name ("blue"); the localized name is shown through desktop.ini.
std::wstring ColorDir(int tag) { return TagsRoot() + L"\\" + tags::kTags[tag].id; }

std::wstring Lower(std::wstring s) {
    CharLowerBuffW(s.data(), (DWORD)s.size());
    return s;
}

// ---------- Icons (Standard 32-bit DIB .ico generated with GDI+) ----------

using Painter = void (*)(Graphics&, int size, int tag);

// Real system DPI (regsvr32 is DPI-unaware, so ask from a system-aware thread context).
int SystemDpi() {
    const DPI_AWARENESS_CONTEXT old = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    const UINT dpi = GetDpiForSystem();
    if (old) SetThreadDpiAwarenessContext(old);
    return dpi ? (int)dpi : 96;
}

// Shell image-list sizes at the current DPI: small (16), large (32), extra-large (48) logical px.
int SmallListSize() { return MulDiv(16, SystemDpi(), 96); }
int LargeListSize() { return MulDiv(32, SystemDpi(), 96); }
int XLargeListSize() { return MulDiv(48, SystemDpi(), 96); }

bool SaveIco(const std::wstring& file, Painter paint, int tag) {
    // Exact frames for the three shell image lists, so Windows never rescales the overlay.
    std::vector<int> sizes = {16, 20, 24, 32, 40, 48, 60, 64, 72, 96, 128};
    for (int s : {SmallListSize(), LargeListSize(), XLargeListSize()})
        if (s < 256 && std::find(sizes.begin(), sizes.end(), s) == sizes.end()) sizes.push_back(s);
    std::sort(sizes.begin(), sizes.end());
    const WORD n = (WORD)sizes.size();

    struct Img {
        int s;
        std::vector<BYTE> data;
    };
    std::vector<Img> images;

    for (int s : sizes) {
        Bitmap bmp(s, s, PixelFormat32bppARGB);
        {
            Graphics g(&bmp);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.Clear(Color(0, 0, 0, 0));
            paint(g, s, tag);
        }

        BITMAPINFOHEADER bih = {};
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = s;
        bih.biHeight = s * 2; // XOR + AND mask
        bih.biPlanes = 1;
        bih.biBitCount = 32;
        bih.biCompression = BI_RGB;
        bih.biSizeImage = s * s * 4;

        BitmapData bd = {};
        Rect rect(0, 0, s, s);
        bmp.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bd);

        int andStride = ((s + 31) / 32) * 4;
        size_t totalImgSize = sizeof(BITMAPINFOHEADER) + (size_t)s * s * 4 + (size_t)andStride * s;
        std::vector<BYTE> imgData(totalImgSize, 0);

        memcpy(imgData.data(), &bih, sizeof(bih));
        BYTE* xorDest = imgData.data() + sizeof(bih);

        // Bottom-up BGRA
        for (int y = s - 1; y >= 0; --y) {
            const BYTE* rowSrc = (const BYTE*)bd.Scan0 + (y * bd.Stride);
            memcpy(xorDest + (size_t)(s - 1 - y) * s * 4, rowSrc, s * 4);
        }

        // AND mask (bottom-up, 1 = transparent, 0 = opaque)
        BYTE* andDest = xorDest + (size_t)s * s * 4;
        for (int y = s - 1; y >= 0; --y) {
            BYTE* andRow = andDest + (size_t)(s - 1 - y) * andStride;
            const BYTE* rowSrc = (const BYTE*)bd.Scan0 + (y * bd.Stride);
            for (int x = 0; x < s; ++x) {
                BYTE a = rowSrc[x * 4 + 3]; // Alpha channel
                if (a < 128) {
                    andRow[x / 8] |= (BYTE)(0x80 >> (x % 8));
                }
            }
        }

        bmp.UnlockBits(&bd);
        images.push_back({s, std::move(imgData)});
    }

    std::vector<BYTE> ico;
    auto put16 = [&](WORD v) { ico.push_back(LOBYTE(v)); ico.push_back(HIBYTE(v)); };
    auto put32 = [&](DWORD v) { put16(LOWORD(v)); put16(HIWORD(v)); };

    put16(0); // reserved
    put16(1); // type 1 = icon
    put16(n); // count

    DWORD offset = 6 + 16 * n;
    for (const auto& img : images) {
        int s = img.s;
        ico.push_back((BYTE)(s >= 256 ? 0 : s));
        ico.push_back((BYTE)(s >= 256 ? 0 : s));
        ico.push_back(0); // color count
        ico.push_back(0); // reserved
        put16(1);  // planes
        put16(32); // bpp
        put32((DWORD)img.data.size());
        put32(offset);
        offset += (DWORD)img.data.size();
    }

    for (const auto& img : images) {
        ico.insert(ico.end(), img.data.begin(), img.data.end());
    }

    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    WriteFile(h, ico.data(), (DWORD)ico.size(), &w, nullptr);
    CloseHandle(h);
    return w == ico.size();
}

void AddRoundRect(GraphicsPath& path, float x, float y, float w, float h, float r) {
    if (r <= 0.0f) {
        path.AddRectangle(RectF(x, y, w, h));
        return;
    }
    r = std::min(r, std::min(w / 2.0f, h / 2.0f));
    const float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillDot(Graphics& g, float cx, float cy, float r, int tag, float ring) {
    const auto& t = tags::kTags[tag];
    if (ring > 0) {
        SolidBrush halo(Color(255, 255, 255, 255));
        const float rr = r + ring;
        g.FillEllipse(&halo, cx - rr, cy - rr, rr * 2, rr * 2);
    }
    SolidBrush b(Color(255, t.r, t.g, t.b));
    g.FillEllipse(&b, cx - r, cy - r, r * 2, r * 2);
}

// Sidebar icon: a single centered dot.
void PaintTagIcon(Graphics& g, int s, int tag) { FillDot(g, s / 2.0f, s / 2.0f, s * 0.34f, tag, 0); }

// Overlay: Modern rounded rectangle badge with white border and color center,
// matching user's requested rectangle sketch.
//
// Explorer draws the overlay in a D x D box anchored at the bottom-left of the t x t icon cell,
// so the badge must be placed relative to the cell, not to the overlay frame. Measured layout
// (in cell px, t = icon size): folder front flap band is centered 0.3087*t above the cell bottom,
// the target badge is 0.1947*t x 0.113*t with its left edge at 0.1442*t. A frame of size s is
// shown in cells with k = t/D, which depends on the view that loads this frame:
//   small list frame  -> details / small views, D = t           -> k = 1
//   large list frame  -> desktop Small (t = D) and Medium (t = 1.5*D); Medium wins -> k = 1.5
//   xlarge list frame -> desktop "Medium + 1 Ctrl+wheel notch" (t ~= 66, D ~= 40.7): calibrated
//                        directly from a screenshot of that size (fractions of the frame below)
//   larger frames     -> desktop Large (t = 3*large, D ~= 0.47*t)                  -> k = 2.12
void PaintOverlay(Graphics& g, int s, int tag) {
    const int lg = LargeListSize();
    const int xl = XLargeListSize();
    float ox, oy, ow, oh;
    if (s >= xl && s < xl + 4) {
        // Rescaled by Explorer (not shown 1:1), so no pixel snapping.
        oh = s * 0.1917f;
        ow = s * 0.3286f;
        ox = s * 0.27f;
        oy = s * 0.4667f - oh / 2.0f;
    } else {
        const float k = s < lg ? 1.0f : (s < xl ? 1.5f : 2.12f);
        const float ks = k * s;
        // Band center above the cell bottom; Medium measured 1 px higher than the generic value.
        const float band = k == 1.5f ? 0.325f : 0.3087f;

        ow = ks * 0.1947f;
        oh = ks * 0.113f;
        if (oh < 4.5f) {  // keep tiny frames legible
            ow *= 4.5f / oh;
            oh = 4.5f;
        }
        // Snap the outer (white) rect to whole pixels: frames are shown 1:1, edges stay crisp.
        oh = std::floor(oh + 0.5f);
        ow = std::floor(ow + 0.5f);
        ox = std::max(1.0f, std::floor(ks * 0.1442f + 0.5f));
        oy = std::min(s - 1.0f - oh, std::floor(s - ks * band - oh / 2.0f + 0.5f));
    }

    const float ring = std::max(1.0f, oh * 0.13f);
    const float outerR = oh * 0.32f;
    const float cr = std::max(0.5f, outerR - ring);

    const float x = ox + ring;
    const float y = oy + ring;
    const float w = ow - ring * 2.0f;
    const float h = oh - ring * 2.0f;

    // Drop shadow
    {
        GraphicsPath sp;
        AddRoundRect(sp, x - ring, y - ring + std::max(0.6f, oh * 0.07f), w + ring * 2.0f, h + ring * 2.0f, cr + ring);
        SolidBrush shadow(Color(65, 0, 0, 0));
        g.FillPath(&shadow, &sp);
    }

    // White border / halo
    {
        GraphicsPath wp;
        AddRoundRect(wp, x - ring, y - ring, w + ring * 2.0f, h + ring * 2.0f, cr + ring);
        SolidBrush halo(Color(255, 255, 255, 255));
        g.FillPath(&halo, &wp);
    }

    // Color fill
    {
        const auto& t = tags::kTags[tag];
        GraphicsPath cp;
        AddRoundRect(cp, x, y, w, h, cr);
        SolidBrush fill(Color(255, t.r, t.g, t.b));
        g.FillPath(&fill, &cp);
    }
}

// Root "Tags" icon: three overlapping dots (red, yellow, blue) like the Finder sidebar.
void PaintRootIcon(Graphics& g, int s, int) {
    const float r = s * 0.24f;
    const int order[] = {4, 2, 0};
    for (int i = 0; i < 3; ++i)
        FillDot(g, s * (0.28f + 0.22f * i), s * 0.5f, r, order[i], std::max(1.0f, s * 0.04f));
}

// ---------- Tags folder tree ----------

// `name` (optional) is the display name Explorer shows instead of the folder's real name.
void WriteDesktopIni(const std::wstring& dir, const std::wstring& icon, const wchar_t* name = nullptr) {
    const std::wstring ini = dir + L"\\desktop.ini";
    SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_NORMAL);
    std::wstring body = L"\xFEFF[.ShellClassInfo]\r\nIconResource=" + icon + L",0\r\n";
    if (name) body += std::wstring(L"LocalizedResourceName=") + name + L"\r\n";
    HANDLE h = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, body.data(), (DWORD)(body.size() * sizeof(wchar_t)), &w, nullptr);
        CloseHandle(h);
    }
    SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    PathMakeSystemFolderW(dir.c_str());  // sets ReadOnly so Explorer honours desktop.ini
}

std::wstring LinkTarget(const std::wstring& lnk) {
    std::wstring out;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link))))
        return out;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
        if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
            wchar_t target[MAX_PATH * 2] = {};
            if (SUCCEEDED(link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH)))
                out = target;
        }
        pf->Release();
    }
    link->Release();
    return out;
}

// Returns the shortcut in `dir` that points to `path`, or empty.
std::wstring FindLink(const std::wstring& dir, const std::wstring& path) {
    const std::wstring want = Lower(tags::Normalize(path));
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        const std::wstring lnk = dir + L"\\" + fd.cFileName;
        if (Lower(tags::Normalize(LinkTarget(lnk))) == want) {
            found = lnk;
            break;
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return found;
}

void AddLink(const std::wstring& dir, const std::wstring& path) {
    if (!FindLink(dir, path).empty()) return;
    std::wstring base = PathFindFileNameW(path.c_str());
    if (base.empty() || base.find(L':') != std::wstring::npos) base = i18n::T(L"Drive", L"Lecteur");
    std::wstring lnk = dir + L"\\" + base + L".lnk";
    for (int i = 2; PathFileExistsW(lnk.c_str()); ++i)
        lnk = dir + L"\\" + base + L" (" + std::to_wstring(i) + L").lnk";

    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link))))
        return;
    link->SetPath(path.c_str());
    link->SetDescription(path.c_str());
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))) {
        pf->Save(lnk.c_str(), TRUE);
        pf->Release();
    }
    link->Release();
}

void RemoveLink(const std::wstring& dir, const std::wstring& path) {
    const std::wstring lnk = FindLink(dir, path);
    if (!lnk.empty()) DeleteFileW(lnk.c_str());
}

void ScanExisting(const std::wstring& dir, int depth) {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || fd.cFileName[0] == L'.')
            continue;
        const std::wstring sub = dir + L"\\" + fd.cFileName;
        if (const unsigned mask = tags::Read(sub)) Update(sub, 0, mask);
        if (depth > 1) ScanExisting(sub, depth - 1);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
}

HRESULT SetReg(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& v,
               DWORD type = REG_SZ) {
    return HRESULT_FROM_WIN32(RegSetKeyValueW(root, key.c_str(), name, type, v.c_str(),
                                              (DWORD)((v.size() + 1) * 2)));
}
HRESULT SetReg(HKEY root, const std::wstring& key, const wchar_t* name, DWORD v) {
    return HRESULT_FROM_WIN32(RegSetKeyValueW(root, key.c_str(), name, REG_DWORD, &v, sizeof(v)));
}

const wchar_t kNameSpaceKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Desktop\\NameSpace\\";
const wchar_t kHideDesktopIconsKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel";

void DeleteNavPane(HKEY root) {
    RegDeleteTreeW(root, (std::wstring(L"Software\\Classes\\CLSID\\") + kNavClsid).c_str());
    RegDeleteTreeW(root, (std::wstring(kNameSpaceKey) + kNavClsid).c_str());
}

std::wstring TagIconPath(int tag) {
    return IconPath((std::wstring(L"tag_") + tags::kTags[tag].id).c_str());
}

// v1.0.0 named the color folders, icons and warm-up folders after the French label.
// Move the color folders (with their shortcuts) to the stable ids and drop the old files.
void MigrateLegacyNames() {
    for (int i = 0; i < tags::kCount; ++i) {
        const auto& t = tags::kTags[i];
        if (!lstrcmpiW(t.labelFr, t.id)) continue;  // "Orange" == "orange" on NTFS
        const std::wstring old = TagsRoot() + L"\\" + t.labelFr;
        if (PathIsDirectoryW(old.c_str()) && !PathFileExistsW(ColorDir(i).c_str())) {
            SetFileAttributesW(old.c_str(), FILE_ATTRIBUTE_NORMAL);  // drop system-folder ReadOnly
            MoveFileW(old.c_str(), ColorDir(i).c_str());
        }
        DeleteFileW(IconPath((std::wstring(L"tag_") + t.labelFr).c_str()).c_str());
        DeleteFileW(IconPath((std::wstring(L"overlay23_") + t.labelFr).c_str()).c_str());
        RemoveDirectoryW((DataDir() + L"\\warmup\\" + t.labelFr).c_str());
    }
}

}  // namespace

std::wstring DataDir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\FolderTags"; }
std::wstring IconPath(const wchar_t* name) { return DataDir() + L"\\icons\\" + name + L".ico"; }
std::wstring OverlayIconPath(int tag) {
    return IconPath((std::wstring(L"overlay23_") + tags::kTags[tag].id).c_str());
}
std::wstring WarmupDir(int tag) { return DataDir() + L"\\warmup\\" + tags::kTags[tag].id; }

void Update(const std::wstring& path, unsigned oldMask, unsigned newMask) {
    for (int i = 0; i < tags::kCount; ++i) {
        const unsigned bit = 1u << i;
        if ((oldMask & bit) == (newMask & bit) && !(newMask & bit)) continue;
        CreateDirectoryW(ColorDir(i).c_str(), nullptr);
        if (newMask & bit)
            AddLink(ColorDir(i), path);
        else
            RemoveLink(ColorDir(i), path);
    }
}

// Per-user part: icons, warm-up folders and the Tags folder tree under %LOCALAPPDATA%.
void SetupUserData() {
    draw::EnsureGdiplus();
    const std::wstring data = DataDir();
    CreateDirectoryW(data.c_str(), nullptr);
    CreateDirectoryW((data + L"\\icons").c_str(), nullptr);
    CreateDirectoryW(TagsRoot().c_str(), nullptr);
    MigrateLegacyNames();

    const std::wstring rootIcon = IconPath(L"tags");
    SaveIco(rootIcon, PaintRootIcon, 0);
    WriteDesktopIni(TagsRoot(), rootIcon);

    CreateDirectoryW((data + L"\\warmup").c_str(), nullptr);
    SetFileAttributesW((data + L"\\warmup").c_str(), FILE_ATTRIBUTE_HIDDEN);
    for (int i = 0; i < tags::kCount; ++i) {
        const std::wstring icon = TagIconPath(i);
        SaveIco(icon, PaintTagIcon, i);
        SaveIco(OverlayIconPath(i), PaintOverlay, i);
        CreateDirectoryW(ColorDir(i).c_str(), nullptr);
        // Display name follows the UI language at install time (re-run install after a change).
        WriteDesktopIni(ColorDir(i), icon, tags::Label(i));

        const std::wstring warm = WarmupDir(i);
        CreateDirectoryW(warm.c_str(), nullptr);
        HANDLE h = CreateFileW((warm + L":FolderTags.Tags").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, tags::kTags[i].key, (DWORD)strlen(tags::kTags[i].key), &w, nullptr);
            CloseHandle(h);
        }
    }

    // The namespace entry is registered for the whole machine, but hiding it from the desktop
    // is a per-user setting.
    SetReg(HKEY_CURRENT_USER, kHideDesktopIconsKey, kNavClsid, 1);

    // Pick up tags set before the index existed.
    ScanExisting(KnownFolder(FOLDERID_Desktop), 2);
    ScanExisting(KnownFolder(FOLDERID_Profile), 1);
    ScanExisting(KnownFolder(FOLDERID_Documents), 2);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void EnsureUserData() {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(
        &once,
        [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
            bool complete = PathFileExistsW(IconPath(L"tags").c_str()) != FALSE;
            for (int i = 0; complete && i < tags::kCount; ++i)
                complete = PathFileExistsW(OverlayIconPath(i).c_str()) != FALSE;
            if (!complete) SetupUserData();
            return TRUE;
        },
        nullptr, nullptr);
}

HRESULT Setup() {
    SetupUserData();

    // Navigation pane entry ("Tags" under Quick Access) for every user: the paths expand to
    // each user's own %LOCALAPPDATA%. Uses shell32's delegate folder: no code of ours is
    // involved in browsing it.
    DeleteNavPane(HKEY_CURRENT_USER);  // v1.x per-user entry
    const HKEY m = HKEY_LOCAL_MACHINE;
    const std::wstring k = std::wstring(L"Software\\Classes\\CLSID\\") + kNavClsid;
    HRESULT hr = SetReg(m, k, nullptr, L"Tags");
    if (FAILED(hr)) return hr;
    SetReg(m, k, L"System.IsPinnedToNameSpaceTree", 1);
    SetReg(m, k, L"SortOrderIndex", 0x41);
    SetReg(m, k + L"\\DefaultIcon", nullptr, L"%LOCALAPPDATA%\\FolderTags\\icons\\tags.ico",
           REG_EXPAND_SZ);
    SetReg(m, k + L"\\InProcServer32", nullptr, L"shell32.dll");
    SetReg(m, k + L"\\Instance", L"CLSID", L"{0E5AAE11-A475-4c5b-AB00-C66DE400274E}");
    SetReg(m, k + L"\\Instance\\InitPropertyBag", L"Attributes", 0x11);
    SetReg(m, k + L"\\Instance\\InitPropertyBag", L"TargetFolderPath",
           L"%LOCALAPPDATA%\\FolderTags\\Tags", REG_EXPAND_SZ);
    SetReg(m, k + L"\\ShellFolder", L"FolderValueFlags", 0x28);
    SetReg(m, k + L"\\ShellFolder", L"Attributes", 0xF080004D);
    SetReg(m, std::wstring(kNameSpaceKey) + kNavClsid, nullptr, L"Tags");
    return S_OK;
}

void Teardown() {
    DeleteNavPane(HKEY_LOCAL_MACHINE);
    DeleteNavPane(HKEY_CURRENT_USER);  // v1.x per-user entry
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kHideDesktopIconsKey, kNavClsid);
}

}  // namespace index
