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

struct Layer {
    std::string id;
    std::string name;
    double opacity = 1.0; // 0..1
    BlendMode blend = BlendMode::Normal;
    bool visible = true;
};

struct Column {
    std::string name;
    double fadeSeconds = 1.0;                 // 0..60
    std::map<std::string, std::string> cells; // layerId -> mediaId
};

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

    const MediaItem* findMedia(const std::string& id) const;
    const Layer* findLayer(const std::string& id) const;

    // Projecto novo com 2 layers, 4 colunas e 1 output.
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
