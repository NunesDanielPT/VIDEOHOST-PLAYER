#pragma once
// Janela de output: sem bordas, ocupa um monitor inteiro, sem cursor, não fecha
// com Alt+F4 e não deixa o Windows ligar o protector de ecrã.
// Se o monitor configurado não existir, abre uma janela de pré-visualização no
// monitor principal e reporta o problema - e volta ao sítio certo quando o
// monitor for ligado.

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
    void present(bool vsync); // desenha o canvas e apresenta
    void placeOnMonitor();    // reposiciona (ex.: após mudança de monitores)

    const std::string& id() const { return config_.id; }
    int monitor() const { return config_.monitor; }
    const Output& config() const { return config_; }
    const std::string& detail() const { return detail_; }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    bool swapChainLost() const { return lost_; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool createSwapChain();
    void resize(int w, int h);

    Renderer& renderer_;
    Output config_;
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
