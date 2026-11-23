// Testes do núcleo. Sem framework: cada CHECK que falha é contado e o programa
// sai com código != 0 (o CTest/CI marca a build como falhada).

#include "vthost/core/atomic_file.h"
#include "vthost/core/countdown.h"
#include "vthost/core/engine_config.h"
#include "vthost/core/loudness.h"
#include "vthost/core/mapping.h"
#include "vthost/core/osc.h"
#include "vthost/core/tally.h"
#include "vthost/core/preview.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"
#include "vthost/core/show_state.h"
#include "vthost/core/version.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <set>
#include <string>

namespace {
int g_failures = 0;
int g_checks = 0;
} // namespace

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                              \
    } while (0)

using namespace vthost;

static void testFuzzNeverThrows() {
    // Lixo aleatório e projectos truncados: nunca podem rebentar.
    std::mt19937 rng(1234);
    const std::string valid = serializeProject(Project::makeDefault());
    for (int i = 0; i < 2000; ++i) {
        std::string s = valid.substr(0, rng() % (valid.size() + 1));
        for (int k = 0; k < 3 && !s.empty(); ++k) s[rng() % s.size()] = static_cast<char>(rng() % 256);
        (void)parseProject(s);
        (void)protocol::parseCommand(s);
        (void)protocol::parseResponse(s);
        ShowState st;
        (void)parseShowState(s, st);
    }
    CHECK(true);
}

static void testAtomicWrite() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "vthost_test_atomic";
    fs::remove_all(dir);
    const fs::path f = dir / "projecto.vthost";
    CHECK(writeFileAtomic(f, "v1"));
    CHECK(writeFileAtomic(f, "v2"));
    auto r = readFileLimited(f);
    CHECK(r && *r == "v2");
    auto bak = readFileLimited(fs::path(f) += ".bak");
    CHECK(bak && *bak == "v1");
    CHECK(!fs::exists(fs::path(f) += ".tmp"));
    CHECK(!readFileLimited(f, 1)); // limite de tamanho respeitado
    fs::remove_all(dir);
}

static void testVersion() {
    auto v = parseVersion("v1.2.3");
    CHECK(v && v->major == 1 && v->minor == 2 && v->patch == 3);
    CHECK(parseVersion("0.1")->toString() == "0.1.0");
    CHECK(parseVersion("2.0.0-beta.1") && parseVersion("2.0.0-beta.1")->major == 2);
    CHECK(!parseVersion(""));
    CHECK(!parseVersion("v"));
    CHECK(!parseVersion("1.2.3.4"));
    CHECK(!parseVersion("1..2"));
    CHECK(!parseVersion("1.2."));
    CHECK(!parseVersion("abc"));
    CHECK(!parseVersion("99999999999"));
    CHECK(isNewerVersion("v0.2.0", "0.1.9"));
    CHECK(isNewerVersion("0.10.0", "0.9.0")); // numérico, não alfabético
    CHECK(isNewerVersion("1.0.0", "0.99.99"));
    CHECK(!isNewerVersion("0.1.0", "0.1.0"));
    CHECK(!isNewerVersion("0.0.9", "0.1.0"));
    CHECK(!isNewerVersion("lixo", "0.1.0"));
}

static bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

