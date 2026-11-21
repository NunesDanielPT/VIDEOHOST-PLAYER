#pragma once
// Preview do que está no ar: o motor escreve uma versão reduzida do canvas
// num bloco de memória partilhada (Local\VTHOST_Preview); o Studio lê-o para o
// Workspace. É exactamente a imagem que sai para os outputs.
//
// Sincronização sem locks (seqlock): o motor põe `sequence` ímpar enquanto
// escreve e par quando acaba. O leitor copia os píxeis e só aceita a cópia se
// `sequence` for par e igual antes e depois. O motor nunca espera pelo Studio.

#include <cstddef>
#include <cstdint>

namespace vthost {

inline constexpr std::uint32_t kPreviewMagic = 0x56545056; // "VTPV"
inline constexpr int kPreviewMaxWidth = 960;
inline constexpr int kPreviewMaxHeight = 540;
inline constexpr const wchar_t* kPreviewName = L"Local\\VTHOST_Preview";
// 2.º bloco: monitor Preview do CUEPLAYER (clip selecionado, antes de ir para o ar).
inline constexpr const wchar_t* kPreviewName2 = L"Local\\VTHOST_Preview2";
// 3.º bloco: fundo do editor de Output avançado (fonte de um slice ou a saída de um screen).
inline constexpr const wchar_t* kPreviewName3 = L"Local\\VTHOST_Preview3";

struct PreviewHeader {
    std::uint32_t magic;
    std::uint32_t width;   // <= kPreviewMaxWidth
    std::uint32_t height;  // <= kPreviewMaxHeight
    std::uint32_t stride;  // bytes por linha (width * 4), BGRA
    std::uint32_t canvasWidth;
    std::uint32_t canvasHeight;
    std::uint64_t sequence; // ímpar = a escrever
    std::uint64_t frameNumber;
};

inline constexpr std::size_t kPreviewPixelBytes =
    static_cast<std::size_t>(kPreviewMaxWidth) * kPreviewMaxHeight * 4;
inline constexpr std::size_t kPreviewBlockBytes = sizeof(PreviewHeader) + kPreviewPixelBytes;

// Tamanho do preview para um canvas (mantém a proporção, cabe em 960x540).
inline void previewSizeFor(int canvasW, int canvasH, int& outW, int& outH) {
    if (canvasW <= 0 || canvasH <= 0) {
        outW = kPreviewMaxWidth;
        outH = kPreviewMaxHeight;
        return;
    }
    double s = static_cast<double>(kPreviewMaxWidth) / canvasW;
    if (canvasH * s > kPreviewMaxHeight) s = static_cast<double>(kPreviewMaxHeight) / canvasH;
    if (s > 1.0) s = 1.0;
    outW = canvasW * s < 2 ? 2 : static_cast<int>(canvasW * s);
    outH = canvasH * s < 2 ? 2 : static_cast<int>(canvasH * s);
}

} // namespace vthost
