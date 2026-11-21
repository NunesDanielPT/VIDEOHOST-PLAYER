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

struct MediaItem {
    std::string id;
    std::string path;     // caminho absoluto (UTF-8)
    bool loop = true;
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

struct Layer {
    std::string id;
    std::string name;
    LayerProps props;
    bool visible = true;
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
    double end() const { return start + duration; }
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

struct Output {
    std::string id;
    std::string name;
    int monitor = 0;          // índice do monitor no Windows (0 = principal)
    bool fullscreen = true;
};

struct Project {
    int version = kProjectFormatVersion;
    std::string name = "Untitled";
    int canvasWidth = 1920;
    int canvasHeight = 1080;
    std::vector<MediaItem> media;
    std::vector<Layer> layers;
    std::vector<Column> columns;
    std::vector<Output> outputs;
    std::vector<Timeline> timelines;

    const Timeline* findTimeline(const std::string& id) const;
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