static void testMapping() {
    OutputMapping m;
    m.resetMesh();
    // Sem warp: identidade.
    auto p = warpPoint(m, 0.25, 0.75);
    CHECK(near(p.x, 0.25) && near(p.y, 0.75));
    // Perspetiva: os cantos vão exatamente para onde foram arrastados.
    m.mode = WarpMode::Perspective;
    m.corners = {{{0.1, 0.05}, {0.95, 0.0}, {0.9, 1.0}, {0.0, 0.92}}};
    const double us[4] = {0, 1, 1, 0}, vs[4] = {0, 0, 1, 1};
    for (int k = 0; k < 4; ++k) {
        auto c = warpPoint(m, us[k], vs[k]);
        CHECK(near(c.x, m.corners[static_cast<std::size_t>(k)].x) && near(c.y, m.corners[static_cast<std::size_t>(k)].y));
    }
    // Inverso da perspetiva.
    auto mid = warpPoint(m, 0.3, 0.6);
    WarpPoint back;
    CHECK(unwarpPerspective(m, mid, back) && near(back.x, 0.3, 1e-9) && near(back.y, 0.6, 1e-9));
    // Malha regular = identidade (com Catmull-Rom e pontos-fantasma).
    OutputMapping g;
    g.mode = WarpMode::Mesh;
    g.meshCols = 4;
    g.meshRows = 3;
    g.resetMesh();
    for (double u : {0.0, 0.13, 0.5, 0.77, 1.0})
        for (double v : {0.0, 0.31, 0.5, 1.0}) {
            auto q = warpPoint(g, u, v);
            CHECK(near(q.x, u, 1e-9) && near(q.y, v, 1e-9));
        }
    // Mover um ponto da malha desloca a imagem perto dele (e passa exatamente nele).
    g.mesh[static_cast<std::size_t>(1 * 5 + 2)] = {0.55, 0.40};
    auto moved = warpPoint(g, 2.0 / 4.0, 1.0 / 3.0);
    CHECK(near(moved.x, 0.55, 1e-9) && near(moved.y, 0.40, 1e-9));
    // Soft-edge: 1 fora da zona, 0 na borda, sobreposição soma ~1 em linear.
    OutputMapping b;
    b.blendRight = 100;
    CHECK(near(softEdgeFactor(b, 0.5, 0.5, 1000, 500), 1.0));
    CHECK(near(softEdgeFactor(b, 1.0, 0.5, 1000, 500), 0.0));
    const double a1 = std::pow(softEdgeFactor(b, 0.95, 0.5, 1000, 500), b.blendGamma);
    OutputMapping b2;
    b2.blendLeft = 100;
    const double a2 = std::pow(softEdgeFactor(b2, 0.05, 0.5, 1000, 500), b2.blendGamma);
    CHECK(near(a1 + a2, 1.0, 1e-6));
    // Malha de triângulos.
    CHECK(buildWarpGrid(g, 8).size() == 8u * 8u * 6u);
    // Layout 3x1 de 1920x1080 com 200 px de sobreposição.
    int cw = 0, ch = 0;
    auto cells = computeLayout(3, 1, 1920, 1080, 200, 0, cw, ch);
    CHECK(cw == 3 * 1920 - 2 * 200 && ch == 1080 && cells.size() == 3);
    if (cells.size() == 3) {
        CHECK(cells[1].mapping.srcX == 1720 && cells[1].mapping.blendLeft == 200 && cells[1].mapping.blendRight == 200);
        CHECK(cells[0].mapping.blendLeft == 0 && cells[2].mapping.blendRight == 0);
    }
    // Valores absurdos são corrigidos.
    OutputMapping bad;
    bad.meshCols = 99;
    bad.brightness = 50;
    bad.blendGamma = 0;
    bad.mesh = {{1, 2}};
    bad.sanitize();
    CHECK(bad.meshCols == kMaxMeshDivisions && bad.brightness == 2.0 && bad.blendGamma == 1.0 && bad.meshIsValid());

}

static void testLayerProps() {
    LayerProps p;
    p.fit = FitMode::Fill;
    p.flipH = true;
    p.cornerPin[2] = {12.5, -4};
    p.brightness = 0.2;
    p.contrast = 1.4;
    p.saturation = 0;
    p.gamma = 1.8;
    p.hue = 90;
    p.invert = true;
    p.cropLeft = 0.1;
    p.cropBottom = 0.25;
    p.feather = 0.05;
    // Valores absurdos
    LayerProps bad;
    bad.contrast = 99;
    bad.cropLeft = -1;
    bad.feather = 3;
    bad.clamp();
    CHECK(bad.contrast == 3.0 && bad.cropLeft == 0.0 && bad.feather == 0.5);

    // Ajustes: media 4:3 em canvas 16:9.
    LayerProps f;
    auto q = computeLayerQuad(f, 1440, 1080, 1920, 1080);
    CHECK(q.halfW == 720 && q.halfH == 540);
    f.fit = FitMode::Fill;
    q = computeLayerQuad(f, 1440, 1080, 1920, 1080);
    CHECK(q.halfW == 960 && q.halfH == 720);
    f.fit = FitMode::Stretch;
    q = computeLayerQuad(f, 1440, 1080, 1920, 1080);
    CHECK(q.halfW == 960 && q.halfH == 540);
    f.fit = FitMode::Native;
    q = computeLayerQuad(f, 640, 360, 1920, 1080);
    CHECK(q.halfW == 320 && q.halfH == 180);
    // Cantos com corner pin.
    LayerProps g;
    g.fit = FitMode::Stretch;
    g.cornerPin[1] = {10, 20};
    auto cs = computeLayerCorners(g, 100, 100, 1920, 1080);
    CHECK(near(cs[0].x, 0) && near(cs[0].y, 0) && near(cs[1].x, 1930) && near(cs[1].y, 20) && near(cs[2].x, 1920) &&
          near(cs[3].y, 1080));
}

