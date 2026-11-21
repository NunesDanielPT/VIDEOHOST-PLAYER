#include "outputs_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include "displays.h" // a mesma deteção que o motor usa
#endif

using namespace vthost;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Janela "Identificar": número grande em cada ecrã durante 3 segundos.
class IdentifyOverlay : public QWidget {
public:
    IdentifyOverlay(QScreen* screen, const QString& number, const QString& text)
        : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), number_(number), text_(text) {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setGeometry(screen->geometry());
        QTimer::singleShot(3000, this, &QWidget::close);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(20, 20, 20, 235));
        p.setPen(QPen(QColor(255, 136, 0), 16));
        p.drawRect(rect().adjusted(8, 8, -8, -8));
        QFont f = font();
        f.setPixelSize(std::max(80, height() / 3));
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(255, 136, 0));
        p.drawText(rect().adjusted(0, 0, 0, -height() / 6), Qt::AlignCenter, number_);
        f.setPixelSize(std::max(18, height() / 20));
        f.setBold(false);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(rect().adjusted(0, height() / 3, 0, 0), Qt::AlignCenter, text_);
    }

private:
    QString number_;
    QString text_;
};

// Ecrãs quando o motor não está ligado. No Windows usa exatamente o código do
// motor (mesmos ids "\\.\DISPLAY2", nomes e placa gráfica).
std::vector<protocol::DisplayInfo> qtDisplays() {
#ifdef Q_OS_WIN
    return vthost::enumerateDisplays();
#else
    std::vector<protocol::DisplayInfo> list;
    const QScreen* primary = QGuiApplication::primaryScreen();
    for (QScreen* s : QGuiApplication::screens()) {
        protocol::DisplayInfo d;
        d.id = s->name().toStdString();
        QString name = (s->manufacturer() + QLatin1Char(' ') + s->model()).trimmed();
        d.name = (name.isEmpty() ? s->name() : name).toStdString();
        const QRect g = s->geometry();
        d.x = g.x();
        d.y = g.y();
        d.width = static_cast<int>(std::lround(g.width() * s->devicePixelRatio()));
        d.height = static_cast<int>(std::lround(g.height() * s->devicePixelRatio()));
        d.refreshHz = s->refreshRate();
        d.primary = s == primary;
        list.push_back(std::move(d));
    }
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        if (a.primary != b.primary) return a.primary;
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    for (std::size_t i = 0; i < list.size(); ++i) list[i].index = static_cast<int>(i);
    return list;
#endif
}


struct Preset {
    const char* label;
    int w, h;
};
const Preset kPresets[] = {{"HD 1920 × 1080", 1920, 1080},   {"4K UHD 3840 × 2160", 3840, 2160},
                           {"HD 1280 × 720", 1280, 720},     {"Vertical 1080 × 1920", 1080, 1920},
                           {"2K DCI 2048 × 1080", 2048, 1080}, {"WUXGA 1920 × 1200", 1920, 1200}};

} // namespace

