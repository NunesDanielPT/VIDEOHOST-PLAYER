#pragma once
// Mapping de saídas: que parte do canvas vai para cada saída e como é deformada
// (warp) para encaixar na superfície: perspetiva (4 cantos) + malha de pontos
// com interpolação suave (Catmull-Rom), soft-edge por lado e brilho.
//
// Coordenadas:
//   - região de origem: píxeis do canvas;
//   - pontos de warp: normalizados na saída (0,0 = canto sup. esq., 1,1 = inf. dir.);
//   - (u,v): posição normalizada dentro da região de origem (0..1).

#include <array>
#include <string>
#include <vector>

namespace vthost {

struct WarpPoint {
    double x = 0.0;
    double y = 0.0;
    friend bool operator==(const WarpPoint&, const WarpPoint&) = default;
};

enum class WarpMode { None, Perspective, Mesh };
const char* toString(WarpMode m);
WarpMode warpModeFromString(const std::string& s);

inline constexpr int kMaxMeshDivisions = 16;

struct OutputMapping {
    // Região do canvas (píxeis). Largura/altura <= 0 = canvas inteiro.
    double srcX = 0.0, srcY = 0.0, srcW = 0.0, srcH = 0.0;

    WarpMode mode = WarpMode::None;
    // Cantos: sup-esq, sup-dir, inf-dir, inf-esq (Perspective e Mesh).
    std::array<WarpPoint, 4> corners{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    // Malha (Mesh): (cols+1)*(rows+1) pontos, por linhas, no espaço ANTES da
    // perspetiva (grelha regular = sem deformação).
    int meshCols = 2;
    int meshRows = 2;
    std::vector<WarpPoint> mesh;

    // Soft-edge: largura de cada lado em píxeis da saída; curva e gama.
    double blendLeft = 0.0, blendRight = 0.0, blendTop = 0.0, blendBottom = 0.0;
    double blendSmoothness = 2.0; // expoente da curva (1 = linear)
    double blendGamma = 2.2;

    double brightness = 1.0; // 0..2 (igualar projetores)
    double contrast = 1.0;   // 0..2
    double gainR = 1.0, gainG = 1.0, gainB = 1.0; // 0..2 (correção de cor por saída/slice)

    friend bool operator==(const OutputMapping&, const OutputMapping&) = default;

    void resetMesh();          // grelha regular com meshCols x meshRows
    void sanitize();           // corrige valores fora do intervalo / malha com tamanho errado
    bool meshIsValid() const;
};

// Ponto de saída (normalizado) para (u,v) da região, aplicando malha e perspetiva.
WarpPoint warpPoint(const OutputMapping& m, double u, double v);

// Inverso da perspetiva: ponto de saída -> espaço da malha (para editar pontos no ecrã).
bool unwarpPerspective(const OutputMapping& m, const WarpPoint& out, WarpPoint& meshSpace);

// Fator de soft-edge (0..1) em (u,v) para uma saída de outW x outH píxeis.
double softEdgeFactor(const OutputMapping& m, double u, double v, int outW, int outH);

// Malha de triângulos para desenhar: vértices (posição de saída + uv), `div`x`div` quads.
struct WarpVertex {
    float x, y; // saída normalizada
    float u, v; // região normalizada
};
std::vector<WarpVertex> buildWarpGrid(const OutputMapping& m, int div);

// Layout: reparte um canvas por cols x rows saídas de outW x outH com
// sobreposição (soft-edge) em píxeis. Devolve as regiões/soft-edges por ordem
// (linha a linha) e o tamanho total do canvas.
struct LayoutCell {
    int col, row;
    OutputMapping mapping;
};
std::vector<LayoutCell> computeLayout(int cols, int rows, int outW, int outH, int overlapH, int overlapV,
                                      int& canvasW, int& canvasH);

} // namespace vthost