static void testQuadAndPreview() {
    // Media 4:3 num canvas 16:9 => encaixa pela altura.
    LayerProps p;
    auto q = computeLayerQuad(p, 1440, 1080, 1920, 1080);
    CHECK(q.cx == 960.0 && q.cy == 540.0 && q.halfW == 720.0 && q.halfH == 540.0);
    p.scale = 0.5;
    p.posX = 100;
    q = computeLayerQuad(p, 1440, 1080, 1920, 1080);
    CHECK(q.cx == 1060.0 && q.halfW == 360.0);
    int w = 0, h = 0;
    previewSizeFor(1920, 1080, w, h);
    CHECK(w == 960 && h == 540);
    previewSizeFor(1080, 1920, w, h);
    CHECK(h == 540 && w < 960);
    previewSizeFor(640, 360, w, h);
    CHECK(w == 640 && h == 360);
}

static Cue makeCue(const std::string& path) {
    Cue q;
    q.path = path;
    return q;
}

static void testProject() {
    Project p = Project::makeDefault();
    p.name = "Gala ação";
    p.loopPlaylist = true;
    Cue a = makeCue("C:/v/Abertura.mp4");
    a.id = p.newCueId();
    a.label = "INTRO";
    a.color = "red";
    a.notes = "entrada do apresentador";
    a.thumbTime = 12.25;
    a.inPoint = 1.5;
    a.outPoint = 21.5;
    a.speed = 2.0;
    a.playCount = 3;
    a.pauseAtStart = true;
    a.fadeIn = 0.5;
    a.fadeOut = 1.0;
    a.crossfade = 2.0;
    a.volumeDb = -3.0;
    a.loudness = -20.0;
    a.peakDb = -6.0;
    a.fit = FitMode::Fill;
    a.scale = 1.2;
    a.posX = 10;
    p.cues.push_back(a);
    Cue b = makeCue("C:/v/Break.mov");
    b.id = p.newCueId();
    b.playCount = 0;
    b.pauseAtEnd = true;
    b.muted = true;
    b.gotoCue = a.id;
    p.cues.push_back(b);
    CHECK(a.id == "Q1" && b.id == "Q2");

    p.outputs[0].dsm = true;
    p.outputs[0].dsmLabelLength = "TEMPO DO VÍDEO";
    auto r = parseProject(serializeProject(p));
    CHECK(r.project && r.warnings.empty());
    if (!r.project) return;
    const Project& q = *r.project;
    CHECK(q.outputs[0].dsm && q.outputs[0].dsmLabelLength == "TEMPO DO VÍDEO" && q.outputs[0].dsmLabelRemaining.empty());
    CHECK(q.name == p.name && q.loopPlaylist && q.cues.size() == 2 && q.outputs.size() == 1);
    const Cue& x = q.cues[0];
    CHECK(x.id == "Q1" && x.label == "INTRO" && x.color == "red" && x.notes == a.notes);
    CHECK(x.thumbTime && *x.thumbTime == 12.25 && !q.cues[1].thumbTime);
    CHECK(x.inPoint == 1.5 && x.outPoint == 21.5 && x.speed == 2.0 && x.playCount == 3);
    CHECK(x.pauseAtStart && !x.pauseAtEnd && x.fadeIn == 0.5 && x.fadeOut == 1.0 && x.crossfade == 2.0);
    CHECK(x.volumeDb == -3.0 && x.loudness && *x.loudness == -20.0 && x.peakDb && *x.peakDb == -6.0);
    CHECK(x.fit == FitMode::Fill && x.scale == 1.2 && x.posX == 10);
    CHECK(q.cues[1].playCount == 0 && q.cues[1].pauseAtEnd && q.cues[1].muted && q.cues[1].gotoCue == "Q1");
    CHECK(x.displayName() == "Abertura.mp4");

    // Tempos: trim + velocidade + repetições.
    CHECK(x.effectiveOut(30.0) == 21.5);
    CHECK(x.passSeconds(30.0) == 10.0);
    CHECK(x.totalSeconds(30.0) == 30.0);
    CHECK(q.cues[1].totalSeconds(10.0) == 0.0); // infinito
    Cue t = makeCue("x");
    t.outPoint = 50.0;
    CHECK(t.effectiveOut(30.0) == 30.0); // out depois do fim do ficheiro: até ao fim
    CHECK(t.passSeconds(0.0) == 50.0);    // duração desconhecida: usa o out

    // Sequência e "ir para".
    CHECK(q.nextCue(-1) == 0 && q.nextCue(0) == 1 && q.nextCue(1) == 0);
    Project s = q;
    s.cues[1].gotoCue.clear();
    s.loopPlaylist = false;
    CHECK(s.nextCue(1) == -1);
    s.loopPlaylist = true;
    CHECK(s.nextCue(1) == 0);
    CHECK(Project().nextCue(0) == -1);

    // Normalização: -20 LUFS para alvo -16 = +4 dB, mas o pico (-6) só deixa +5.
    CHECK(std::abs(x.gainDb(-16.0) - (-3.0 + 4.0)) < 1e-9);
    CHECK(std::abs(normalizationGainDb(-30.0, -3.0, -16.0) - 2.0) < 1e-9); // limitado pelo pico
    CHECK(normalizationGainDb(-80.0, std::nullopt, -16.0) == 0.0);         // silêncio
    CHECK(normalizationGainDb(-60.0, std::nullopt, -16.0) == 20.0);        // máximo +20
    Cue off = x;
    off.normalize = false;
    CHECK(off.gainDb(-16.0) == -3.0);
}