OutputsDialog::OutputsDialog(int width, int height, const std::vector<Output>& outputs, EngineClient* engine,
                             QWidget* parent)
    : QDialog(parent), engine_(engine), outputs_(outputs) {
    setWindowTitle(tr("Outputs"));
    auto* layout = new QVBoxLayout(this);

    // Resolução de trabalho.
    auto* resRow = new QHBoxLayout;
    preset_ = new QComboBox(this);
    for (const auto& p : kPresets) preset_->addItem(QString::fromUtf8(p.label), QSize(p.w, p.h));
    preset_->addItem(tr("Custom"), QSize());
    width_ = new QSpinBox(this);
    height_ = new QSpinBox(this);
    for (auto* s : {width_, height_}) {
        s->setRange(16, kMaxCanvasSize);
        s->setKeyboardTracking(false);
    }
    width_->setValue(width);
    height_->setValue(height);
    auto syncPreset = [this] {
        const QSignalBlocker b(preset_);
        int idx = preset_->count() - 1;
        for (int i = 0; i < preset_->count() - 1; ++i)
            if (preset_->itemData(i).toSize() == QSize(width_->value(), height_->value())) idx = i;
        preset_->setCurrentIndex(idx);
    };
    syncPreset();
    connect(preset_, &QComboBox::activated, this, [this](int i) {
        const QSize s = preset_->itemData(i).toSize();
        if (!s.isValid()) return;
        const QSignalBlocker b1(width_), b2(height_);
        width_->setValue(s.width());
        height_->setValue(s.height());
        changed();
    });
    for (auto* s : {width_, height_})
        connect(s, &QSpinBox::valueChanged, this, [this, syncPreset] {
            syncPreset();
            changed();
        });
    resRow->addWidget(new QLabel(tr("Resolution:"), this));
    resRow->addWidget(preset_);
    resRow->addWidget(width_);
    resRow->addWidget(new QLabel(QStringLiteral("×"), this));
    resRow->addWidget(height_);
    resRow->addStretch();
    layout->addLayout(resRow);

    // Lista de saídas + formulário.
    auto* mid = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    list_ = new QListWidget(this);
    list_->setMinimumWidth(220);
    connect(list_, &QListWidget::currentRowChanged, this, &OutputsDialog::showOutput);
    left->addWidget(list_, 1);
    auto* addRow = new QHBoxLayout;
    auto* addScreen = new QPushButton(tr("+ Screen"), this);
    auto* addWindow = new QPushButton(tr("+ Window"), this);
    auto* addNdi = new QPushButton(tr("+ NDI"), this);
    auto* remove = new QPushButton(tr("Remove"), this);
    connect(addScreen, &QPushButton::clicked, this, [this] { addOutput(OutputType::Display); });
    connect(addWindow, &QPushButton::clicked, this, [this] { addOutput(OutputType::Window); });
    connect(addNdi, &QPushButton::clicked, this, [this] { addOutput(OutputType::Ndi); });
    connect(remove, &QPushButton::clicked, this, &OutputsDialog::removeOutput);
    addRow->addWidget(addScreen);
    addRow->addWidget(addWindow);
    addRow->addWidget(addNdi);
    left->addLayout(addRow);
    left->addWidget(remove);
    mid->addLayout(left);

    form_ = new QWidget(this);
    auto* f = new QFormLayout(form_);
    name_ = new QLineEdit(form_);
    enabled_ = new QCheckBox(tr("Enabled"), form_);
    type_ = new QComboBox(form_);
    type_->addItem(tr("Screen (full screen)"), static_cast<int>(OutputType::Display));
    type_->addItem(tr("Window"), static_cast<int>(OutputType::Window));
    type_->addItem(tr("NDI (network)"), static_cast<int>(OutputType::Ndi));
    f->addRow(tr("Name:"), name_);
    f->addRow(QString(), enabled_);
    f->addRow(tr("Type:"), type_);
    pages_ = new QStackedWidget(form_);
    {
        auto* page = new QWidget(pages_);
        auto* pf = new QFormLayout(page);
        pf->setContentsMargins(0, 0, 0, 0);
        display_ = new QComboBox(page);
        display_->setMinimumWidth(320);
        auto* identifyBtn = new QPushButton(tr("Identify screens"), page);
        connect(identifyBtn, &QPushButton::clicked, this, &OutputsDialog::identify);
        pf->addRow(tr("Screen:"), display_);
        pf->addRow(QString(), identifyBtn);
        pages_->addWidget(page);
    }
    {
        auto* page = new QWidget(pages_);
        auto* pf = new QFormLayout(page);
        pf->setContentsMargins(0, 0, 0, 0);
        winW_ = new QSpinBox(page);
        winH_ = new QSpinBox(page);
        winW_->setRange(160, kMaxCanvasSize);
        winH_->setRange(90, kMaxCanvasSize);
        auto* row = new QHBoxLayout;
        row->addWidget(winW_);
        row->addWidget(new QLabel(QStringLiteral("×"), page));
        row->addWidget(winH_);
        pf->addRow(tr("Window size:"), row);
        pages_->addWidget(page);
    }
    {
        auto* page = new QWidget(pages_);
        auto* pf = new QFormLayout(page);
        pf->setContentsMargins(0, 0, 0, 0);
        ndiName_ = new QLineEdit(page);
        ndiName_->setPlaceholderText(tr("VIDEOHOST - <output name>"));
        ndiAlpha_ = new QCheckBox(tr("Send with transparency (alpha)"), page);
        pf->addRow(tr("NDI name:"), ndiName_);
        pf->addRow(QString(), ndiAlpha_);
        auto* note = new QLabel(tr("Needs the free NDI Runtime / NDI Tools installed on this PC."), page);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: gray;"));
        pf->addRow(QString(), note);
        pages_->addWidget(page);
    }
    f->addRow(pages_);
    status_ = new QLabel(form_);
    status_->setWordWrap(true);
    f->addRow(tr("State:"), status_);
    mid->addWidget(form_, 1);
    layout->addLayout(mid, 1);

    source_ = new QLabel(this);
    source_->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(source_);

    connect(name_, &QLineEdit::editingFinished, this, &OutputsDialog::changed);
    connect(enabled_, &QCheckBox::toggled, this, &OutputsDialog::changed);
    connect(type_, &QComboBox::activated, this, &OutputsDialog::changed);
    connect(display_, &QComboBox::activated, this, &OutputsDialog::changed);
    for (auto* s : {winW_, winH_}) {
        s->setKeyboardTracking(false);
        connect(s, &QSpinBox::valueChanged, this, &OutputsDialog::changed);
    }
    connect(ndiName_, &QLineEdit::editingFinished, this, &OutputsDialog::changed);
    connect(ndiAlpha_, &QCheckBox::toggled, this, &OutputsDialog::changed);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        commitForm();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    setDisplays(qtDisplays(), false);
    refreshDisplays();
    rebuildList();
    if (!outputs_.empty()) list_->setCurrentRow(0);
    else showOutput(-1);
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OutputsDialog::updateStatus);
    timer->start(500);
    resize(820, 480);
}

