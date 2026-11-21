#pragma once
// Countdown grande (tempo que falta do que está no ar). Usado na barra do
// Workspace (compacto) e na janela de countdown (ecrã inteiro para o palco).

#include "vthost/core/countdown.h"

#include <QWidget>

class CountdownView : public QWidget {
    Q_OBJECT
public:
    explicit CountdownView(bool compact, QWidget* parent = nullptr);
    void setCountdown(const vthost::Countdown& c);
    QSize sizeHint() const override;

signals:
    void doubleClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override { emit doubleClicked(); }

private:
    bool compact_;
    vthost::Countdown c_;
};

// Janela própria (pode ir para um monitor de palco em ecrã inteiro: duplo clique / F11).
class CountdownWindow : public QWidget {
    Q_OBJECT
public:
    explicit CountdownWindow(QWidget* parent = nullptr);
    void setCountdown(const vthost::Countdown& c) { view_->setCountdown(c); }

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    void toggleFullScreen();
    CountdownView* view_;
};
