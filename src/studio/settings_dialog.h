#pragma once
// Definições: Geral (idioma, atualizações) e Sobre (versão, autor, créditos).

#include <QDialog>

class QCheckBox;
class QComboBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr, int initialTab = 0);

    static bool checkUpdatesOnStartup();

signals:
    void checkForUpdatesRequested();

private:
    void apply();

    QComboBox* language_ = nullptr;
    QCheckBox* autoUpdate_ = nullptr;
    QString initialLanguage_;
};
