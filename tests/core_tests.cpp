// Testes do núcleo. Sem framework: cada CHECK que falha é contado e o programa
// sai com código != 0 (o CTest/CI marca a build como falhada).

#include "vthost/core/atomic_file.h"
#include "vthost/core/mapping.h"
#include "vthost/core/preview.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"
#include "vthost/core/show_state.h"
#include "vthost/core/version.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
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

static void testProjectRoundTrip() {
    Project p = Project::makeDefault();
    p.name = "Ação — Teste";
    p.media.push_back({"m1", "C:/media/vídeo 1.mp4", LoopMode::AutoFollow});
    p.columns[0].cells["L1"] = "m1";
    p.layers[1].props.opacity = 0.25;
    p.layers[1].props.blend = BlendMode::Add;
    p.layers[1].props.posX = -120.5;
    p.layers[1].props.scale = 0.5;
    p.layers[1].props.rotation = 45.0;
    p.timelines[0].clips.push_back({"c1", "L1", "m1", 2.0, 8.0, 1.0, 1.5, 2.0});
    p.layers[0].locked = true;
    Output ndi;
    ndi.id = "O2";
    ndi.name = "Rede";
    ndi.type = OutputType::Ndi;
    ndi.ndiName = "Palco";
    ndi.ndiAlpha = true;
    p.outputs.push_back(ndi);
    p.outputs[0].displayId = "\\\\.\\DISPLAY2";
    p.timelines[0].segments.push_back({"intro", 0.0, 10.0, true});

    const auto text = serializeProject(p);
    const auto r = parseProject(text);
    CHECK(r.project.has_value());
    CHECK(r.warnings.empty());
    if (!r.project) return;
    CHECK(r.project->name == p.name);
    CHECK(r.project->media.size() == 1);
    CHECK(r.project->media[0].path == "C:/media/vídeo 1.mp4");
    CHECK(r.project->media[0].loopMode == LoopMode::AutoFollow);
    CHECK(r.project->columns.size() == 4);
    CHECK(r.project->columns[0].cells.at("L1") == "m1");
    CHECK(r.project->layers[1].props == p.layers[1].props);
    CHECK(r.project->layers[0].locked && !r.project->layers[1].locked);
    CHECK(r.project->outputs.size() == 2);
    if (r.project->outputs.size() == 2) {
        CHECK(r.project->outputs[0].type == OutputType::Display && r.project->outputs[0].displayId == p.outputs[0].displayId);
        CHECK(r.project->outputs[1].type == OutputType::Ndi && r.project->outputs[1].ndiName == "Palco" &&
              r.project->outputs[1].ndiAlpha);
    }
    CHECK(r.project->timelines.size() == 1);
    if (!r.project->timelines.empty()) {
        const auto& t = r.project->timelines[0];
        CHECK(t.clips.size() == 1 && t.clips[0].start == 2.0 && t.clips[0].inPoint == 1.0);
        CHECK(t.clips.size() == 1 && t.clips[0].fadeIn == 1.5 && t.clips[0].fadeOut == 2.0);
        CHECK(t.segments.size() == 1 && t.segments[0].loop);
    }
}

static void testCompatAndFades() {
    // Projeto da 0.1/0.2: "loop" booleano e outputs só com monitor.
    const auto r = parseProject(R"({"format":"vthost-project","media":[{"id":"a","path":"a.mp4","loop":false},
        {"id":"b","path":"b.mp4"}],"outputs":[{"id":"O1","monitor":2,"fullscreen":true}]})");
    CHECK(r.project.has_value());
    if (r.project) {
        CHECK(r.project->media[0].loopMode == LoopMode::OnceFreeze);
        CHECK(r.project->media[1].loopMode == LoopMode::Infinite);
        CHECK(r.project->outputs[0].type == OutputType::Display && r.project->outputs[0].monitor == 2 &&
              r.project->outputs[0].enabled);
    }
    Clip c{"c", "L", "m", 10.0, 10.0, 0.0, 2.0, 4.0};
    CHECK(c.opacityAt(10.0) == 0.0);
    CHECK(c.opacityAt(11.0) == 0.5);
    CHECK(c.opacityAt(15.0) == 1.0);
    CHECK(c.opacityAt(18.0) == 0.5);
    CHECK(c.opacityAt(20.0) == 0.0);
    CHECK(loopModeFromString(toString(LoopMode::OnceStop)) == LoopMode::OnceStop);
    CHECK(outputTypeFromString("lixo") == OutputType::Display);
}

