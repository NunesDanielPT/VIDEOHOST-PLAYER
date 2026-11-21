#pragma once
// Janela "Nova versão disponível" (como no MapSim): versão instalada, lista do
// que muda em cada versão nova, barra de progresso do download e os botões
// "Atualizar agora" / "Mais tarde". Depois de descarregar: "Reiniciar e instalar".

#include "update_checker.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextBrowser;

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    UpdateDialog(const UpdateInfo& info, QWidget* parent = nullptr);

    void setProgress(qint64 received, qint64 total);
    void setFailed(const QString& error);
    void setReady(); // descarregado e verificado

    // Novidades em HTML (também usado no rodapé / Ajuda).
    static QString notesHtml(const QList<ReleaseNotes>& changes);

signals:
    void updateNowRequested();  // começar a descarregar
    void installNowRequested(); // instalador pronto: fechar e instalar

private:
    QLabel* status_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPushButton* primary_ = nullptr;
    QPushButton* later_ = nullptr;
    bool ready_ = false;
};
