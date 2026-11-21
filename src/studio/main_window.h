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

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>

#include <deque>
#include <optional>

class DashboardTable;
class LayerHeader;
class QMenu;
class LibraryPanel;
class MediaCache;
class PreviewWidget;
class PropertiesPanel;
class QAction;
class QLabel;
class QSplitter;
class QTabWidget;
class TimelinePanel;
class TransportBar;

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
    void cueSettings(int column);
    void placeMedia(int row, int column, const QString& mediaId);
    void clearCell(int row, int column);
    void openOutputs();
    void setCanvasSize();
    void showCellMenu(const QPoint& pos);
    void showColumnMenu(int column, const QPoint& globalPos);
    void showLayerMenu(int row, const QPoint& globalPos);
    void showMediaMenu(const QString& mediaId, const QPoint& globalPos);
    void addLoopModeMenu(QMenu* menu, const QString& mediaId);

    // Layers e colunas
    void insertLayerAt(int index);
    void duplicateLayer(int row);
    void moveLayer(int from, int to);
    void toggleLayerVisible(int row);
    void toggleLayerLock(int row);
    void toggleSolo(int row);
    void sendVisibility();          // visibilidade efetiva (olho + solo) para o motor
    bool effectiveVisible(const vthost::Layer& l) const;
    void refreshCanvasChoices();
    void setWorkspaceCanvas(const QString& canvasId);
    void insertColumnAt(int index);
    void duplicateColumn(int column);
    void moveColumn(int from, int to);
    void deleteColumn(int column);

    // Seleção e propriedades
    void selectLayer(int row);
    void updateSelectionViews();
    void onPropsChanged(const vthost::LayerProps& props);
    void onLayerDragged(double dx, double dy, bool finished);
    void onCornerDragged(int corner, double dx, double dy, bool finished);
    void applyLayerProps(int row, const vthost::LayerProps& props, bool persist);

    // Desfazer / refazer (Ctrl+Z / Ctrl+Shift+Z): instantâneos do projecto.
    void recordUndo();
    void clearUndo();
    void undo();
    void redo();
    void restoreSnapshot(const std::string& json);
    void updateUndoActions();

    // Barra de tempo do Workspace
    void updateTransport();

    // Show
    void go(int column);
    void goNext();
    void goPrevious();
    void onSpace();            // ESPAÇO: Dashboard = coluna seguinte; Timeline = play/stop
    void applyPreferences();
    void sendEngineSettings();
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
    std::string soloLayer_;      // layer em solo ("" = nenhuma)
    QString workspaceCanvas_;    // canvas mostrado no Workspace
    std::optional<vthost::LayerProps> dragStartProps_;
    QTimer pushTimer_;
    std::deque<std::string> undoStack_;
    std::deque<std::string> redoStack_;
    std::string undoBase_;        // estado actual já registado
    QElapsedTimer lastUndoPush_;  // agrupa alterações seguidas (ex. arrastar um valor)
    bool restoring_ = false;
    std::string transportLayer_;  // layer controlada pela barra de tempo

    QSplitter* mainSplit_ = nullptr;
    QSplitter* topSplit_ = nullptr;
    LibraryPanel* library_ = nullptr;
    PreviewWidget* preview_ = nullptr;
    class QComboBox* canvasSelect_ = nullptr;
    PropertiesPanel* properties_ = nullptr;
    QTabWidget* bottomTabs_ = nullptr;
    DashboardTable* dashboard_ = nullptr;
    LayerHeader* layerHeader_ = nullptr;
    TimelinePanel* timelinePanel_ = nullptr;
    TransportBar* transport_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QAction* showModeAction_ = nullptr;
    QList<QWidget*> editWidgets_;
    QList<QAction*> editActions_;
};