static void testProjectTolerance() {
    // Valores fora do intervalo, tipos errados e referências partidas não podem
    // impedir o projecto de abrir.
    const std::string bad = R"({
      "format":"vthost-project","version":1,
      "canvas":{"width":999999,"height":"x"},
      "media":[{"id":"m1","path":"a.mp4"},{"id":"m1","path":"dup.mp4"},{"id":"","path":"x"}, 5],
      "layers":[{"id":"L1","opacity":7},{"id":"L1"}],
      "columns":[{"name":"c","fade":-3,"cells":{"L1":"m1","L9":"m1","L1x":"nope"}}],
      "outputs":[{"id":"O1","monitor":-5}]
    })";
    const auto r = parseProject(bad);
    CHECK(r.project.has_value());
    if (!r.project) return;
    CHECK(r.project->canvases.size() == 1);
    CHECK(r.project->canvases[0].width == kMaxCanvasSize && r.project->canvases[0].height == 1080);
    CHECK(r.project->media.size() == 1);
    CHECK(r.project->layers.size() == 1);
    CHECK(r.project->layers[0].props.opacity == 1.0);
    CHECK(r.project->columns[0].fadeSeconds == 0.0);
    CHECK(r.project->columns[0].cells.size() == 1);
    CHECK(r.project->outputs[0].monitor == 0);
    CHECK(!r.warnings.empty());

    CHECK(!parseProject("").project);
    CHECK(!parseProject("{").project);
    CHECK(!parseProject("[]").project);
    CHECK(!parseProject(R"({"format":"outra-coisa"})").project);
}

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

