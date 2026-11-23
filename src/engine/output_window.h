#pragma once
// Janela de output: sem bordas, ocupa um monitor inteiro, sem cursor, não fecha
// com Alt+F4 e não deixa o Windows ligar o protector de ecrã.
// Se o monitor configurado não existir, abre uma janela de pré-visualização no
// monitor principal e reporta o problema - e volta ao sítio certo quando o
// monitor for ligado.

#include "dsm.h"
#include "renderer.h"
#include "vthost/core/project.h"

#include <windows.h>

#include <string>

namespace vthost {

class OutputWindow {
public:
    // Tipos Display (ecrã inteiro num ecrã) e Window (janela normal).
    OutputWindow(Renderer& renderer, const Output& config);
    ~OutputWindow();
    OutputWindow(const OutputWindow&) = delete;
    OutputWindow& operator=(const OutputWindow&) = delete;

    bool create();
    void present(bool vsync, bool testCard); // desenha o canvas (com barras pretas se o formato diferir)
    // Canvas a mostrar e o seu tamanho (para manter a proporção, sem esticar).
    void setCanvas(const std::string& canvasId, int canvasW, int canvasH);
    void placeOnMonitor();    // reposiciona (ex.: após mudança de monitores)

    const std::string& id() const { return config_.id; }
    int monitor() const { return config_.monitor; }
    const Output& config() const { return config_; }
    const std::string& detail() const { return detail_; }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    bool swapChainLost() const { return lost_; }
    // Ecrã ocupado em ecrã inteiro (vazio se não estiver): o rato não deve entrar.
    const RECT& fullscreenRect() const { return fullscreenRect_; }
    // Modo DSM: tamanho (px) da faixa dos tempos, para a imagem do texto. false = sem DSM.
    bool dsmPanelSize(int& w, int& h);

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool createSwapChain();
    void resize(int w, int h);

    Renderer& renderer_;
    Output config_;
    void updateLetterbox();

    std::string canvasId_ = "C1";
    int canvasW_ = 1920;
    int canvasH_ = 1080;
    OutputMapping mapping_; // canvas inteiro, centrado e com a proporção certa
    WarpMesh mesh_;
    RECT fullscreenRect_{};
    DsmGeometry dsm_;
    std::string detail_;
    HWND hwnd_ = nullptr;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    int width_ = 0;
    int height_ = 0;
    bool ok_ = false;
    bool lost_ = false;
    bool onRightMonitor_ = false;
    std::string error_;
};

} // namespace vthost
