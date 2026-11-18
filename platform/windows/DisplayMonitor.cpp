#include "DisplayMonitor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace mmr {
namespace {
struct Mon { RECT r; UINT dpi; };
BOOL CALLBACK enumProc(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(m, &mi)) return TRUE;
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
    reinterpret_cast<std::vector<Mon>*>(lp)->push_back({mi.rcMonitor, dx});
    return TRUE;
}
}  // namespace

std::string currentLayoutSignature() {
    std::vector<Mon> mons;
    EnumDisplayMonitors(nullptr, nullptr, enumProc, reinterpret_cast<LPARAM>(&mons));
    std::sort(mons.begin(), mons.end(), [](const Mon& a, const Mon& b) {
        return a.r.left != b.r.left ? a.r.left < b.r.left : a.r.top < b.r.top;
    });
    std::ostringstream s;
    s << "v" << GetSystemMetrics(SM_XVIRTUALSCREEN) << "," << GetSystemMetrics(SM_YVIRTUALSCREEN) << ","
      << GetSystemMetrics(SM_CXVIRTUALSCREEN) << "," << GetSystemMetrics(SM_CYVIRTUALSCREEN);
    for (const auto& m : mons)
        s << ";" << m.r.left << "," << m.r.top << "," << (m.r.right - m.r.left) << ","
          << (m.r.bottom - m.r.top) << "@" << m.dpi;
    return s.str();
}

}  // namespace mmr
