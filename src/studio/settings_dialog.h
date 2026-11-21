#pragma once
// Preferências (Ctrl+,): Geral, Playlist, Áudio, Motor e saídas, Show Mode,
// Countdown, Atalhos, Atualizações e, por último, Sobre.

#include <QDialog>

class EngineClient;

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr int kAboutTab = -1; // último separador

    explicit SettingsDialog(QWidget* parent = nullptr, int initialTab = 0, EngineClient* engine = nullptr);

    static bool checkUpdatesOnStartup();

signals:
    void checkForUpdatesRequested();
    void preferencesChanged();

private:
    void apply();

    QComboBox* language_ = nullptr;
    QString initialLanguage_;
    std::vector<std::function<void()>> savers_; // cada controlo sabe gravar-se
};
