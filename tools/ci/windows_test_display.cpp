// For the disposable GitHub-hosted Windows release-test machine.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace {
// Resource-dock fixtures need 1773 pixels of client width. COG fixtures allocate
// their complete panels within the available work area, arranging surrounding
// diagnostics when necessary. This is a disposable test desktop requirement,
// measured in 96-DPI logical coordinates.
constexpr int minimumWorkWidth = 1800;
constexpr int minimumWorkHeight = 900;

bool currentMode(DEVMODEW& mode) {
    mode = {};
    mode.dmSize = sizeof(mode);
    return EnumDisplaySettingsExW(nullptr, ENUM_CURRENT_SETTINGS, &mode, 0) != FALSE;
}

struct Desktop {
    DEVMODEW current{};
    RECT work{};
    UINT dpi{};
    int screenWidth{};
    int screenHeight{};

    bool read() {
        dpi = GetDpiForSystem();
        screenWidth = GetSystemMetrics(SM_CXSCREEN);
        screenHeight = GetSystemMetrics(SM_CYSCREEN);
        if (!dpi || !currentMode(current) || !SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
            return false;
        }
        std::printf("Mode %lux%lu, desktop %dx%d, work area (%ld,%ld)-(%ld,%ld), DPI %u\n",
            current.dmPelsWidth, current.dmPelsHeight, screenWidth, screenHeight,
            work.left, work.top, work.right, work.bottom, dpi);
        return true;
    }

    int requiredWidth() const {
        return MulDiv(minimumWorkWidth, static_cast<int>(dpi), 96);
    }
    int requiredHeight() const {
        return MulDiv(minimumWorkHeight, static_cast<int>(dpi), 96);
    }
    bool enoughWorkArea() const {
        return work.right - work.left >= requiredWidth() && work.bottom - work.top >= requiredHeight();
    }
};

bool configure() {
    Desktop desktop;
    if (!desktop.read()) {
        return false;
    }
    if (desktop.enoughWorkArea()) {
        return true;
    }
    const int reservedWidth = desktop.screenWidth - (desktop.work.right - desktop.work.left);
    const int reservedHeight = desktop.screenHeight - (desktop.work.bottom - desktop.work.top);
    std::vector<DEVMODEW> candidates;
    for (DWORD index = 0; ; ++index) {
        DEVMODEW next{};
        next.dmSize = sizeof(next);
        if (!EnumDisplaySettingsExW(nullptr, index, &next, 0)) {
            break;
        }
        std::printf("Advertised mode %lux%lu at %lu Hz, %lu bits per pixel\n",
            next.dmPelsWidth, next.dmPelsHeight, next.dmDisplayFrequency, next.dmBitsPerPel);
        if (next.dmBitsPerPel == desktop.current.dmBitsPerPel
            && static_cast<int>(next.dmPelsWidth) >= desktop.requiredWidth() + reservedWidth
            && static_cast<int>(next.dmPelsHeight) >= desktop.requiredHeight() + reservedHeight) {
            candidates.push_back(next);
        }
    }
    // Prefer the smallest advertised mode that admits the usable work area.
    std::stable_sort(candidates.begin(), candidates.end(), [](const DEVMODEW& a, const DEVMODEW& b) {
        return UINT64(a.dmPelsWidth) * a.dmPelsHeight < UINT64(b.dmPelsWidth) * b.dmPelsHeight;
    });
    for (auto& next : candidates) {
        next.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        const LONG probe = ChangeDisplaySettingsExW(nullptr, &next, nullptr, CDS_TEST, nullptr);
        if (probe != DISP_CHANGE_SUCCESSFUL) {
            std::printf("Mode %lu Hz rejected by CDS_TEST (%ld)\n", next.dmDisplayFrequency, probe);
            continue;
        }
        const LONG applied = ChangeDisplaySettingsExW(nullptr, &next, nullptr, 0, nullptr);
        std::printf("Mode %lux%lu at %lu Hz application returned %ld\n",
            next.dmPelsWidth, next.dmPelsHeight, next.dmDisplayFrequency, applied);
        if (applied == DISP_CHANGE_SUCCESSFUL) {
            return true;
        }
    }
    std::fprintf(stderr, "No supported mode met the %dx%d logical work-area requirement\n",
        minimumWorkWidth, minimumWorkHeight);
    return false;
}
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDPIAware();
    if (argc == 2 && std::wcscmp(argv[1], L"--configure") == 0) {
        return configure() ? 0 : 1;
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--verify") == 0) {
        Desktop desktop;
        return desktop.read() && desktop.enoughWorkArea() ? 0 : 1;
    }
    std::fputs("Pass --configure or --verify\n", stderr);
    return 2;
}