static void testProjectTolerance() {
    const std::string bad = R"({
      "format":"vthost-project","version":2,"width":999999,"height":"x",
      "cues":[{"id":"Q1","path":"a.mp4","in":-5,"out":3,"speed":99,"playCount":-2,"fadeIn":1e9,
               "volumeDb":80,"color":"azul","goto":"QX","loudness":"alto","scale":0,"thumb":-3},
              {"id":"Q1","path":"b.mp4"},{"path":""},5,
              {"path":"c.mp4","out":0.01}],
      "outputs":[{"id":"O1","monitor":-5},{"id":"O1"}]
    })";
    const auto r = parseProject(bad);
    CHECK(r.project.has_value());
    if (!r.project) return;
    const Project& p = *r.project;
    CHECK(p.width == kMaxCanvasSize && p.height == 1080);
    CHECK(p.cues.size() == 3);
    const Cue& a = p.cues[0];
    CHECK(a.inPoint == 0.0 && a.outPoint == 3.0 && a.speed == 4.0 && a.playCount == 0 && a.fadeIn == 60.0);
    CHECK(a.volumeDb == 12.0 && a.color.empty() && a.gotoCue.empty() && !a.loudness && a.scale == 0.01 && !a.thumbTime);
    CHECK(p.cues[1].id != "Q1" && !p.cues[1].id.empty()); // id repetido recebe um novo
    CHECK(p.cues[2].outPoint == 0.0);                      // trim minúsculo anulado
    CHECK(p.outputs.size() == 1 && p.outputs[0].monitor == 0);
    CHECK(!r.warnings.empty());
    CHECK(!parseProject("{}").project);
    CHECK(!parseProject("lixo").project);

    // Projecto gravado na 0.8.0 (formato 3, CUEPLAYER): abre sem avisos e as cues ficam.
    auto v3 = parseProject(R"({"format":"vthost-project","version":3,"cues":[{"id":"Q1","path":"a.mp4"}],
        "compositions":[{"id":"comp1","layers":[]}],"outputs":[{"id":"O1","slices":[{"id":"S1"}]}]})");
    CHECK(v3.project && v3.project->cues.size() == 1 && v3.project->outputs.size() == 1);
    CHECK(v3.warnings.empty());
    auto future = parseProject(R"({"format":"vthost-project","version":9,"cues":[]})");
    CHECK(future.project && !future.warnings.empty());

    // Projecto antigo (0.6): cada media passa a uma cue.
    auto old = parseProject(R"({"format":"vthost-project","version":1,"canvases":[{"id":"C1","width":1280,"height":720}],
        "media":[{"id":"m1","path":"a.mp4","loopMode":"infinite"},{"id":"m2","path":"b.mov","loopMode":"once-freeze"}],
        "outputs":[{"id":"O1","type":"ndi"}]})");
    CHECK(old.project && old.project->cues.size() == 2 && old.project->width == 1280);
    if (old.project && old.project->cues.size() == 2) {
        CHECK(old.project->cues[0].playCount == 0 && old.project->cues[1].playCount == 1);
        CHECK(old.project->outputs.size() == 1 && old.project->outputs[0].type == OutputType::Ndi);
    }
}

