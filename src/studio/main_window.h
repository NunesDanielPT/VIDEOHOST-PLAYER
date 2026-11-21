#pragma once
// Janela principal: player de playlist (estilo Mitti).
//
//   ┌──────────────── barra de transporte (play, anterior/seguinte, GOTO, tempo) ───────┐
//   │  Playlist (cues)   │  Preview (decorrido / restante)  │  Inspector da cue       │
//   │                    ├──────────────────────────────────┤                          │
//   │                    │  Forma de onda (In/Out, saltar)  │                          │
//   └────────────────────┴──────────────────────────────────┴──────────────────────────┘

#include "engine_client.h"
#include "media_cache.h"
#include "playlist_view.h"
#include "update_checker.h"
#include "vthost/core/countdown.h"
#include "vthost/core/project.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>

#include <QPointer>
#include <QSet>

#include <deque>
#include <optional>
#include <string>

class CountdownServer;
class CuePlayerPage;
class UpdateDialog;
class CountdownView;
class CountdownWindow;
class InspectorPanel;
class PreviewWidget;
class QAction;
class QLabel;
class QSlider;
class QSplitter;
class QStackedWidget;
class QToolButton;
class WaveformView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    QWidget* buildTransportBar();
    void buildMenus();
    void setWorkspace(int index);
    void onWorkspaceChanged(int index); // fonte do preview principal, atalhos...

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

    // Playlist
    void addMedia();
    void addFiles(int row, const QStringList& paths);
    void deleteCue(int index);
    void duplicateCue(int index);
    void moveCue(int from, int to);
    void replaceFile(int index);
    void onIconClicked(int index, CueIcon icon, const QPoint& globalPos);
    void showCueMenu(int index, const QPoint& globalPos);
    void onCueEdited(const vthost::Cue& cue);
    void applyAnalysis(const QString& path); // grava loudness/pico medidos nas cues desse ficheiro
    void refreshPlaylist();
    void updateSelection();      // inspector + forma de onda da cue selecionada

    // Reprodução
    void playCue(int index);     // com SHIFT = corte
    void playPause();            // ESPAÇO
    void stopPlayback();
    void nextCue();
    void previousCue();
    void gotoRemaining(double seconds);
    void seekTo(double fileTime);
    void toggleLoopPlaylist();
    void setMasterVolume(double db);
    double playingMediaTime() const; // posição no ficheiro da cue no ar (-1 = nada)
    void thumbnailFromPlayhead();    // Ctrl+P / botão da câmara

    // Desfazer / refazer (Ctrl+Z / Ctrl+Shift+Z): instantâneos do projecto.
    void recordUndo();
    void clearUndo();
    void undo();
    void redo();
    void restoreSnapshot(const std::string& json);
    void updateUndoActions();

    // Countdown (preview, janela própria e página web na rede local)
    vthost::Countdown currentCountdown() const;
    void refreshCountdown();
    void openCountdownWindow();
    void showCountdownWebInfo();
    void applyCountdownServer();

    // Show
    void applyPreferences();
    void sendEngineSettings();
    void setShowMode(bool on);
    void openOutputs();

    // Motor
    void startEngine();
    void stopEngine();
    void onEngineConnection(bool connected);
    void onEngineStatus();

    // Definições e atualizações
    void openSettings(int tab = 0);
    void checkForUpdates(bool userInitiated);
    void onUpdateAvailable(const UpdateInfo& info);
    void showUpdateDialog();
    void installUpdate(const QString& installerPath, bool ask = true);
    void setUpdateLabel(const QString& html);

    void updateTitle();
    void updateEditingEnabled();
    int selectedCue() const;

    EngineClient* engine_ = nullptr;
    UpdateChecker* updater_ = nullptr;
    MediaCache* media_ = nullptr;
    bool updateCheckIsManual_ = false;
    std::optional<UpdateInfo> availableUpdate_; // última versão nova encontrada
    QString readyInstaller_;                    // instalador já descarregado e verificado
    bool downloading_ = false;
    QSet<QString> promptedVersions_;            // janela automática: uma vez por versão
    QPointer<UpdateDialog> updateDialog_;
    QLabel* updateLabel_ = nullptr;             // rodapé: "Versão X disponível — ver novidades"
    QTimer updateTimer_;                        // procura de 30 em 30 minutos

    vthost::Project project_;
    QString projectPath_;
    bool dirty_ = false;
    bool showMode_ = false;
    bool adoptedEngineProject_ = false;
    int playingCue_ = -1;
    QTimer pushTimer_;
    QTimer uiTimer_; // tempos no ecrã a ~25 fps (interpolados entre estados do motor)

    std::deque<std::string> undoStack_;
    std::deque<std::string> redoStack_;
    std::string undoBase_;
    QElapsedTimer lastUndoPush_;
    bool restoring_ = false;

    vthost::Countdown countdown_;
    QElapsedTimer countdownClock_;
    QTimer countdownTimer_;
    double statusMediaTime_ = 0.0; // posição no ficheiro no último estado

    QStackedWidget* workspaces_ = nullptr; // 0 = MEDIAPLAYER, 1 = CUEPLAYER
    CuePlayerPage* cuePage_ = nullptr;
    QToolButton* workspaceButtons_[2] = {nullptr, nullptr};
    QSplitter* mainSplit_ = nullptr;
    QSplitter* centerSplit_ = nullptr;
    PlaylistView* playlist_ = nullptr;
    PreviewWidget* preview_ = nullptr;
    WaveformView* waveform_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    CountdownWindow* countdownWindow_ = nullptr;
    CountdownServer* countdownServer_ = nullptr;

    QToolButton* playBtn_ = nullptr;
    QToolButton* loopBtn_ = nullptr;
    QLabel* timecode_ = nullptr;
    QLabel* cueLabel_ = nullptr;
    QSlider* master_ = nullptr;
    QLabel* masterLabel_ = nullptr;
    QWidget* meter_ = nullptr;
    QToolButton* testBtn_ = nullptr;
    QLabel* engineLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QLabel* webLabel_ = nullptr;
    QAction* showModeAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* loopAction_ = nullptr;
    QList<QWidget*> editWidgets_;
    QList<QAction*> editActions_;
};
