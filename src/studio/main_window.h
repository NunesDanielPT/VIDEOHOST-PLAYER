#pragma once
// Janela principal, organizada como um media server de espetáculo:
//
//   ┌───────────┬──────────────────────────┬─────────────┐
//   │  Library  │  Workspace (preview AR)  │ Properties  │
//   ├───────────┴──────────────────────────┴─────────────┤
//   │        Dashboard  |  Timeline   (separadores)       │
//   └─────────────────────────────────────────────────────┘

#include "engine_client.h"
#include "update_checker.h"
#include "vthost/core/project.h"

#include <QMainWindow>
#include <QTimer>

#include <optional>

class DashboardTable;
class LibraryPanel;
class MediaCache;
class PreviewWidget;
class PropertiesPanel;
class QAction;
class QLabel;
class QSplitter;
class QTabWidget;
class TimelinePanel;

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
    void ensureTimeline();
    void markChanged();
    void pushToEngine();
    QString livePath() const;

    // Edição
    void addMedia();
    void removeMedia(const QString& mediaId);
    void addLayer();
    void removeLayer();
    void renameLayer();
    void addColumn();
    void removeColumn();
    void renameColumn();
    void setColumnFade();
    void placeMedia(int row, int column, const QString& mediaId);
    void clearCell(int row, int column);
    void addOutput();
    void removeOutput();
    void setCanvasSize();
    void showCellMenu(const QPoint& pos);

    // Seleção e propriedades
    void selectLayer(int row);
    void updateSelectionViews();
    void onPropsChanged(const vthost::LayerProps& props);
    void onLayerDragged(double dx, double dy, bool finished);
    void applyLayerProps(int row, const vthost::LayerProps& props, bool persist);

    // Show
    void go(int column);
    void goNext();
    void stopAll();
    void setShowMode(bool on);

    // Timeline
    QString timelineId() const;
    void sendTimeline(vthost::protocol::TimelineAction action, double time);

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
    void refreshDashboard();
    void updateTitle();
    void updateEditingEnabled();
    QString nextId(const QString& prefix) const;

    EngineClient* engine_ = nullptr;
    UpdateChecker* updater_ = nullptr;
    MediaCache* media_ = nullptr;
    bool updateCheckIsManual_ = false;

    vthost::Project project_;
    QString projectPath_;
    bool dirty_ = false;
    bool showMode_ = false;
    bool adoptedEngineProject_ = false;
    int activeColumn_ = -1;
    int selectedLayer_ = -1;
    std::optional<vthost::LayerProps> dragStartProps_;
    QTimer pushTimer_;

    QSplitter* mainSplit_ = nullptr;
    QSplitter* topSplit_ = nullptr;
    LibraryPanel* library_ = nullptr;
    PreviewWidget* preview_ = nullptr;
    PropertiesPanel* properties_ = nullptr;
    QTabWidget* bottomTabs_ = nullptr;
    DashboardTable* dashboard_ = nullptr;
    TimelinePanel* timelinePanel_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QAction* showModeAction_ = nullptr;
    QList<QWidget*> editWidgets_;
    QList<QAction*> editActions_;
};