static void testProtocol() {
    using namespace vthost::protocol;
    auto play = parseCommand(R"({"cmd":"play","cue":3,"paused":true})");
    CHECK(play.command && play.command->type == CommandType::Play && play.command->cue == 3 && play.command->paused &&
          !play.command->cut);
    CHECK(!parseCommand(R"({"cmd":"play"})").command);
    CHECK(!parseCommand(R"({"cmd":"play","cue":-1})").command);
    CHECK(!parseCommand(R"({"cmd":"transport","action":"fly"})").command);
    CHECK(!parseCommand(R"({"cmd":"seek"})").command);
    CHECK(!parseCommand("nada").command);
    CHECK(!parseCommand(R"({"cmd":"explodir"})").command);
    auto neg = parseCommand(R"({"cmd":"seek","time":-4})");
    CHECK(neg.command && neg.command->time == 0.0);

    // Ida e volta de todos os comandos.
    std::vector<Command> all;
    for (int t = 0; t <= static_cast<int>(CommandType::Restart); ++t) {
        Command c;
        c.type = static_cast<CommandType>(t);
        c.path = "C:/shows/ação.vthost";
        c.cue = 7;
        c.cut = true;
        c.on = true;
        c.time = 12.5;
        c.action = TransportAction::Stop;
        c.settings.audioDevice = "{0.0.0.00000000}.{abc}";
        c.settings.previewFps = 25;
        c.settings.lockCursor = false;
        all.push_back(c);
    }
    for (const auto& c : all) {
        auto b = parseCommand(serializeCommand(c));
        CHECK(b.command && b.command->type == c.type);
        if (!b.command) continue;
        switch (c.type) {
        case CommandType::LoadProject: CHECK(b.command->path == c.path); break;
        case CommandType::Play: CHECK(b.command->cue == 7 && b.command->cut && !b.command->paused); break;
        case CommandType::Transport: CHECK(b.command->action == TransportAction::Stop); break;
        case CommandType::Seek:
        case CommandType::SeekRemaining: CHECK(b.command->time == 12.5); break;
        case CommandType::SetSettings: CHECK(b.command->settings == c.settings); break;
        case CommandType::SetShowMode:
        case CommandType::SetTestCard: CHECK(b.command->on); break;
        default: break;
        }
    }

    EngineStatus s;
    s.version = "0.7.0";
    s.cue = 2;
    s.cueId = "Q3";
    s.state = "playing";
    s.mediaTime = 14.0;
    s.elapsed = 4.0;
    s.remaining = 26.0;
    s.passDuration = 10.0;
    s.pass = 1;
    s.nextCue = 3;
    s.audioOk = true;
    s.audioDevice = "Altifalantes";
    s.levelL = 0.5;
    s.renderMs = 2.5;
    s.vramUsedMb = 512.0;
    s.vramBudgetMb = 7000.0;
    s.gpuName = "NVIDIA GeForce RTX 5060 Ti";
    s.outputs.push_back({"O1", 1, true, "", "display", "OMEN 27"});
    auto r = parseResponse(statusResponse(s));
    CHECK(r.ok && r.status);
    if (r.status) {
        const auto& x = *r.status;
        CHECK(x.cue == 2 && x.cueId == "Q3" && x.state == "playing" && x.mediaTime == 14.0 && x.elapsed == 4.0);
        CHECK(x.remaining == 26.0 && x.passDuration == 10.0 && x.pass == 1 && x.nextCue == 3 && !x.infinite);
        CHECK(x.audioOk && x.audioDevice == "Altifalantes" && x.levelL == 0.5);
        CHECK(x.outputs.size() == 1 && x.outputs[0].detail == "OMEN 27");
        CHECK(x.renderMs == 2.5 && x.vramUsedMb == 512.0 && x.vramBudgetMb == 7000.0);
        CHECK(x.gpuName == "NVIDIA GeForce RTX 5060 Ti");
    }
    DisplayInfo d;
    d.id = "\\\\.\\DISPLAY2";
    d.name = "OMEN 27";
    d.width = 2560;
    d.index = 1;
    auto dr = parseResponse(displaysResponse({d}));
    CHECK(dr.ok && dr.displays && dr.displays->size() == 1 && dr.displays->front().name == "OMEN 27");
    auto ar = parseResponse(audioDevicesResponse({{"id1", "Colunas", true}}));
    CHECK(ar.ok && ar.audioDevices && ar.audioDevices->size() == 1 && ar.audioDevices->front().isDefault);
    auto e = parseResponse(errorResponse("falhou"));
    CHECK(!e.ok && e.error == "falhou");
}

static void testShowState() {
    ShowState s;
    s.projectPath = "C:/shows/gala.vthost";
    s.showMode = true;
    s.cueId = "Q4";
    s.cueTime = 33.25;
    s.paused = true;
    ShowState b;
    CHECK(parseShowState(serializeShowState(s), b));
    CHECK(b.projectPath == s.projectPath && b.showMode && b.cueId == "Q4" && b.cueTime == 33.25 && b.paused);
    CHECK(!parseShowState("{}", b));
    CHECK(!parseShowState(R"({"format":"outro"})", b));
}

