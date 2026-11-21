#pragma once

#include "engine_client.h"
#include "update_checker.h"
#include "vthost/core/project.h"

#include <QMainWindow>
#include <QTimer>

class QAction;
class QLabel;
class QListWidget;
class QSlider;
class QTableWidget;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void buildMenus();

    // Projecto
    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool confirmDiscard();
    void loadFrom(const QString& path);
    void setProject(vthost::Project p, const QString& path);
    void markChanged();
    void pushToEngine();
    QString livePath() const;

    // Edição
    void addMedia();
    void removeMedia();
    void addLayer();
    void removeLayer();
    void renameLayer();
    void addColumn();
    void removeColumn();
    void renameColumn();
    void setColumnFade();
    void assignSelectedMedia(int row, int column);
    void clearCell(int row, int column);
    void addOutput();
    void removeOutput();
    void setCanvasSize();
    void showCellMenu(const QPoint& pos);

    // Show
    void go(int column);
    void goNext();
    void stopAll();
    void setShowMode(bool on);
    void onLayerSelected();
    void onOpacityChanged(int value);

    // Motor
    void startEngine();
    void stopEngine();
    void onEngineConnection(bool connected);
    void onEngineStatus();

    // Definições e atualizações
    void openSettings(int tab = 0);
    void checkForUpdates(bool userInitiated);
    void onUpdateAvailable(const UpdateInfo& info);
    void downloadUpdate(const UpdateInfo& info);
    void installUpdate(const QString& installerPath);
    void refreshAll();
    void refreshMediaList();
    void refreshGrid();
    void updateTitle();
    void updateEditingEnabled();
    QString nextId(const QString& prefix) const;

    EngineClient* engine_ = nullptr;
    UpdateChecker* updater_ = nullptr;
    bool updateCheckIsManual_ = false;
    vthost::Project project_;
    QString projectPath_;
    bool dirty_ = false;
    bool showMode_ = false;
    bool adoptedEngineProject_ = false;
    int activeColumn_ = -1;
    QTimer pushTimer_;

    QListWidget* mediaList_ = nullptr;
    QTableWidget* grid_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QLabel* opacityLabel_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QPushButton* goButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QAction* showModeAction_ = nullptr;
    QList<QWidget*> editWidgets_;
    QList<QAction*> editActions_;
};
