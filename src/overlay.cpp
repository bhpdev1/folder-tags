#include "overlay.h"
#include "index.h"
#include "tags.h"
#include <commoncontrols.h>

namespace {

// Explorer asks every overlay handler about every item: cache the last lookups so the
// 7 handlers share a single stream read per folder.
struct Cache {
    SRWLOCK lock = SRWLOCK_INIT;
    static constexpr int N = 32;
    std::wstring path[N];
    unsigned mask[N] = {};
    ULONGLONG tick[N] = {};
    int next = 0;

    unsigned Get(const std::wstring& p) {
        const ULONGLONG now = GetTickCount64();
        AcquireSRWLockShared(&lock);
        for (int i = 0; i < N; ++i) {
            if (now - tick[i] < 250 && path[i] == p) {
                const unsigned m = mask[i];
                ReleaseSRWLockShared(&lock);
                return m;
            }
        }
        ReleaseSRWLockShared(&lock);
        const unsigned m = tags::Read(p);
        AcquireSRWLockExclusive(&lock);
        path[next] = p;
        mask[next] = m;
        tick[next] = now;
        next = (next + 1) % N;
        ReleaseSRWLockExclusive(&lock);
        return m;
    }

    void Clear() {
        AcquireSRWLockExclusive(&lock);
        for (auto& t : tick) t = 0;
        ReleaseSRWLockExclusive(&lock);
    }
};
Cache g_cache;

// Explorer only loads an overlay's image the first time an item needs it while the view is
// being populated; a color applied later then has no image and nothing is drawn. Querying one
// hidden pre-tagged folder per color makes this process load all 7 overlay images up front.
DWORD WINAPI OverlayWorker(void* module) {
    Sleep(500);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (int i = 0; i < tags::kCount; ++i) {
        SHFILEINFOW fi{};
        SHGetFileInfoW(index::WarmupDir(i).c_str(), 0, &fi, sizeof(fi),
                       SHGFI_ICON | SHGFI_SYSICONINDEX | SHGFI_OVERLAYINDEX);
        if (fi.hIcon) DestroyIcon(fi.hIcon);
    }
    CoUninitialize();
    FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
}

class Overlay final : public IShellIconOverlayIdentifier {
public:
    explicit Overlay(int tag) : tag_(tag) {
        InterlockedIncrement(&g_dllRefs);
        static LONG started = 0;
        HMODULE self = nullptr;
        if (InterlockedExchange(&started, 1) == 0 &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&OverlayWorker),
                               &self)) {
            if (HANDLE t = CreateThread(nullptr, 0, OverlayWorker, self, 0, nullptr))
                CloseHandle(t);
            else
                FreeLibrary(self);
        }
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        static const QITAB qit[] = {QITABENT(Overlay, IShellIconOverlayIdentifier), {nullptr, 0}};
        return QISearch(this, qit, riid, ppv);
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const long r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }

    IFACEMETHODIMP IsMemberOf(PCWSTR path, DWORD) override {
        if (!path) return S_FALSE;
        if (PathIsNetworkPathW(path) || PathIsRootW(path)) return S_FALSE;
        const unsigned mask = g_cache.Get(path);
        if (!mask) return S_FALSE;
        // One overlay per item: show the first tag (lowest index), deterministically.
        unsigned lowest = 0;
        while (!(mask & (1u << lowest))) ++lowest;
        return (int)lowest == tag_ ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP GetOverlayInfo(PWSTR iconFile, int cch, int* index, DWORD* flags) override {
        index::EnsureUserData();  // Explorer loads the icon file right after this call
        const std::wstring p = index::OverlayIconPath(tag_);
        if (FAILED(StringCchCopyW(iconFile, cch, p.c_str()))) return E_FAIL;
        *index = 0;
        *flags = ISIOI_ICONFILE | ISIOI_ICONINDEX;
        return S_OK;
    }

    IFACEMETHODIMP GetPriority(int* priority) override {
        *priority = 0;
        return S_OK;
    }

private:
    ~Overlay() { InterlockedDecrement(&g_dllRefs); }
    long refs_ = 1;
    int tag_;
};

}  // namespace

CLSID OverlayClsid(int tag) {
    return {0xb6e4cd56, 0x987a, 0x4c8f, {0x87, 0x29, 0x7f, 0xd3, 0xd4, 0xd8, 0xeb, (BYTE)(0xe0 + tag)}};
}

std::wstring OverlayClsidString(int tag) {
    wchar_t buf[64];
    StringFromGUID2(OverlayClsid(tag), buf, ARRAYSIZE(buf));
    return buf;
}

int OverlayTagFromClsid(REFCLSID clsid) {
    for (int i = 0; i < tags::kCount; ++i)
        if (IsEqualCLSID(clsid, OverlayClsid(i))) return i;
    return -1;
}

IUnknown* CreateOverlay(int tag) { return new (std::nothrow) Overlay(tag); }

void OverlayInvalidateCache() { g_cache.Clear(); }
