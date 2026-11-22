#pragma once
// Placas gráficas do PC (DXGI) e escolha da placa do motor.

#include <windows.h>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace vthost {

struct GpuInfo {
    std::string name;        // nome único (ex. "NVIDIA GeForce RTX 4060 Laptop GPU"; repetidos ganham " #2")
    double dedicatedMb = 0;  // memória de vídeo própria
    bool highPerformance = false; // a que o Windows considera de alto desempenho
};

// Placas físicas (sem o "Microsoft Basic Render Driver"), a de alto desempenho primeiro.
std::vector<GpuInfo> enumerateGpus();

// Placa para o motor: `wanted` = nome (de enumerateGpus) ou "" = alto desempenho.
// Se a escolhida não existir, usa a de alto desempenho (e `fellBack` = true).
// Devolve nullptr se não houver nenhuma (o motor usa então o adaptador por omissão).
Microsoft::WRL::ComPtr<IDXGIAdapter1> pickGpu(const std::string& wanted, std::string& chosenName, bool& fellBack);

} // namespace vthost
