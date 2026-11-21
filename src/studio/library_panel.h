#pragma once
// Library: media do projecto com miniaturas e pesquisa. Arrasta-se para o
// Dashboard ou para a Timeline.

#include "vthost/core/project.h"

#include <QWidget>

class MediaCache;
class QLineEdit;
class QListWidget;
class QPushButton;

inline constexpr const char* kMediaMime = "application/x-vthost-media";

class LibraryPanel : public QWidget {
    Q_OBJECT
public:
    LibraryPanel(MediaCache* cache, QWidget* parent = nullptr);

    void setMedia(const std::vector<vthost::MediaItem>& media);
    QString selectedMediaId() const;
    void setEditable(bool editable);

signals:
    void addRequested();
    void removeRequested(const QString& mediaId);

private:
    void applyFilter();
    void refreshThumbnails();

    MediaCache* cache_;
    QLineEdit* search_;
    QListWidget* list_;
    QPushButton* add_;
    QPushButton* remove_;
};
