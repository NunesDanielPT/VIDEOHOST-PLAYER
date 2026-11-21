#pragma once
// Janela de output: sem bordas, ocupa um monitor inteiro, sem cursor, não fecha
// com Alt+F4 e não deixa o Windows ligar o protector de ecrã.
// Se o monitor configurado não existir, abre uma janela de pré-visualização no
// monitor principal e reporta o problema - e volta ao sítio certo quando o
// monitor for ligado.

#include "renderer.h"

#include <windows.h>

#include <string>

namespace vthost {

class OutputWindow {
public:
    OutputWindow(Renderer& renderer, std::string id, std::string name, int monitor);
    ~OutputWindow();
    OutputWindow(const OutputWindow&) = delete;
    OutputWindow& operator=(const OutputWindow&) = delete;

    bool create();
    void present(bool vsync); // desenha o canvas e apresenta
    void placeOnMonitor();    // reposiciona (ex.: após mudança de monitores)

    const std::string& id() const { return id_; }
    int monitor() const { return monitor_; }
    bool ok() const { return ok_; }
    const std::string& error() const { return error_; }
    bool swapChainLost() const { return lost_; }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool createSwapChain();
    void resize(int w, int h);

    Renderer& renderer_;
    std::string id_;
    std::string name_;
    int monitor_ = 0;
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