static void testCountdown() {
    Project p;
    Cue a = makeCue("C:/v/Abertura.mp4");
    a.id = "Q1";
    a.label = "INTRO";
    Cue b = makeCue("C:/v/Break.mov");
    b.id = "Q2";
    p.cues = {a, b};
    protocol::EngineStatus s;
    CHECK(!computeCountdown(p, s).active); // nada no ar
    s.cue = 0;
    s.state = "playing";
    s.passDuration = 80.0;
    s.elapsed = 20.0;
    s.remaining = 60.0;
    s.nextCue = 1;
    auto c = computeCountdown(p, s);
    CHECK(c.active && c.running && c.remaining == 60.0 && c.duration == 80.0 && c.elapsed == 20.0);
    CHECK(c.cueLabel == "INTRO" && c.cueNumber == 1 && c.nextName == "Break.mov" && c.mediaName == "Abertura");
    auto adv = advanceCountdown(c, 2.5);
    CHECK(std::abs(adv.remaining - 57.5) < 1e-9);
    CHECK(advanceCountdown(c, 500).remaining == 0.0);
    // Segunda passagem de 3: conta o total.
    s.pass = 1;
    s.remaining = 140.0;
    c = computeCountdown(p, s);
    CHECK(c.elapsed == 100.0 && c.duration == 240.0);
    // Pausa: não anda.
    s.state = "paused";
    c = computeCountdown(p, s);
    CHECK(!c.running && advanceCountdown(c, 3).remaining == 140.0);
    // Loop infinito: conta a passagem e recomeça.
    s.state = "playing";
    s.infinite = true;
    s.pass = 5;
    s.elapsed = 70.0;
    c = computeCountdown(p, s);
    CHECK(c.loops && c.remaining == 10.0 && c.duration == 80.0);
    CHECK(std::abs(advanceCountdown(c, 15).remaining - 75.0) < 1e-9);
    s.state = "stopped";
    CHECK(!computeCountdown(p, s).active);
    CHECK(formatCountdown(0.2) == "00:01" && formatCountdown(61) == "01:01" && formatCountdown(3725) == "1:02:05");
    CHECK(formatCountdown(-4) == "00:00" && formatCountdown(std::nan("")) == "00:00");
    CHECK(countdownJson(computeCountdown(p, protocol::EngineStatus{}), "Show").find("\"active\":false") != std::string::npos);
}

static void testLoudness() {
    // EBU Tech 3341: seno de 1 kHz a -23 dBFS em L e R = -23 LUFS (±0.1).
    const int rate = 48000;
    const double amp = std::pow(10.0, -23.0 / 20.0);
    std::vector<float> buf(static_cast<std::size_t>(rate) * 2 * 20);
    for (std::size_t i = 0; i < buf.size() / 2; ++i) {
        const float v = static_cast<float>(amp * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / rate));
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
    LoudnessMeter m(rate, 2);
    m.add(buf.data(), buf.size() / 2);
    auto l = m.integrated();
    CHECK(l && std::abs(*l - (-23.0)) < 0.1);
    CHECK(m.peakDb() && std::abs(*m.peakDb() - (-23.0)) < 0.05);
    CHECK(std::abs(m.seconds() - 20.0) < 1e-9);
    // A 44.1 kHz dá o mesmo.
    LoudnessMeter m44(44100, 2);
    std::vector<float> b44(44100 * 2 * 10);
    for (std::size_t i = 0; i < b44.size() / 2; ++i) {
        const float v = static_cast<float>(amp * std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 44100.0));
        b44[2 * i] = v;
        b44[2 * i + 1] = v;
    }
    m44.add(b44.data(), b44.size() / 2);
    CHECK(m44.integrated() && std::abs(*m44.integrated() - (-23.0)) < 0.1);
    // Silêncio e entradas estranhas.
    LoudnessMeter s(48000, 2);
    std::vector<float> zero(48000 * 2 * 3, 0.0f);
    s.add(zero.data(), zero.size() / 2);
    CHECK(!s.integrated() && !s.peakDb());
    std::vector<float> nan(1000, std::nanf(""));
    s.add(nan.data(), 500);
    s.add(nullptr, 100);
    CHECK(!s.integrated());
    // Porta relativa: 10 s a -23 + 10 s a -60 -> o silêncio relativo é ignorado.
    LoudnessMeter g(rate, 2);
    g.add(buf.data(), buf.size() / 4);
    std::vector<float> quiet(buf.size() / 2);
    const double qa = std::pow(10.0, -60.0 / 20.0) / amp;
    for (std::size_t i = 0; i < quiet.size(); ++i) quiet[i] = static_cast<float>(buf[i] * qa);
    g.add(quiet.data(), quiet.size() / 2);
    CHECK(g.integrated() && std::abs(*g.integrated() - (-23.0)) < 0.2);
}