int OutputsDialog::canvasWidth() const { return width_->value(); }
int OutputsDialog::canvasHeight() const { return height_->value(); }

QString OutputsDialog::displayLabel(const protocol::DisplayInfo& d) const {
    QString s = QStringLiteral("%1: %2 — %3×%4").arg(d.index + 1).arg(qs(d.name)).arg(d.width).arg(d.height);
    if (d.refreshHz > 0) s += QStringLiteral(" @ %1 Hz").arg(std::lround(d.refreshHz));
    if (!d.adapter.empty()) s += QStringLiteral(" — ") + qs(d.adapter);
    if (d.primary) s += QStringLiteral("  ") + tr("(main screen)");
    return s;
}

void OutputsDialog::refreshDisplays() {
    if (!engine_ || !engine_->isConnected()) return;
    protocol::Command c;
    c.type = protocol::CommandType::ListDisplays;
    QPointer<OutputsDialog> self(this);
    engine_->send(c, [self](const protocol::ParsedResponse& r) {
        if (self && r.ok && r.displays) self->setDisplays(*r.displays, true);
    });
}

void OutputsDialog::setDisplays(std::vector<protocol::DisplayInfo> displays, bool fromEngine) {
    displays_ = std::move(displays);
    source_->setText(fromEngine ? tr("Screens detected by the engine.") : tr("Screens detected by Windows."));
    if (current_ >= 0) showOutput(current_);
}

void OutputsDialog::rebuildList() {
    const QSignalBlocker b(list_);
    const int row = list_->currentRow();
    list_->clear();
    for (const auto& o : outputs_) {
        QString t = qs(o.name.empty() ? o.id : o.name);
        if (o.type == OutputType::Ndi) t += QStringLiteral("  [NDI]");
        else if (o.type == OutputType::Window) t += QStringLiteral("  [") + tr("window") + QStringLiteral("]");
        if (!o.enabled) t += QStringLiteral("  (") + tr("off") + QStringLiteral(")");
        list_->addItem(t);
    }
    if (row >= 0 && row < list_->count()) list_->setCurrentRow(row);
}

void OutputsDialog::showOutput(int index) {
    current_ = index;
    form_->setEnabled(index >= 0 && index < static_cast<int>(outputs_.size()));
    if (!form_->isEnabled()) {
        status_->clear();
        return;
    }
    loading_ = true;
    const Output& o = outputs_[static_cast<std::size_t>(index)];
    name_->setText(qs(o.name));
    enabled_->setChecked(o.enabled);
    type_->setCurrentIndex(std::max(0, type_->findData(static_cast<int>(o.type))));
    pages_->setCurrentIndex(o.type == OutputType::Display ? 0 : o.type == OutputType::Window ? 1 : 2);
    display_->clear();
    int sel = -1;
    for (const auto& d : displays_) {
        display_->addItem(displayLabel(d), qs(d.id));
        if ((!o.displayId.empty() && (d.id == o.displayId || d.name == o.displayId)) ||
            (o.displayId.empty() && d.index == o.monitor))
            sel = display_->count() - 1;
    }
    if (sel < 0 && !o.displayId.empty()) {
        display_->addItem(tr("%1 (not connected)").arg(qs(o.displayId)), qs(o.displayId));
        sel = display_->count() - 1;
    }
    display_->setCurrentIndex(std::max(0, sel));
    winW_->setValue(o.windowWidth);
    winH_->setValue(o.windowHeight);
    ndiName_->setText(qs(o.ndiName));
    ndiAlpha_->setChecked(o.ndiAlpha);
    loading_ = false;
    updateStatus();
}

