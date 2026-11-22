#include "gpus.h"

#include <map>

using Microsoft::WRL::ComPtr;

namespace vthost {
namespace {

std::string utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

struct Entry {
    GpuInfo info;
    ComPtr<IDXGIAdapter1> adapter;
};

// Por ordem de preferência de desempenho (IDXGIFactory6, Windows 10 1803+); senão a ordem normal.
std::vector<Entry> listAdapters() {
    std::vector<Entry> out;
    ComPtr<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return out;
    ComPtr<IDXGIFactory6> f6;
    f1.As(&f6);
    std::map<std::string, int> seen;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        const HRESULT hr = f6 ? f6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a))
                              : f1->EnumAdapters1(i, &a);
        if (hr == DXGI_ERROR_NOT_FOUND || FAILED(hr) || i > 16) break;
        DXGI_ADAPTER_DESC1 d{};
        if (FAILED(a->GetDesc1(&d)) || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        Entry e;
        e.info.name = utf8(d.Description);
        const int n = ++seen[e.info.name];
        if (n > 1) e.info.name += " #" + std::to_string(n);
        e.info.dedicatedMb = static_cast<double>(d.DedicatedVideoMemory) / (1024.0 * 1024.0);
        e.adapter = a;
        out.push_back(std::move(e));
    }
    if (!out.empty()) {
        // Sem IDXGIFactory6: a de mais memória própria conta como a de alto desempenho.
        std::size_t best = 0;
        if (!f6)
            for (std::size_t i = 1; i < out.size(); ++i)
                if (out[i].info.dedicatedMb > out[best].info.dedicatedMb) best = i;
        out[best].info.highPerformance = true;
        if (best != 0) std::swap(out[0], out[best]);
    }
    return out;
}

} // namespace

std::vector<GpuInfo> enumerateGpus() {
    std::vector<GpuInfo> v;
    for (auto& e : listAdapters()) v.push_back(e.info);
    return v;
}

ComPtr<IDXGIAdapter1> pickGpu(const std::string& wanted, std::string& chosenName, bool& fellBack) {
    fellBack = false;
    auto list = listAdapters();
    if (list.empty()) return nullptr;
    if (!wanted.empty()) {
        for (auto& e : list)
            if (e.info.name == wanted) {
                chosenName = e.info.name;
                return e.adapter;
            }
        fellBack = true;
    }
    chosenName = list.front().info.name;
    return list.front().adapter;
}

} // namespace vthost
