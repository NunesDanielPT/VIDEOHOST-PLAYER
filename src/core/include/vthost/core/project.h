#pragma once
// Modelo do projecto VTHOST (.vthost, JSON).
//
// Ideia base (inspirada em media servers de espetáculo):
//   - Media: ficheiros de vídeo/imagem na biblioteca.
//   - Layers: camadas compostas de baixo para cima no canvas.
//   - Colunas (cues): cada coluna define que media toca em cada layer. "GO" numa
//     coluna dispara essas media com um fade.
//   - Outputs: ecrãs/projectores onde o canvas é mostrado.
//
// O parser é tolerante: valores fora do intervalo são corrigidos (clamp) e
// referências inválidas são descartadas com aviso, em vez de falhar o projecto
// inteiro. Um projecto ligeiramente danificado deve abrir na mesma.

#include "vthost/core/mapping.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vthost {

inline constexpr int kProjectFormatVersion = 1;
inline constexpr int kMaxCanvasSize = 16384;

enum class BlendMode { Normal, Add, Multiply, Screen };

const char* toString(BlendMode m);
BlendMode blendModeFromString(const std::string& s);

// O que acontece quando uma media chega ao fim (como no Millumin).
enum class LoopMode {
    Infinite,   // repete sem parar
    OnceFreeze, // toca uma vez e fica no último frame
    OnceStop,   // toca uma vez e desaparece
    AutoFollow, // toca uma vez e dispara a coluna seguinte
};
const char* toString(LoopMode m);
LoopMode loopModeFromString(const std::string& s);

struct MediaItem {
    std::string id;
    std::string path;     // caminho absoluto (UTF-8)
    LoopMode loopMode = LoopMode::Infinite;
    bool loops() const { return loopMode == LoopMode::Infinite; }
};

// Propriedades de uma layer que podem mudar ao vivo (Properties no Studio).
struct LayerProps {
    double opacity = 1.0;  // 0..1
    BlendMode blend = BlendMode::Normal;
    double posX = 0.0;     // deslocamento em píxeis do canvas (0,0 = centrado)
    double posY = 0.0;
    double scale = 1.0;    // 1 = encaixa no canvas mantendo a proporção; 0.01..10
    double rotation = 0.0; // graus, -3600..3600

    friend bool operator==(const LayerProps&, const LayerProps&) = default;
    void clamp();
};

// Desenho de uma layer no canvas: centro e meia-largura/altura em píxeis do canvas
// e rotação. A media encaixa no canvas (fit) e depois aplica-se escala/posição.
struct LayerQuad {
    double cx, cy, halfW, halfH, rotationRad;
};
LayerQuad computeLayerQuad(const LayerProps& p, int mediaW, int mediaH, int canvasW, int canvasH);

// Canvas: superfície de composição com resolução própria. Cada layer pertence a
// um canvas; cada saída mostra uma região de um canvas.
struct Canvas {
    std::string id;
    std::string name;
    int width = 1920;
    int height = 1080;
};

struct Layer {
    std::string id;
    std::string name;
    std::string canvasId; // "" = primeiro canvas
    LayerProps props;
    bool visible = true;
    bool locked = false; // bloqueada: não se move, escala nem roda
};

struct Column {
    std::string name;
    double fadeSeconds = 1.0;                 // 0..60
    std::map<std::string, std::string> cells; // layerId -> mediaId
};

// Timeline: clips colocados no tempo, por layer.
struct Clip {
    std::string id;
    std::string layerId;
    std::string mediaId;
    double start = 0.0;    // segundos na timeline
    double duration = 5.0; // segundos (> 0)
    double inPoint = 0.0;  // início dentro da media
    double fadeIn = 0.0;   // segundos de fade-in no início do clip
    double fadeOut = 0.0;  // segundos de fade-out no fim do clip
    double end() const { return start + duration; }
    // Opacidade do clip no instante t da timeline (fades), 0..1.
    double opacityAt(double t) const;
};

// Segmento: zona da timeline; com loop, a cabeça de leitura volta ao início ao chegar ao fim.
struct Segment {
    std::string name;
    double start = 0.0;
    double end = 10.0;
    bool loop = false;
};

struct Timeline {
    std::string id;
    std::string name;
    std::vector<Clip> clips;
    std::vector<Segment> segments;

    double duration() const; // fim do último clip/segmento (mínimo 60 s)
    const Clip* clipAt(const std::string& layerId, double time) const;
    const Clip* findClip(const std::string& clipId) const;
};

inline constexpr double kMaxTimelineSeconds = 24.0 * 3600.0;

// Tipos de saída (como no painel Outputs do Millumin).
enum class OutputType {
    Display,  // ecrã/projetor/LED ligado à placa gráfica, em ecrã inteiro
    Window,   // janela normal (testes, pré-visualização)
    Ndi,      // envio por rede NDI
    DeckLink, // placa Blackmagic (SDI/HDMI) - em desenvolvimento
};
const char* toString(OutputType t);
OutputType outputTypeFromString(const std::string& s);

struct Output {
    std::string id;
    std::string name;
    std::string canvasId; // "" = primeiro canvas
    OutputMapping mapping; // região do canvas, warp, soft-edge, brilho
    OutputType type = OutputType::Display;
    bool enabled = true;
    std::string displayId;    // ecrã (ex. "\\.\DISPLAY2"); identifica o ecrã mesmo que a ordem mude
    int monitor = 0;          // índice de recurso, se displayId não existir (0 = principal)
    int windowWidth = 1280;   // tipo Window
    int windowHeight = 720;
    std::string ndiName;      // tipo Ndi ("" = "VTHOST - <nome>")
    bool ndiAlpha = false;
    std::string deviceId;     // tipo DeckLink
};

struct Project {
    int version = kProjectFormatVersion;
    std::string name = "Untitled";
    std::vector<Canvas> canvases; // nunca vazio depois de parse/makeDefault
    std::vector<MediaItem> media;
    std::vector<Layer> layers;
    std::vector<Column> columns;
    std::vector<Output> outputs;
    std::vector<Timeline> timelines;

    const Timeline* findTimeline(const std::string& id) const;
    const Canvas* findCanvas(const std::string& id) const;
    // Canvas de uma layer/saída: o indicado ou, se não existir, o primeiro.
    const Canvas& canvasOf(const std::string& canvasId) const;
    // Id efetivo (resolve "" e ids inexistentes para o primeiro canvas).
    std::string resolveCanvasId(const std::string& canvasId) const;
    const MediaItem* findMedia(const std::string& id) const;
    const Layer* findLayer(const std::string& id) const;

    // Projecto novo com 2 layers, 4 colunas, 1 output e 1 timeline vazia.
    static Project makeDefault();
};

struct ParseResult {
    std::optional<Project> project;
    std::vector<std::string> warnings;
    std::string error; // preenchido se project estiver vazio
};

ParseResult parseProject(const std::string& json);
std::string serializeProject(const Project& p);

ParseResult loadProjectFile(const std::string& utf8Path);
bool saveProjectFile(const Project& p, const std::string& utf8Path, std::string* error = nullptr);

} // namespace vthost
