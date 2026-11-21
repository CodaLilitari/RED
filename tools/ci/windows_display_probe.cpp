// Read-only diagnostic on a disposable GitHub-hosted runner. Not a release gate.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <set>
#include <tuple>

int main()
{
    SetProcessDPIAware();
    RECT work{};
    if ( !SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) )
    {
        return 1;
    }
    std::printf("Desktop %dx%d, work (%ld,%ld)-(%ld,%ld), DPI %u\n",
        GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        work.left, work.top, work.right, work.bottom, GetDpiForSystem());
    for ( const DWORD flags : {DWORD(0), DWORD(EDS_RAWMODE)} )
    {
        std::printf("Mode enumeration flags: %lu\n", flags);
        std::set<std::tuple<DWORD, DWORD, DWORD, DWORD>> seen;
        for ( DWORD index = 0; ; ++index )
        {
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if ( !EnumDisplaySettingsExW(nullptr, index, &mode, flags) )
            {
                break;
            }
            if ( !seen.emplace(mode.dmPelsWidth, mode.dmPelsHeight,
                    mode.dmBitsPerPel, mode.dmDisplayFrequency).second )
            {
                continue;
            }
            mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
            const LONG probe = ChangeDisplaySettingsExW(nullptr, &mode, nullptr, CDS_TEST, nullptr);
            std::printf("%lux%lu, %lu bpp, %lu Hz, CDS_TEST=%ld\n",
                mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel,
                mode.dmDisplayFrequency, probe);
        }
    }
    return 0;
}
