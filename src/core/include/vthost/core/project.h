#pragma once
// Modelo do projecto VTHOST (.vthost, JSON). Dois workspaces que podem estar no ar
// ao mesmo tempo:
//
//   - MEDIAPLAYER — cues: a playlist (estilo Mitti). Cada cue é um ficheiro com
//     In/Out, velocidade, repetições, pausas, fades, crossfade, "ir para", volume
//     e geometria.
//   - CUEPLAYER — composições (estilo Resolume): cada uma com resolução própria,
//     layers × colunas de clips.
//   - Outputs (screens): ecrãs, janelas e NDI. Cada screen tem slices; cada slice
//     mostra uma fonte (MEDIAPLAYER, uma composição ou uma layer) com recorte,
//     posição, rotação, cantos/pontos, cor e soft edge.
//
// O parser é tolerante: valores fora do intervalo são corrigidos (clamp) e
// referências inválidas são descartadas com aviso. Um projecto ligeiramente
// danificado deve abrir na mesma.

#include "vthost/core/layer_props.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vthost {

inline constexpr int kProjectFormatVersion = 3;
inline constexpr int kMaxCanvasSize = 16384;
inline constexpr double kMaxMediaSeconds = 24.0 * 3600.0;

// Cores das cues (etiquetas, como no Mitti).
inline constexpr const char* kCueColors[] = {"red", "yellow", "green", "purple", "pink"};
bool isValidCueColor(const std::string& c); // "" também é válida (sem cor)

struct Cue {
    std::string id;     // estável, interno ("Q1", "Q2"...)
    std::string label;  // ID visível (ex. "INTRO"); "" = mostra o número
    std::string name;   // "" = nome do ficheiro
    std::string path;   // caminho absoluto (UTF-8)
    std::string color;  // "" ou uma de kCueColors
    std::string notes;
    std::optional<double> thumbTime; // miniatura escolhida (segundos no ficheiro); vazio = automática

    // Trim e reprodução.
    double inPoint = 0.0;  // segundos dentro do ficheiro
    double outPoint = 0.0; // 0 = até ao fim
    double speed = 1.0;    // 0.25..4
    int playCount = 1;     // 1 = uma vez; 0 = loop infinito; N = N vezes

    // Comportamento (ícones da playlist).
    bool pauseAtStart = false; // ao chegar a esta cue, fica pronta no 1.º frame à espera de play
    bool pauseAtEnd = false;   // no fim fica no último frame (não avança)
    bool clearAtEnd = false;   // no fim apaga para preto e pára
    double fadeIn = 0.0;       // segundos (a partir de preto)
    double fadeOut = 0.0;      // segundos (para preto)
    double crossfade = 0.0;    // dissolve a partir da cue anterior (0 = corte)
    std::string gotoCue;       // id da cue seguinte ("" = a próxima da lista)

    // Áudio.
    bool muted = false;
    double volumeDb = 0.0;          // -60..+12
    bool normalize = true;          // aplica o ganho de normalização (se medido)
    std::optional<double> loudness; // LUFS integrados medidos (análise no Studio)
    std::optional<double> peakDb;   // pico em dBFS

    // Geometria.
    FitMode fit = FitMode::Fit;
    double scale = 1.0; // 0.01..10
    double posX = 0.0;  // píxeis do canvas
    double posY = 0.0;

    std::string displayName() const; // nome ou nome do ficheiro (sem pasta)
    // Fim efetivo dentro do ficheiro (outPoint ou duração do ficheiro).
    double effectiveOut(double fileDuration) const;
    // Duração de uma passagem em tempo real (segundos, já com a velocidade).
    double passSeconds(double fileDuration) const;
    // Duração total em tempo real (todas as passagens); 0 se infinita ou desconhecida.
    double totalSeconds(double fileDuration) const;
    // Ganho total de áudio em dB (volume + normalização), sem contar mute/fades.
    double gainDb(double normalizeTargetLufs) const;
    void clamp();
};

// Ganho de normalização: leva `loudness` ao alvo, limitado a ±20 dB e de forma a
// que o pico não passe de -1 dBFS.
double normalizationGainDb(double loudnessLufs, std::optional<double> peakDb, double targetLufs);

enum class OutputType {
    Display,  // ecrã/projetor ligado à placa gráfica, em ecrã inteiro
    Window,   // janela normal (testes, pré-visualização)
    Ndi,      // envio por rede NDI
    DeckLink, // placa Blackmagic (SDI/HDMI) - em desenvolvimento
};
const char* toString(OutputType t);
OutputType outputTypeFromString(const std::string& s);

// Fonte de um slice.
enum class SliceSourceKind { MediaPlayer, Composition, Layer };
const char* toString(SliceSourceKind k);
SliceSourceKind sliceSourceFromString(const std::string& s);

// Uma zona de um screen (como os slices do Resolume).
struct Slice {
    std::string id;
    std::string name;
    bool enabled = true;
    SliceSourceKind source = SliceSourceKind::MediaPlayer;
    std::string compositionId; // Composition / Layer
    std::string layerId;       // Layer

    // Input Selection: recorte da fonte em píxeis da fonte (largura <= 0 = fonte inteira).
    double inX = 0.0, inY = 0.0, inW = 0.0, inH = 0.0;

    // Output Transformation, em píxeis do screen.
    double x = 960.0, y = 540.0;          // centro
    double width = 1920.0, height = 1080.0;
    double rotation = 0.0;                // graus
    bool flipH = false, flipV = false;
    bool keepAspect = false;              // encaixa a fonte no retângulo mantendo a proporção
    std::array<WarpPoint, 4> cornerOffsets{}; // ajuste de cada canto (sup-esq, sup-dir, inf-dir, inf-esq), px
    int meshCols = 0, meshRows = 0;       // pontos de edição (0 = só cantos)
    std::vector<WarpPoint> mesh;          // (cols+1)*(rows+1), normalizado no espaço do slice

