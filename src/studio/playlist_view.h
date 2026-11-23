#pragma once
// Playlist (como no Mitti): uma linha por cue com miniatura, número/ID, cor,
// nome, Start/TRT e a fila de ícones de comportamento (som, pausa no início,
// fade in, loop, crossfade, fade out, pausa no fim, apagar no fim, ir para).
// Aceita ficheiros arrastados do Explorador e reordenar arrastando.

#include "vthost/core/project.h"

#include <QListWidget>
#include <QStyledItemDelegate>

class MediaCache;

enum class CueIcon { Audio, PauseStart, FadeIn, Loop, Crossfade, FadeOut, PauseEnd, Clear, Goto, Count };

class PlaylistView : public QListWidget {
    Q_OBJECT
public:
    PlaylistView(MediaCache* media, QWidget* parent = nullptr);

    // Reconstrói as linhas (mantém a seleção pelo id da cue).
    void setProject(const vthost::Project* project);
    void refresh(); // só repinta (miniaturas, tempos)

    // Cue no ar: índice, progresso 0..1 e estado ("playing", "paused", ...).
    void setPlaying(int index, double progress, const QString& state);
    // Disparo pelo misturador (vMix / ATEM): botão CUE nas cues e a cue armada.
    void setTallyEnabled(bool on);
    void setArmed(int index);
    int armed() const { return armed_; }
    int playingIndex() const { return playing_; }
    double playingProgress() const { return progress_; }
    QString playingState() const { return playingState_; }

    int selectedIndex() const;
    void selectIndex(int index);
    void setEditable(bool editable) { editable_ = editable; }
    bool editable() const { return editable_; }

    const vthost::Project* project() const { return project_; }
    MediaCache* media() const { return media_; }
    // Início de cada cue na playlist (soma das durações anteriores), em segundos.
    double startOf(int index) const;

signals:
    void playRequested(int index);
    void iconClicked(int index, CueIcon icon, const QPoint& globalPos);
    void filesDropped(int row, const QStringList& paths);
    void cueMoved(int from, int to);
    void deleteRequested(int index);
    void contextMenuRequested(int index, const QPoint& globalPos);
    void thumbnailRequested(int index);
    void armClicked(int index); // botão CUE
    void spacePressed(); // ESPAÇO com a playlist em foco = play/pausa (não mexe na seleção) // botão da câmara: miniatura = ponto actual da cue no ar

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool event(QEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    friend class CueDelegate;
    MediaCache* media_;
    const vthost::Project* project_ = nullptr;
    int playing_ = -1;
    int armed_ = -1;
    bool tallyEnabled_ = false;
    double progress_ = 0.0;
    QString playingState_;
    bool editable_ = true;
};

class CueDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit CueDelegate(PlaylistView* view);
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    bool editorEvent(QEvent* e, QAbstractItemModel* model, const QStyleOptionViewItem& opt, const QModelIndex& idx) override;

    static QRect iconRect(const QRect& row, int i);
    static QRect thumbButtonRect(const QRect& row);
    static QRect cueButtonRect(const QRect& row);

private:
    PlaylistView* view_;
};

// Desenho dos ícones (também usado no Inspector e na barra de transporte).
void paintCueIcon(QPainter* p, const QRectF& r, CueIcon icon, bool on);
// "00:00:27:04" (horas:minutos:segundos:frames).
QString formatTimecode(double seconds, double fps);