static void testProtocol() {
    using namespace protocol;
    auto c = parseCommand(R"({"cmd":"go","column":3})");
    CHECK(c.command && c.command->type == CommandType::Go && c.command->column == 3 && c.command->value == -1.0);
    auto cut = parseCommand(R"({"cmd":"go","column":1,"fade":0})");
    CHECK(cut.command && cut.command->value == 0.0);
    Command st;
    st.type = CommandType::SetSettings;
    st.settings.vsync = false;
    st.settings.previewFps = 15;
    st.settings.priority = "high";
    st.settings.loopBoard = true;
    auto sb = parseCommand(serializeCommand(st));
    CHECK(sb.command && sb.command->type == CommandType::SetSettings && sb.command->settings == st.settings);
    auto bad = parseCommand(R"({"cmd":"set_settings","previewFps":1000,"priority":"realtime"})");
    CHECK(bad.command && bad.command->settings.previewFps == 60 && bad.command->settings.priority == "above_normal");
    CHECK(!parseCommand(R"({"cmd":"go","column":-1})").command);
    CHECK(!parseCommand(R"({"cmd":"go"})").command);
    CHECK(!parseCommand(R"({"cmd":"rm -rf"})").command);
    CHECK(!parseCommand("não é json").command);

    auto o = parseCommand(R"({"cmd":"set_layer_props","layer":"L1","props":{"opacity":5,"scale":0,"blend":"screen"}})");
    CHECK(o.command && o.command->props.opacity == 1.0 && o.command->props.scale == 0.01 &&
          o.command->props.blend == BlendMode::Screen);
    CHECK(!parseCommand(R"({"cmd":"set_layer_props","layer":"L1"})").command);

    Command props;
    props.type = CommandType::SetLayerProps;
    props.layer = "L2";
    props.props.posX = 10;
    props.props.rotation = -90;
    props.props.blend = BlendMode::Multiply;
    auto pb = parseCommand(serializeCommand(props));
    CHECK(pb.command && pb.command->props == props.props && pb.command->layer == "L2");

    auto tl = parseCommand(R"({"cmd":"timeline","action":"seek","timeline":"T1","time":12.5})");
    CHECK(tl.command && tl.command->action == TimelineAction::Seek && tl.command->value == 12.5);
    CHECK(!parseCommand(R"({"cmd":"timeline","action":"play"})").command);
    CHECK(!parseCommand(R"({"cmd":"timeline","action":"explode","timeline":"T1"})").command);
    auto tstop = parseCommand(R"({"cmd":"timeline","action":"stop"})");
    CHECK(tstop.command && tstop.command->value == -1.0);

    Command lp;
    lp.type = CommandType::LoadProject;
    lp.path = "C:/shows/Ação.vthost";
    auto back = parseCommand(serializeCommand(lp));
    CHECK(back.command && back.command->path == lp.path);

    EngineStatus s;
    s.version = "0.1.0";
    s.activeColumn = 2;
    s.fps = 59.94;
    s.layers.push_back({"L1", "m1", "playing", 12.5, ""});
    s.timelineId = "T1";
    s.timelineTime = 3.25;
    s.timelinePlaying = true;
    s.outputs.push_back({"O1", 1, true, "", "display", "OMEN 27"});
    auto r = parseResponse(statusResponse(s));
    CHECK(r.ok && r.status);
    if (r.status) {
        CHECK(r.status->activeColumn == 2);
        CHECK(r.status->layers.size() == 1 && r.status->layers[0].position == 12.5);
        CHECK(r.status->outputs.size() == 1 && r.status->outputs[0].monitor == 1 && r.status->outputs[0].detail == "OMEN 27");
        CHECK(r.status->timelineId == "T1" && r.status->timelineTime == 3.25 && r.status->timelinePlaying);
    }
    DisplayInfo d;
    d.id = "\\\\.\\DISPLAY2";
    d.name = "OMEN 27";
    d.adapter = "NVIDIA GeForce RTX 5060 Ti";
    d.width = 2560;
    d.height = 1440;
    d.refreshHz = 143.97;
    d.index = 1;
    auto dr = parseResponse(displaysResponse({d}));
    CHECK(dr.ok && dr.displays && dr.displays->size() == 1);
    if (dr.displays && !dr.displays->empty()) {
        const auto& x = dr.displays->front();
        CHECK(x.id == d.id && x.name == "OMEN 27" && x.adapter == d.adapter && x.width == 2560 && x.index == 1);
    }
    CHECK(parseCommand(R"({"cmd":"list_displays"})").command->type == CommandType::ListDisplays);
    Command vis;
    vis.type = CommandType::SetLayerVisible;
    vis.layer = "L2";
    vis.on = false;
    auto vb = parseCommand(serializeCommand(vis));
    CHECK(vb.command && vb.command->type == CommandType::SetLayerVisible && vb.command->layer == "L2" && !vb.command->on);
    CHECK(!parseCommand(R"({"cmd":"set_layer_visible","layer":"L1"})").command);

    auto e = parseResponse(errorResponse("falhou"));
    CHECK(!e.ok && e.error == "falhou");
}