    // Cor (-1..1, 0 = neutro) e soft edge (px do slice).
    double brightness = 0.0, contrast = 0.0, red = 0.0, green = 0.0, blue = 0.0;
    bool softEdge = false;
    double edgeLeft = 0.0, edgeRight = 0.0, edgeTop = 0.0, edgeBottom = 0.0;
    double edgeGamma = 2.2, edgePower = 2.0;

    void clamp();
};

struct Output {
    std::string id;
    std::string name;
    OutputType type = OutputType::Display;
    bool enabled = true;
    std::string displayId;    // ecrã (ex. "\\.\DISPLAY2"); identifica o ecrã mesmo que a ordem mude
    int monitor = 0;          // índice de recurso, se displayId não existir (0 = principal)
    int windowWidth = 1280;   // tipo Window
    int windowHeight = 720;
    std::string ndiName;      // tipo Ndi ("" = "VTHOST - <nome>")
    bool ndiAlpha = false;
    std::string deviceId;     // tipo DeckLink
    int width = 1920;         // resolução do screen (as posições dos slices estão nestes píxeis)
    int height = 1080;
    std::vector<Slice> slices;
};

// Slice que mostra a fonte inteira no screen inteiro.
Slice makeFullSlice(SliceSourceKind source, int screenW, int screenH, const std::string& id = "S1");

// Geometria final de um slice para o motor: região da fonte, cantos normalizados no
// screen (rotação, espelho, ajuste dos cantos, proporção), malha, cor e soft edge.
OutputMapping sliceToMapping(const Slice& s, int screenW, int screenH, int sourceW, int sourceH);

// ---------------------------------------------------------------- CUEPLAYER

// O que um clip faz quando chega ao fim.
enum class ClipEnd { Loop, Hold, Clear };
const char* toString(ClipEnd e);
ClipEnd clipEndFromString(const std::string& s);

struct Clip {
    std::string path;
    std::string name;           // "" = nome do ficheiro
    double inPoint = 0.0;
    double outPoint = 0.0;      // 0 = até ao fim
    double speed = 1.0;
    ClipEnd end = ClipEnd::Loop;
    double volumeDb = 0.0;
    bool normalize = true;
    std::optional<double> loudness;
    std::optional<double> peakDb;
    std::optional<double> thumbTime;

    std::string displayName() const;
    void clamp();
};

struct CompLayer {
    std::string id;
    std::string name;
    LayerProps props;          // opacidade, blend, posição, escala, rotação, ajuste, espelho
    bool video = true;         // V: imagem ligada
    bool audio = true;         // A: som ligado
    bool solo = false;         // S
    bool bypass = false;       // B: layer escondida (som e imagem)
    double transition = 0.0;   // s: dissolve ao trocar/limpar o clip (0 = corte)
    double volumeDb = 0.0;
    void clamp();
};

struct CompColumn {
    std::string id;
    std::string name;
    std::map<std::string, Clip> cells; // layerId -> clip
};

struct Composition {
    std::string id;
    std::string name;
    int width = 1920;
    int height = 1080;
    double opacity = 1.0;      // master
    double volumeDb = 0.0;
    std::vector<CompLayer> layers;   // [0] = Layer 1 (em baixo)
    std::vector<CompColumn> columns;

    const CompLayer* findLayer(const std::string& id) const;
    const CompColumn* findColumn(const std::string& id) const;
    std::string newLayerId() const;
    std::string newColumnId() const;
    // Composição nova: 3 layers e 9 colunas vazias.
    static Composition makeDefault(const std::string& id, const std::string& name);
};

// Cue (para o motor) a partir de um clip numa layer de uma composição.
Cue clipToCue(const Clip& clip, const CompLayer& layer, const Composition& comp);

struct Project {
    int version = kProjectFormatVersion;
    std::string name = "Untitled";
    int width = 1920;  // resolução de trabalho (canvas)
    int height = 1080;
    bool loopPlaylist = false;         // no fim da última cue volta à primeira
    double normalizeTargetLufs = -16.0; // alvo da normalização (-40..-5)
    double masterVolumeDb = 0.0;        // -60..+12
    std::vector<Cue> cues;
    std::vector<Composition> compositions;
    std::vector<Output> outputs;

    const Cue* findCue(const std::string& id) const;
    int cueIndex(const std::string& id) const; // -1 se não existir
    // Cue que se segue a `current` (respeita gotoCue e loopPlaylist).
    // -1 se não houver. current < 0 -> primeira cue.
    int nextCue(int current) const;
    std::string newCueId() const;
    std::string newOutputId() const;
    const Composition* findComposition(const std::string& id) const;
    std::string newCompositionId() const;

    // Projecto novo: playlist vazia, uma composição e uma saída de ecrã inteiro no 2.º ecrã.
    static Project makeDefault();
};

struct ParseResult {
    std::optional<Project> project;
    std::vector<std::string> warnings;
    std::string error; // preenchido se project estiver vazio
};

ParseResult parseProject(const std::string& json);
std::string serializeProject(const Project& p);

// Uma saída / layer em JSON (comandos ao vivo do Studio para o motor).
std::string outputToJson(const Output& o);
std::optional<Output> outputFromJson(const std::string& json);
std::string compLayerToJson(const CompLayer& l);
std::optional<CompLayer> compLayerFromJson(const std::string& json);

ParseResult loadProjectFile(const std::string& utf8Path);
bool saveProjectFile(const Project& p, const std::string& utf8Path, std::string* error = nullptr);

} // namespace vthost