void OutputsDialog::commitForm() {
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size())) return;
    Output& o = outputs_[static_cast<std::size_t>(current_)];
    o.name = name_->text().trimmed().toStdString();
    o.enabled = enabled_->isChecked();
    o.type = static_cast<OutputType>(type_->currentData().toInt());
    if (o.type == OutputType::Display && display_->currentIndex() >= 0) {
        o.displayId = display_->currentData().toString().toStdString();
        for (const auto& d : displays_)
            if (d.id == o.displayId) o.monitor = d.index;
    }
    o.windowWidth = winW_->value();
    o.windowHeight = winH_->value();
    o.ndiName = ndiName_->text().trimmed().toStdString();
    o.ndiAlpha = ndiAlpha_->isChecked();
}

void OutputsDialog::changed() {
    if (loading_) return;
    commitForm();
    rebuildList();
    if (current_ >= 0) {
        const int t = static_cast<int>(outputs_[static_cast<std::size_t>(current_)].type);
        pages_->setCurrentIndex(t == static_cast<int>(OutputType::Display) ? 0 : t == static_cast<int>(OutputType::Window) ? 1 : 2);
    }
    emit liveChanged();
}

void OutputsDialog::addOutput(OutputType type) {
    commitForm();
    Output o;
    for (int i = 1;; ++i) {
        const std::string id = "O" + std::to_string(i);
        if (std::none_of(outputs_.begin(), outputs_.end(), [&](const Output& x) { return x.id == id; })) {
            o.id = id;
            o.name = tr("Output %1").arg(i).toStdString();
            break;
        }
    }
    o.type = type;
    if (type == OutputType::Display) {
        // Por omissão o primeiro ecrã que não é o principal (o projetor/LED).
        for (const auto& d : displays_)
            if (!d.primary) {
                o.displayId = d.id;
                o.monitor = d.index;
                break;
            }
    }
    outputs_.push_back(o);
    rebuildList();
    list_->setCurrentRow(static_cast<int>(outputs_.size()) - 1);
    emit liveChanged();
}

void OutputsDialog::removeOutput() {
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size())) return;
    outputs_.erase(outputs_.begin() + current_);
    current_ = -1;
    rebuildList();
    if (!outputs_.empty()) list_->setCurrentRow(0);
    else showOutput(-1);
    emit liveChanged();
}

void OutputsDialog::identify() {
    for (const auto& d : displays_) {
        for (QScreen* s : QGuiApplication::screens()) {
            // O Qt pode dar o nome do monitor em vez do id; compara também a posição.
            const QRect g = s->geometry();
            const bool samePos = std::abs(g.x() * s->devicePixelRatio() - d.x) < 2 &&
                                 std::abs(g.y() * s->devicePixelRatio() - d.y) < 2;
            if (s->name().toStdString() != d.id && s->name().toStdString() != d.name && !samePos) continue;
            auto* w = new IdentifyOverlay(s, QString::number(d.index + 1),
                                          QStringLiteral("%1\n%2×%3").arg(qs(d.name)).arg(d.width).arg(d.height) +
                                              (d.adapter.empty() ? QString() : QStringLiteral("\n") + qs(d.adapter)));
            w->show();
            break;
        }
    }
}

void OutputsDialog::updateStatus() {
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size())) return;
    const Output& o = outputs_[static_cast<std::size_t>(current_)];
    if (!engine_ || !engine_->hasStatus() || !engine_->isConnected()) {
        status_->setText(tr("Engine not running."));
        return;
    }
    for (const auto& os : engine_->lastStatus().outputs)
        if (os.id == o.id) {
            QString t = qs(os.detail);
            if (!os.error.empty()) t += (t.isEmpty() ? QString() : QStringLiteral("\n")) + QStringLiteral("⚠ ") + qs(os.error);
            status_->setText(t.isEmpty() ? (os.ok ? tr("OK") : tr("Not running")) : t);
            status_->setStyleSheet(os.ok && os.error.empty() ? QString() : QStringLiteral("color: #e66;"));
            return;
        }
    status_->setText(tr("Waiting for the engine..."));
}
