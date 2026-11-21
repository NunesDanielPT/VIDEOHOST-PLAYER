#include "library_panel.h"

#include "media_cache.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr QSize kIconSize(64, 36);

// Lista que arrasta o id da media em vez do texto.
class MediaList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QStringList mimeTypes() const override { return {QString::fromLatin1(kMediaMime)}; }
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        if (items.isEmpty()) return nullptr;
        auto* data = new QMimeData;
        data->setData(QString::fromLatin1(kMediaMime), items.first()->data(Qt::UserRole).toString().toUtf8());
        return data;
    }
};

QIcon placeholderIcon(bool missing) {
    QPixmap pm(kIconSize);
    pm.fill(missing ? QColor(90, 30, 30) : QColor(45, 45, 45));
    QPainter p(&pm);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return QIcon(pm);
}

} // namespace

LibraryPanel::LibraryPanel(MediaCache* cache, QWidget* parent) : QWidget(parent), cache_(cache) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* title = new QLabel(tr("Library"), this);
    title->setStyleSheet(QStringLiteral("font-weight: bold;"));
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, &LibraryPanel::applyFilter);

    list_ = new MediaList(this);
    list_->setIconSize(kIconSize);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setDragEnabled(true);
    list_->setDragDropMode(QAbstractItemView::DragOnly);
    list_->setUniformItemSizes(true);

    add_ = new QPushButton(QStringLiteral("+"), this);
    add_->setToolTip(tr("Add media..."));
    add_->setFixedWidth(32);
    remove_ = new QPushButton(QStringLiteral("−"), this);
    remove_->setToolTip(tr("Remove the selected media"));
    remove_->setFixedWidth(32);
    connect(add_, &QPushButton::clicked, this, &LibraryPanel::addRequested);
    connect(remove_, &QPushButton::clicked, this, [this] {
        const QString id = selectedMediaId();
        if (!id.isEmpty()) emit removeRequested(id);
    });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add_);
    buttons->addWidget(remove_);
    buttons->addStretch();

    layout->addWidget(title);
    layout->addWidget(search_);
    layout->addWidget(list_, 1);
    layout->addLayout(buttons);

    connect(cache_, &MediaCache::updated, this, [this](const QString&) { refreshThumbnails(); });
}

void LibraryPanel::setMedia(const std::vector<vthost::MediaItem>& media) {
    const QString selected = selectedMediaId();
    list_->clear();
    for (const auto& m : media) {
        const QString path = QString::fromStdString(m.path);
        auto* item = new QListWidgetItem(QFileInfo(path).fileName(), list_);
        item->setData(Qt::UserRole, QString::fromStdString(m.id));
        item->setData(Qt::UserRole + 1, path);
        if (QString::fromStdString(m.id) == selected) list_->setCurrentItem(item);
    }
    refreshThumbnails();
    applyFilter();
}

void LibraryPanel::refreshThumbnails() {
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        const QString path = item->data(Qt::UserRole + 1).toString();
        const MediaInfo info = cache_->info(path);
        const bool missing = info.ready && !QFileInfo::exists(path);
        if (info.ok && !info.thumb.isNull()) {
            QPixmap pm(kIconSize);
            pm.fill(Qt::black);
            QPainter p(&pm);
            const QImage scaled = info.thumb.scaled(kIconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage((kIconSize.width() - scaled.width()) / 2, (kIconSize.height() - scaled.height()) / 2, scaled);
            item->setIcon(QIcon(pm));
        } else {
            item->setIcon(placeholderIcon(missing));
        }
        QString tip = QDir::toNativeSeparators(path);
        if (missing) tip = tr("File not found: %1").arg(tip);
        else if (info.ok)
            tip += QStringLiteral("\n%1×%2").arg(info.width).arg(info.height) +
                   (info.still ? QString() : QStringLiteral(" · %1 s").arg(info.duration, 0, 'f', 1));
        item->setToolTip(tip);
        if (missing) item->setForeground(QColor(230, 90, 90));
        else item->setData(Qt::ForegroundRole, QVariant());
    }
}

void LibraryPanel::applyFilter() {
    const QString f = search_->text().trimmed();
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        item->setHidden(!f.isEmpty() && !item->text().contains(f, Qt::CaseInsensitive));
    }
}

QString LibraryPanel::selectedMediaId() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void LibraryPanel::setEditable(bool editable) {
    add_->setEnabled(editable);
    remove_->setEnabled(editable);
}