static void testShowState() {
    ShowState s;
    s.projectPath = "C:/x.vthost";
    s.activeColumn = 1;
    s.showMode = true;
    LayerProps lp;
    lp.opacity = 0.5;
    lp.scale = 2.0;
    lp.blend = BlendMode::Add;
    s.layers.push_back({"L1", "m1", 33.3, lp});
    s.timelineId = "T1";
    s.timelineTime = 7.5;
    ShowState back;
    CHECK(parseShowState(serializeShowState(s), back));
    CHECK(back.projectPath == s.projectPath && back.activeColumn == 1 && back.showMode);
    CHECK(back.layers.size() == 1 && back.layers[0].positionSeconds == 33.3);
    CHECK(back.layers.size() == 1 && back.layers[0].props == lp);
    CHECK(back.timelineId == "T1" && back.timelineTime == 7.5 && !back.timelinePlaying);
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

static void testTimelineAndQuad() {
    Timeline t;
    t.clips.push_back({"a", "L1", "m1", 0.0, 10.0, 0.0});
    t.clips.push_back({"b", "L1", "m2", 5.0, 10.0, 0.0}); // sobrepõe: ganha o mais recente
    CHECK(t.clipAt("L1", 2.0) && t.clipAt("L1", 2.0)->id == "a");
    CHECK(t.clipAt("L1", 6.0) && t.clipAt("L1", 6.0)->id == "b");
    CHECK(!t.clipAt("L1", 15.0));
    CHECK(!t.clipAt("L2", 2.0));
    CHECK(t.duration() == 60.0);
    t.clips.push_back({"c", "L1", "m1", 100.0, 20.0, 0.0});
    CHECK(t.duration() == 120.0);

    // Media 4:3 num canvas 16:9 => encaixa pela altura.
    LayerProps p;
    auto q = computeLayerQuad(p, 1440, 1080, 1920, 1080);
    CHECK(q.cx == 960.0 && q.cy == 540.0 && q.halfW == 720.0 && q.halfH == 540.0);
    p.scale = 0.5;
    p.posX = 100;
    q = computeLayerQuad(p, 1440, 1080, 1920, 1080);
    CHECK(q.cx == 1060.0 && q.halfW == 360.0);

    // Segmentos de 0 s são descartados (evitam loops infinitos).
    const std::string j = R"({"format":"vthost-project","layers":[{"id":"L1"}],"media":[{"id":"m","path":"x"}],
      "timelines":[{"id":"T","segments":[{"start":5,"end":5,"loop":true},{"start":0,"end":4,"loop":true}],
                    "clips":[{"id":"c","layer":"L1","media":"m","start":1,"duration":-5},
                             {"id":"d","layer":"LX","media":"m"}]}]})";
    auto r = parseProject(j);
    CHECK(r.project && r.project->timelines.size() == 1);
    if (r.project && !r.project->timelines.empty()) {
        CHECK(r.project->timelines[0].segments.size() == 1);
        CHECK(r.project->timelines[0].clips.size() == 1 && r.project->timelines[0].clips[0].duration > 0.0);
    }

    int w = 0, h = 0;
    previewSizeFor(1920, 1080, w, h);
    CHECK(w == 960 && h == 540);
    previewSizeFor(1080, 1920, w, h);
    CHECK(h == 540 && w < 960);
    previewSizeFor(640, 360, w, h);
    CHECK(w == 640 && h == 360);
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

    // Projeto com vários canvas, layers e saídas por canvas + mapping.
    Project pr = Project::makeDefault();
    pr.canvases.push_back({"C2", "LED", 3840, 1080});
    pr.layers[1].canvasId = "C2";
    pr.outputs[0].canvasId = "C2";
    pr.outputs[0].mapping = cells[1].mapping;
    pr.outputs[0].mapping.mode = WarpMode::Mesh;
    pr.outputs[0].mapping.mesh[0] = {0.02, 0.03};
    auto rr = parseProject(serializeProject(pr));
    CHECK(rr.project.has_value());
    if (rr.project) {
        CHECK(rr.project->canvases.size() == 2 && rr.project->canvases[1].width == 3840);
        CHECK(rr.project->layers[1].canvasId == "C2" && rr.project->layers[0].canvasId.empty());
        CHECK(rr.project->outputs[0].mapping == pr.outputs[0].mapping);
        CHECK(rr.project->canvasOf("XX").id == "C1" && rr.project->resolveCanvasId("C2") == "C2");
    }
    // Comando ao vivo.
    protocol::Command mc;
    mc.type = protocol::CommandType::SetOutputMapping;
    mc.output = "O1";
    mc.mapping = pr.outputs[0].mapping;
    auto mb = protocol::parseCommand(protocol::serializeCommand(mc));
    CHECK(mb.command && mb.command->output == "O1" && mb.command->mapping == mc.mapping);
    auto pc = protocol::parseCommand(R"({"cmd":"set_preview_canvas","canvas":"C2"})");
    CHECK(pc.command && pc.command->canvas == "C2");
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
    // Projeto
    Project pr = Project::makeDefault();
    pr.layers[0].props = p;
    auto r = parseProject(serializeProject(pr));
    CHECK(r.project && r.project->layers[0].props == p);
    // Protocolo
    protocol::Command c;
    c.type = protocol::CommandType::SetLayerProps;
    c.layer = "L1";
    c.props = p;
    auto b = protocol::parseCommand(protocol::serializeCommand(c));
    CHECK(b.command && b.command->props == p);
    // Estado do show
    ShowState st;
    st.layers.push_back({"L1", "m", 1.0, p});
    ShowState back;
    CHECK(parseShowState(serializeShowState(st), back) && back.layers[0].props == p);
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

int main() {
    testMapping();
    testLayerProps();
    testVersion();
    testTimelineAndQuad();
    testProjectRoundTrip();
    testCompatAndFades();
    testProjectTolerance();
    testFuzzNeverThrows();
    testProtocol();
    testShowState();
    testAtomicWrite();
    std::printf("%d verificações, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
