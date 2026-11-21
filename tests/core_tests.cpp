// Testes do núcleo. Sem framework: cada CHECK que falha é contado e o programa
// sai com código != 0 (o CTest/CI marca a build como falhada).

#include "vthost/core/atomic_file.h"
#include "vthost/core/preview.h"
#include "vthost/core/project.h"
#include "vthost/core/protocol.h"
#include "vthost/core/show_state.h"
#include "vthost/core/version.h"

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
    p.media.push_back({"m1", "C:/media/vídeo 1.mp4", true});
    p.columns[0].cells["L1"] = "m1";
    p.layers[1].props.opacity = 0.25;
    p.layers[1].props.blend = BlendMode::Add;
    p.layers[1].props.posX = -120.5;
    p.layers[1].props.scale = 0.5;
    p.layers[1].props.rotation = 45.0;
    p.timelines[0].clips.push_back({"c1", "L1", "m1", 2.0, 8.0, 1.0});
    p.timelines[0].segments.push_back({"intro", 0.0, 10.0, true});

    const auto text = serializeProject(p);
    const auto r = parseProject(text);
    CHECK(r.project.has_value());
    CHECK(r.warnings.empty());
    if (!r.project) return;
    CHECK(r.project->name == p.name);
    CHECK(r.project->media.size() == 1);
    CHECK(r.project->media[0].path == "C:/media/vídeo 1.mp4");
    CHECK(r.project->columns.size() == 4);
    CHECK(r.project->columns[0].cells.at("L1") == "m1");
    CHECK(r.project->layers[1].props == p.layers[1].props);
    CHECK(r.project->timelines.size() == 1);
    if (!r.project->timelines.empty()) {
        const auto& t = r.project->timelines[0];
        CHECK(t.clips.size() == 1 && t.clips[0].start == 2.0 && t.clips[0].inPoint == 1.0);
        CHECK(t.segments.size() == 1 && t.segments[0].loop);
    }
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
    CHECK(r.project->canvasWidth == kMaxCanvasSize);
    CHECK(r.project->canvasHeight == 1080);
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
    CHECK(c.command && c.command->type == CommandType::Go && c.command->column == 3);
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
    s.outputs.push_back({"O1", 1, true, ""});
    auto r = parseResponse(statusResponse(s));
    CHECK(r.ok && r.status);
    if (r.status) {
        CHECK(r.status->activeColumn == 2);
        CHECK(r.status->layers.size() == 1 && r.status->layers[0].position == 12.5);
        CHECK(r.status->outputs.size() == 1 && r.status->outputs[0].monitor == 1);
        CHECK(r.status->timelineId == "T1" && r.status->timelineTime == 3.25 && r.status->timelinePlaying);
    }
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

int main() {
    testVersion();
    testTimelineAndQuad();
    testProjectRoundTrip();
    testProjectTolerance();
    testFuzzNeverThrows();
    testProtocol();
    testShowState();
    testAtomicWrite();
    std::printf("%d verificações, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
