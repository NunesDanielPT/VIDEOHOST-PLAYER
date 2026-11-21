#include "displays.h"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace vthost {
namespace {

using Microsoft::WRL::ComPtr;

std::string utf8(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// "\\.\DISPLAY2" -> "OMEN 27" (nome que o monitor/projetor anuncia por EDID).
std::map<std::wstring, std::wstring> friendlyNames() {
    std::map<std::wstring, std::wstring> names;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return names;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS)
        return names;
    paths.resize(pathCount);
    for (const auto& p : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = p.sourceInfo.adapterId;
        src.header.id = p.sourceInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof(tgt);
        tgt.header.adapterId = p.targetInfo.adapterId;
        tgt.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
        if (DisplayConfigGetDeviceInfo(&tgt.header) != ERROR_SUCCESS) continue;
        std::wstring friendly = tgt.monitorFriendlyDeviceName;
        if (friendly.empty() && tgt.outputTechnology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL)
            friendly = L"Built-in display";
        names[src.viewGdiDeviceName] = friendly;
    }
    return names;
}

void fillMode(const wchar_t* device, protocol::DisplayInfo& d) {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &dm)) {
        d.width = static_cast<int>(dm.dmPelsWidth);
        d.height = static_cast<int>(dm.dmPelsHeight);
        d.refreshHz = dm.dmDisplayFrequency > 1 ? static_cast<double>(dm.dmDisplayFrequency) : 0.0;
    }
}

} // namespace

std::vector<protocol::DisplayInfo> enumerateDisplays() {
    std::vector<protocol::DisplayInfo> list;
    const auto names = friendlyNames();

    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a, adapter.Reset()) {
            DXGI_ADAPTER_DESC1 ad{};
            adapter->GetDesc1(&ad);
            if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            ComPtr<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o, output.Reset()) {
                DXGI_OUTPUT_DESC od{};
                if (FAILED(output->GetDesc(&od)) || !od.AttachedToDesktop) continue;
                protocol::DisplayInfo d;
                d.id = utf8(od.DeviceName);
                d.adapter = utf8(ad.Description);
                d.x = od.DesktopCoordinates.left;
                d.y = od.DesktopCoordinates.top;
                d.width = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
                d.height = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
                fillMode(od.DeviceName, d);
                MONITORINFO mi{sizeof(mi)};
                if (GetMonitorInfoW(od.Monitor, &mi)) d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                auto n = names.find(od.DeviceName);
                d.name = n != names.end() ? utf8(n->second.c_str()) : std::string();
                if (std::none_of(list.begin(), list.end(), [&](const auto& x) { return x.id == d.id; }))
                    list.push_back(std::move(d));
            }
        }
    }

    // Recurso: monitores que o DXGI não listou (drivers antigos, ligações remotas...).
    struct Ctx {
        std::vector<protocol::DisplayInfo>* list;
        const std::map<std::wstring, std::wstring>* names;
    } ctx{&list, &names};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR mon, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(mon, &mi)) return TRUE;
            const std::string id = utf8(mi.szDevice);
            if (std::any_of(c->list->begin(), c->list->end(), [&](const auto& x) { return x.id == id; })) return TRUE;
            protocol::DisplayInfo d;
            d.id = id;
            d.x = mi.rcMonitor.left;
            d.y = mi.rcMonitor.top;
            d.width = mi.rcMonitor.right - mi.rcMonitor.left;
            d.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
            d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            fillMode(mi.szDevice, d);
            auto n = c->names->find(mi.szDevice);
            if (n != c->names->end()) d.name = utf8(n->second.c_str());
            c->list->push_back(std::move(d));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));

    for (auto& d : list) {
        if (d.name.empty()) d.name = d.id;
    }
    // Ordem estável: principal primeiro, depois da esquerda para a direita.
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        if (a.primary != b.primary) return a.primary;
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    for (std::size_t i = 0; i < list.size(); ++i) list[i].index = static_cast<int>(i);
    return list;
}

bool findDisplay(const std::vector<protocol::DisplayInfo>& displays, const std::string& id, int index,
                 bool allowIndexFallback, protocol::DisplayInfo& out) {
    if (!id.empty()) {
        for (const auto& d : displays)
            if (d.id == id) {
                out = d;
                return true;
            }
        if (!allowIndexFallback) return false;
    }
    if (index >= 0 && index < static_cast<int>(displays.size())) {
        out = displays[static_cast<std::size_t>(index)];
        return true;
    }
    return false;
}

std::string describeDisplay(const protocol::DisplayInfo& d) {
    std::string s = d.name + " · " + std::to_string(d.width) + "×" + std::to_string(d.height);
    if (d.refreshHz > 0) s += " @ " + std::to_string(static_cast<int>(std::lround(d.refreshHz))) + " Hz";
    return s;
}

} // namespace vthost
