#pragma once
// Geometria e aspecto de uma imagem desenhada no canvas (uso interno do motor):
// ajuste (fit), escala, posição, rotação, espelho, corner pin, efeitos e recorte.
// No player só o ajuste, escala e posição de cada cue são expostos.

#include "vthost/core/mapping.h"

#include <array>
#include <string>

namespace vthost {

enum class BlendMode { Normal, Add, Multiply, Screen };

const char* toString(BlendMode m);
BlendMode blendModeFromString(const std::string& s);

// Como a media se ajusta ao canvas (as "constraints" do Millumin).
enum class FitMode {
    Fit,     // cabe inteira no canvas, sem cortar (omissão)
    Fill,    // enche o canvas, corta o que sobra
    Stretch, // estica para o tamanho do canvas
    Native,  // tamanho real em píxeis
};
const char* toString(FitMode m);
FitMode fitModeFromString(const std::string& s);

// Propriedades de uma layer que podem mudar ao vivo (Properties no Studio).
struct LayerProps {
    double opacity = 1.0;  // 0..1
    BlendMode blend = BlendMode::Normal;
    double posX = 0.0;     // deslocamento em píxeis do canvas (0,0 = centrado)
    double posY = 0.0;
    double scale = 1.0;    // 1 = tamanho dado pelo ajuste (fit); 0.01..10
    double rotation = 0.0; // graus, -3600..3600
    FitMode fit = FitMode::Fit;
    bool flipH = false;
    bool flipV = false;

    // Mapping da layer (corner pin): deslocamento de cada canto em píxeis do
    // canvas, por ordem sup-esq, sup-dir, inf-dir, inf-esq.
    std::array<WarpPoint, 4> cornerPin{};

    // Efeitos de cor.
    double brightness = 0.0; // -1..1
    double contrast = 1.0;   // 0..3
    double saturation = 1.0; // 0..3
    double gamma = 1.0;      // 0.2..5
    double hue = 0.0;        // graus, -180..180
    bool invert = false;

    // Máscara: recorte de cada lado (fração da media, 0..1) com contorno suave.
    double cropLeft = 0.0, cropRight = 0.0, cropTop = 0.0, cropBottom = 0.0;
    double feather = 0.0; // 0..0.5

    friend bool operator==(const LayerProps&, const LayerProps&) = default;
    void clamp();
};

// Desenho de uma layer no canvas: centro e meia-largura/altura em píxeis do canvas
// e rotação. A media encaixa no canvas (fit) e depois aplica-se escala/posição.
struct LayerQuad {
    double cx, cy, halfW, halfH, rotationRad;
};
LayerQuad computeLayerQuad(const LayerProps& p, int mediaW, int mediaH, int canvasW, int canvasH);

// Os 4 cantos finais da layer em píxeis do canvas (sup-esq, sup-dir, inf-dir,
// inf-esq): ajuste + escala + posição + rotação + corner pin.
std::array<WarpPoint, 4> computeLayerCorners(const LayerProps& p, int mediaW, int mediaH, int canvasW, int canvasH);


} // namespace vthost
