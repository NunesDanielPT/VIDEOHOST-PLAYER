#pragma once
// Preferências (Ctrl+,), organizadas como no Millumin: Geral, Workspace, Board,
// Timeline, Motor e saídas, Show Mode, Atualizações e, por último, Sobre.

#include <QDialog>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr int kAboutTab = -1; // último separador

    explicit SettingsDialog(QWidget* parent = nullptr, int initialTab = 0);

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