void testOsc() {
    using namespace vthost::osc;
    // Ida e volta com os três tipos.
    Message m{"/mitti/playCueWithCueID", {std::string("12"), std::int32_t{3}, 0.5f}};
    const auto bytes = encodeMessage(m);
    CHECK(bytes.size() % 4 == 0);
    auto d = decodePacket(bytes.data(), bytes.size());
    CHECK(d.size() == 1 && d[0].address == m.address && d[0].args.size() == 3);
    if (d.size() == 1 && d[0].args.size() == 3) {
        CHECK(d[0].stringArg(0) == "12" && d[0].stringArg(1) == "3");
        double v = 0;
        CHECK(d[0].numberArg(v, 2) && v == 0.5);
        CHECK(d[0].numberArg(v, 0) && v == 12.0);
    }
    // Sem argumentos (tag ",") e sem type tag nenhuma.
    auto e = encodeMessage({"/mitti/play", {}});
    d = decodePacket(e.data(), e.size());
    CHECK(d.size() == 1 && d[0].address == "/mitti/play" && d[0].args.empty());
    const std::uint8_t bare[] = {'/', 'a', 0, 0};
    CHECK(decodePacket(bare, sizeof bare).size() == 1);
    // Bundle com duas mensagens.
    std::vector<std::uint8_t> b = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1};
    for (const auto& part : {encodeMessage({"/mitti/goto30", {}}), encodeMessage({"/mitti/togglePlay", {}})}) {
        const auto n = static_cast<std::uint32_t>(part.size());
        b.insert(b.end(), {static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16),
                           static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)});
        b.insert(b.end(), part.begin(), part.end());
    }
    d = decodePacket(b.data(), b.size());
    CHECK(d.size() == 2 && d[1].address == "/mitti/togglePlay");
    // Lixo: nunca lança, devolve nada.
    const std::uint8_t junk[] = {'x', 'y', 'z', 0};
    CHECK(decodePacket(junk, sizeof junk).empty());
    CHECK(decodePacket(nullptr, 0).empty());
    std::mt19937 rng(7);
    for (int i = 0; i < 3000; ++i) {
        std::vector<std::uint8_t> r(static_cast<std::size_t>(4 * (rng() % 40)));
        for (auto& c : r) c = static_cast<std::uint8_t>(rng());
        if (!r.empty() && rng() % 2) r[0] = '/';
        (void)decodePacket(r.data(), r.size());
    }
    // String sem terminador dentro do pacote: rejeitada.
    const std::uint8_t open[] = {'/', 'a', 'b', 'c'};
    CHECK(decodePacket(open, sizeof open).empty());
}

