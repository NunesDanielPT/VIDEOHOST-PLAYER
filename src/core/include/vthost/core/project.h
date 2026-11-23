#pragma once
// Modelo do projecto VTHOST (.vthost, JSON) — player de playlist (estilo Mitti).
//
//   - Cues: a playlist. Cada cue é um ficheiro (vídeo, imagem ou só áudio) com
//     pontos In/Out, velocidade, número de repetições, pausas no início/fim,
//     fades, crossfade, "ir para", volume e geometria.
//   - Outputs: ecrãs/projectores (ecrã inteiro), janelas e NDI onde a imagem aparece.
//
// O parser é tolerante: valores fora do intervalo são corrigidos (clamp) e
// referências inválidas são descartadas com aviso. Um projecto ligeiramente
// danificado deve abrir na mesma.

#include "vthost/core/layer_props.h"

#include <optional>
#include <string>
#include <vector>

namespace vthost {

inline constexpr int kProjectFormatVersion = 2;
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
    std::string block;  // id do bloco a que pertence ("" = fora de blocos)
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

    // Modo DSM (monitor para a régie / colegas): o vídeo mais pequeno em cima e por
    // baixo a duração do vídeo e o tempo que falta, com legendas. Vale para qualquer
    // tipo de saída (ecrã, janela, NDI).
    bool dsm = false;
    std::string dsmLabelLength;    // legenda da duração ("" = "VIDEO LENGTH")
    std::string dsmLabelRemaining; // legenda do tempo que falta ("" = "TIME LEFT")
};

// Geometria do modo DSM, em fracções da saída (0..1).
struct DsmLayout {
    double videoX = 0, videoY = 0, videoW = 1, videoH = 1; // vídeo, com a proporção do canvas
    double panelX = 0, panelY = 0, panelW = 1, panelH = 0; // faixa dos tempos, por baixo
};
DsmLayout computeDsmLayout(int outW, int outH, int canvasW, int canvasH);

// Bloco: agrupa cues seguidas da playlist (podem esconder-se) e diz o que acontece
// quando a última cue do bloco acaba.
enum class BlockEnd { Continue, Pause, Stop };
// Continue = segue para a cue seguinte; Pause = fica no último frame à espera (ESPAÇO
// segue); Stop = preto.
struct Block {
    std::string id;   // "B1", "B2"...
    std::string name; // "" = "Block <n>"
    bool collapsed = false;
    bool loop = false; // no fim do bloco volta à primeira cue do bloco
    BlockEnd end = BlockEnd::Pause;
    std::string color;     // "" ou uma de kCueColors (cabeçalho e faixa)
    double dissolve = 0.0; // s: dissolve entre os vídeos do bloco (0 = como cada cue)
    double fadeIn = 0.0;   // s: fade in a partir do preto ao entrar no bloco
    double fadeOut = 0.0;  // s: fade out para preto no fim do bloco (sem loop)
    void clamp();
};
const char* toString(BlockEnd e);
BlockEnd blockEndFromString(const std::string& s);

struct Project {
    int version = kProjectFormatVersion;
    std::string name = "Untitled";
    int width = 1920;  // resolução de trabalho (canvas)
    int height = 1080;
    bool loopPlaylist = false;         // no fim da última cue volta à primeira
    double normalizeTargetLufs = -16.0; // alvo da normalização (-40..-5)
    double masterVolumeDb = 0.0;        // -60..+12
    std::vector<Cue> cues;
    std::vector<Output> outputs;
    std::vector<Block> blocks; // ordem dos blocos vazios; as cues de cada bloco ficam seguidas

    const Cue* findCue(const std::string& id) const;
    const Block* findBlock(const std::string& id) const;
    Block* findBlock(const std::string& id);
    std::string newBlockId() const;
    // Última cue do seu bloco? (então conta o loop / fim do bloco)
    bool isLastInBlock(int index) const;
    int firstInBlock(const std::string& blockId) const; // -1 se vazio
    // Cue com as regras do bloco aplicadas (dissolve entre vídeos, fade in à entrada,
    // fade out no fim). `previous` = cue que estava no ar (-1 = nenhuma).
    Cue effectiveCue(int index, int previous) const;
    // Põe as cues de cada bloco seguidas (na posição da primeira) e limpa blocos inexistentes.
    void normalizeBlocks();
    int cueIndex(const std::string& id) const; // -1 se não existir
    // Cue que se segue a `current` (respeita gotoCue, loop do bloco e loopPlaylist).
    // -1 se não houver. current < 0 -> primeira cue.
    int nextCue(int current) const;
    std::string newCueId() const;
    std::string newOutputId() const;

    // Projecto novo: playlist vazia e uma saída de ecrã inteiro no 2.º ecrã.
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
