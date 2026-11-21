// Testes do núcleo. Sem framework: cada CHECK que falha é contado e o programa
// sai com código != 0 (o CTest/CI marca a build como falhada).

#include "vthost/core/atomic_file.h"
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
    p.layers[1].opacity = 0.25;
    p.layers[1].blend = BlendMode::Add;

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
    CHECK(r.project->layers[1].opacity == 0.25);
    CHECK(r.project->layers[1].blend == BlendMode::Add);
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
    CHECK(r.project->layers[0].opacity == 1.0);
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

    auto o = parseCommand(R"({"cmd":"set_layer_opacity","layer":"L1","opacity":5})");
    CHECK(o.command && o.command->value == 1.0);

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
    s.outputs.push_back({"O1", 1, true, ""});
    auto r = parseResponse(statusResponse(s));
    CHECK(r.ok && r.status);
    if (r.status) {
        CHECK(r.status->activeColumn == 2);
        CHECK(r.status->layers.size() == 1 && r.status->layers[0].position == 12.5);
        CHECK(r.status->outputs.size() == 1 && r.status->outputs[0].monitor == 1);
    }
    auto e = parseResponse(errorResponse("falhou"));
    CHECK(!e.ok && e.error == "falhou");
}

static void testShowState() {
    ShowState s;
    s.projectPath = "C:/x.vthost";
    s.activeColumn = 1;
    s.showMode = true;
    s.layers.push_back({"L1", "m1", 33.3, 0.5});
    ShowState back;
    CHECK(parseShowState(serializeShowState(s), back));
    CHECK(back.projectPath == s.projectPath && back.activeColumn == 1 && back.showMode);
    CHECK(back.layers.size() == 1 && back.layers[0].positionSeconds == 33.3);
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

int main() {
    testVersion();
    testProjectRoundTrip();
    testProjectTolerance();
    testFuzzNeverThrows();
    testProtocol();
    testShowState();
    testAtomicWrite();
    std::printf("%d verificações, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