void testEngineConfig() {
    EngineConfig c;
    c.gpu = "NVIDIA GeForce RTX 4060 Laptop GPU";
    CHECK(engineConfigFromJson(engineConfigToJson(c)) == c);
    CHECK(engineConfigFromJson("lixo").gpu.empty());
    CHECK(engineConfigFromJson(R"({"gpu":5})").gpu.empty());
    CHECK(engineConfigFromJson("{}").gpu.empty());
    const auto dir = std::filesystem::temp_directory_path() / "vthost_test_engcfg";
    std::filesystem::create_directories(dir);
    const auto f = dir / "engine_config.json";
    CHECK(saveEngineConfig(f, c));
    CHECK(loadEngineConfig(f) == c);
    CHECK(loadEngineConfig(dir / "nao_existe.json").gpu.empty());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

void testDsmLayout() {
    // 1920x1080 com canvas 16:9: vídeo encolhido em cima, faixa por baixo, tudo dentro da saída.
    const DsmLayout l = computeDsmLayout(1920, 1080, 1920, 1080);
    CHECK(l.videoW < 1.0 && l.videoH < 1.0 && l.videoX > 0 && l.videoY > 0);
    CHECK(std::abs(l.videoW * 1920 / (l.videoH * 1080) - 16.0 / 9.0) < 1e-6);
    CHECK(l.panelY >= l.videoY + l.videoH && l.panelY + l.panelH <= 1.0 + 1e-9 && l.panelH > 0.1);
    CHECK(std::abs(l.panelX - l.videoX) < 1e-9 && std::abs(l.panelW - l.videoW) < 1e-9);
    // Canvas muito largo numa saída 4:3: continua dentro e com a proporção.
    const DsmLayout w = computeDsmLayout(1024, 768, 3840, 1080);
    CHECK(w.videoX >= 0 && w.videoX + w.videoW <= 1.0 + 1e-9);
    CHECK(std::abs(w.videoW * 1024 / (w.videoH * 768) - 3840.0 / 1080.0) < 1e-6);
    // Entradas inválidas não rebentam.
    const DsmLayout z = computeDsmLayout(0, 0, 0, 0);
    CHECK(z.videoW == 1 && z.panelH == 0);
}

void testTally() {
    using namespace vthost::tally;
    CHECK(vmixProgram("TALLY OK 0121\r\n", 2) == std::optional<bool>(true));
    CHECK(vmixProgram("TALLY OK 0121", 3) == std::optional<bool>(false));
    CHECK(vmixProgram("TALLY OK 0121", 9) == std::nullopt);
    CHECK(vmixProgram("SUBSCRIBE OK TALLY", 1) == std::nullopt);
    CHECK(vmixProgram("TALLY OK ", 1) == std::nullopt && vmixProgram("", 1) == std::nullopt);

    const auto hello = atemHello();
    AtemHeader h;
    CHECK(parseAtemHeader(hello.data(), hello.size(), h) && h.flags == kFlagNewSession && h.length == 20 && h.session == 0x53ab);
    const auto ack = atemAck(0x8123, 42);
    CHECK(parseAtemHeader(ack.data(), ack.size(), h) && h.flags == kFlagAckReply && h.session == 0x8123 && h.ackId == 42);
    CHECK(!parseAtemHeader(ack.data(), 11, h));
    std::vector<std::uint8_t> bad = ack;
    bad[1] = 13; // comprimento não bate
    CHECK(!parseAtemHeader(bad.data(), bad.size(), h));

    // Pacote com dois comandos: TlSr (fontes 1 e 3) e InCm.
    std::vector<std::uint8_t> pl = {0, 17, 0, 0, 'T', 'l', 'S', 'r', 0, 3, 0, 1, 0x01, 0, 2, 0x02, 0, 3};
    pl.resize(17); // fonte 3 sem byte de estado: lista truncada
    pl[0] = 0;
    pl[1] = 17;
    std::vector<std::uint8_t> full = {0, 18, 0, 0, 'T', 'l', 'S', 'r', 0, 3, 0, 1, 0x01, 0, 2, 0x02, 0, 3};
    full.push_back(0x03);
    full[1] = 19;
    const std::uint8_t incm[] = {0, 12, 0, 0, 'I', 'n', 'C', 'm', 1, 2, 3, 4};
    full.insert(full.end(), incm, incm + sizeof incm);
    const auto cmds = parseAtemCommands(full.data(), full.size());
    CHECK(cmds.size() == 2 && cmds[0].name == "TlSr" && cmds[1].name == "InCm");
    if (cmds.size() == 2) {
        CHECK(atemTallyProgram(cmds[0], 1) == std::optional<bool>(true));
        CHECK(atemTallyProgram(cmds[0], 2) == std::optional<bool>(false));
        CHECK(atemTallyProgram(cmds[0], 3) == std::optional<bool>(true));
        CHECK(atemTallyProgram(cmds[0], 4) == std::nullopt);
        CHECK(atemTallyProgram(cmds[1], 1) == std::nullopt);
    }
    const auto cut = parseAtemCommands(pl.data(), pl.size());
    CHECK(cut.size() == 1 && atemTallyProgram(cut[0], 3) == std::nullopt);
    // Lixo: não rebenta.
    std::mt19937 rng(11);
    for (int i = 0; i < 2000; ++i) {
        std::vector<std::uint8_t> r(rng() % 64);
        for (auto& c : r) c = static_cast<std::uint8_t>(rng());
        for (const auto& c : parseAtemCommands(r.data(), r.size())) (void)atemTallyProgram(c, 1);
        (void)parseAtemHeader(r.data(), r.size(), h);
    }
}

int main() {
    testLoudness();
    testProject();
    testProjectTolerance();
    testProtocol();
    testShowState();
    testCountdown();
    testQuadAndPreview();
    testMapping();
    testLayerProps();
    testVersion();
    testFuzzNeverThrows();
    testAtomicWrite();
    testOsc();
    testEngineConfig();
    testDsmLayout();
    testTally();
    std::printf("%d verificações, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
