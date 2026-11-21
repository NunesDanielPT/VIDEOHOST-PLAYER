#pragma once
// Preferências (Ctrl+,): Geral, Playlist, Áudio, Motor e saídas, Show Mode,
// Countdown, Rede, Atalhos, Atualizações e, por último, Sobre.

#include <QDialog>

class EngineClient;

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    // Ordem dos separadores: Geral, Playlist, Áudio, Motor e saídas, Show Mode,
    // Countdown, Rede, Atalhos, Atualizações, Sobre.
    static constexpr int kAudioTab = 2;
    static constexpr int kCountdownTab = 5;
    static constexpr int kNetworkTab = 6;
    static constexpr int kShortcutsTab = 7;
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
