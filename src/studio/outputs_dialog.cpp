#include "outputs_dialog.h"

#include "mapping_editor.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

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

} // namespace

namespace {
QSpinBox* ispin(QWidget* parent, int min, int max, const QString& suffix = QStringLiteral(" px")) {
    auto* s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    s->setAccelerated(true);
    return s;
}
QDoubleSpinBox* dspin(QWidget* parent, double min, double max, double step, int dec, const QString& suffix = {}) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setDecimals(dec);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

// Resoluções favoritas (como no Millumin).
const std::pair<int, int> kPresets[] = {{1280, 720},  {1920, 1080}, {1920, 1200}, {2560, 1440}, {3840, 2160},
                                        {4096, 2160}, {1080, 1920}, {3840, 1080}, {5760, 1080}, {7680, 1080},
                                        {3840, 1200}, {5760, 1200}};
} // namespace

OutputsDialog::OutputsDialog(const std::vector<Canvas>& canvases, const std::vector<Output>& outputs,
                             EngineClient* engine, QWidget* parent)
    : QDialog(parent), engine_(engine), canvases_(canvases), outputs_(outputs) {
    setWindowTitle(tr("Outputs and canvases"));
    resize(1020, 620);
    if (canvases_.empty()) canvases_.push_back({"C1", "Canvas 1", 1920, 1080});
    for (auto& o : outputs_) o.mapping.sanitize();

    // ---------------- coluna da esquerda: canvas + saídas
    canvasList_ = new QListWidget(this);
    canvasList_->setMaximumHeight(150);
    auto* addCanvasBtn = new QPushButton(tr("+ Canvas"), this);
    auto* removeCanvasBtn = new QPushButton(QStringLiteral("−"), this);
    removeCanvasBtn->setFixedWidth(32);
    auto* layoutBtn = new QPushButton(tr("Layout..."), this);
    layoutBtn->setToolTip(tr("Split this canvas across several outputs (with soft-edge overlap)"));
    connect(addCanvasBtn, &QPushButton::clicked, this, &OutputsDialog::addCanvas);
    connect(removeCanvasBtn, &QPushButton::clicked, this, &OutputsDialog::removeCanvas);
    connect(layoutBtn, &QPushButton::clicked, this, &OutputsDialog::layoutWizard);
    auto* canvasButtons = new QHBoxLayout;
    canvasButtons->addWidget(addCanvasBtn);
    canvasButtons->addWidget(removeCanvasBtn);
    canvasButtons->addWidget(layoutBtn);

    list_ = new QListWidget(this);
    auto* add = new QToolButton(this);
    add->setText(tr("+ Add output"));
    add->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(add);
    addMenu->addAction(tr("Display (fullscreen)"), this, [this] { addOutput(OutputType::Display); });
    addMenu->addAction(tr("Window"), this, [this] { addOutput(OutputType::Window); });
    addMenu->addAction(tr("NDI"), this, [this] { addOutput(OutputType::Ndi); });
    addMenu->addAction(tr("Blackmagic (SDI / HDMI)"), this, [this] { addOutput(OutputType::DeckLink); });
    add->setMenu(addMenu);
    auto* remove = new QPushButton(tr("Remove"), this);
    connect(remove, &QPushButton::clicked, this, &OutputsDialog::removeOutput);
    auto* outButtons = new QHBoxLayout;
    outButtons->addWidget(add);
    outButtons->addWidget(remove);

    auto* left = new QVBoxLayout;
    auto* cl = new QLabel(tr("Canvases"), this);
    cl->setStyleSheet(QStringLiteral("font-weight: bold;"));
    auto* ol = new QLabel(tr("Outputs"), this);
    ol->setStyleSheet(QStringLiteral("font-weight: bold;"));
    left->addWidget(cl);
    left->addWidget(canvasList_);
    left->addLayout(canvasButtons);
    left->addSpacing(8);
    left->addWidget(ol);
    left->addWidget(list_, 1);
    left->addLayout(outButtons);
    testCard_ = new QCheckBox(tr("Display test card on all outputs"), this);
    testCard_->setChecked(engine_ && engine_->hasStatus() && engine_->lastStatus().testCard);
    connect(testCard_, &QCheckBox::toggled, this, &OutputsDialog::setTestCard);
    left->addWidget(testCard_);

    // ---------------- formulário do canvas
    auto* canvasPage = new QWidget(this);
    auto* cf = new QFormLayout(canvasPage);
    cName_ = new QLineEdit(canvasPage);
    cPreset_ = new QComboBox(canvasPage);
    cPreset_->addItem(tr("Custom"), QSize());
    for (const auto& [w, h] : kPresets) cPreset_->addItem(QStringLiteral("%1 × %2").arg(w).arg(h), QSize(w, h));
    cW_ = ispin(canvasPage, 16, kMaxCanvasSize);
    cH_ = ispin(canvasPage, 16, kMaxCanvasSize);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(cW_);
    sizeRow->addWidget(new QLabel(QStringLiteral("×"), canvasPage));
    sizeRow->addWidget(cH_);
    cf->addRow(tr("Name:"), cName_);
    cf->addRow(tr("Resolution:"), cPreset_);
    cf->addRow(QString(), sizeRow);
    auto* cnote = new QLabel(tr("Each layer belongs to one canvas (Properties). Each output shows a region of a canvas: "
                                "use Layout... to split a large canvas across several projectors or screens."),
                             canvasPage);
    cnote->setWordWrap(true);
    cnote->setStyleSheet(QStringLiteral("color: gray;"));
    cf->addRow(QString(), cnote);

    // ---------------- formulário da saída
    auto* outputPage = new QWidget(this);
    auto* of = new QFormLayout(outputPage);
    name_ = new QLineEdit(outputPage);
    type_ = new QComboBox(outputPage);
    type_->addItem(tr("Display (fullscreen)"), static_cast<int>(OutputType::Display));
    type_->addItem(tr("Window"), static_cast<int>(OutputType::Window));
    type_->addItem(tr("NDI"), static_cast<int>(OutputType::Ndi));
    type_->addItem(tr("Blackmagic (SDI / HDMI)"), static_cast<int>(OutputType::DeckLink));
    enabled_ = new QCheckBox(tr("Enabled"), outputPage);
    canvas_ = new QComboBox(outputPage);

    pages_ = new QStackedWidget(outputPage);
    {
        auto* pDisplay = new QWidget(pages_);
        auto* fd = new QVBoxLayout(pDisplay);
        fd->setContentsMargins(0, 0, 0, 0);
        display_ = new QComboBox(pDisplay);
        display_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        auto* refresh = new QPushButton(tr("Refresh"), pDisplay);
        auto* ident = new QPushButton(tr("Identify"), pDisplay);
        connect(refresh, &QPushButton::clicked, this, &OutputsDialog::refreshDisplays);
        connect(ident, &QPushButton::clicked, this, &OutputsDialog::identify);
        auto* dispRow = new QHBoxLayout;
        dispRow->addWidget(refresh);
        dispRow->addWidget(ident);
        dispRow->addStretch();
        displayWarning_ = new QLabel(pDisplay);
        displayWarning_->setWordWrap(true);
        displayWarning_->setStyleSheet(QStringLiteral("color: #ffb04a;"));
        source_ = new QLabel(pDisplay);
        source_->setStyleSheet(QStringLiteral("color: gray;"));
        fd->addWidget(display_);
        fd->addLayout(dispRow);
        fd->addWidget(displayWarning_);
        fd->addWidget(source_);
        pages_->addWidget(pDisplay);

        auto* pWindow = new QWidget(pages_);
        auto* fw = new QFormLayout(pWindow);
        winW_ = ispin(pWindow, 160, kMaxCanvasSize);
        winH_ = ispin(pWindow, 90, kMaxCanvasSize);
        fw->addRow(tr("Width (px):"), winW_);
        fw->addRow(tr("Height (px):"), winH_);
        pages_->addWidget(pWindow);

        auto* pNdi = new QWidget(pages_);
        auto* fn = new QFormLayout(pNdi);
        ndiName_ = new QLineEdit(pNdi);
        ndiName_->setPlaceholderText(tr("VTHOST - output name"));
        ndiAlpha_ = new QCheckBox(tr("Send with alpha (transparency)"), pNdi);
        auto* ndiNote = new QLabel(tr("Needs the free NDI Tools (NDI runtime) installed on this computer."), pNdi);
        ndiNote->setWordWrap(true);
        ndiNote->setStyleSheet(QStringLiteral("color: gray;"));
        fn->addRow(tr("Source name:"), ndiName_);
        fn->addRow(QString(), ndiAlpha_);
        fn->addRow(QString(), ndiNote);
        pages_->addWidget(pNdi);

        auto* pDeck = new QWidget(pages_);
        auto* fdk = new QVBoxLayout(pDeck);
        auto* deckNote = new QLabel(
            tr("Blackmagic DeckLink / UltraStudio / Intensity output is in development. "
               "It will need Blackmagic Desktop Video installed. The output is saved in the project and will start "
               "working when support arrives in an update."),
            pDeck);
        deckNote->setWordWrap(true);
        fdk->addWidget(deckNote);
        pages_->addWidget(pDeck);
    }

    srcX_ = ispin(outputPage, 0, kMaxCanvasSize);
    srcY_ = ispin(outputPage, 0, kMaxCanvasSize);
    srcW_ = ispin(outputPage, 0, kMaxCanvasSize);
    srcH_ = ispin(outputPage, 0, kMaxCanvasSize);
    srcW_->setSpecialValueText(tr("whole"));
    srcH_->setSpecialValueText(tr("whole"));
    auto* wholeBtn = new QPushButton(tr("Whole canvas"), outputPage);
    auto* regionRow = new QHBoxLayout;
    for (auto* w : {srcX_, srcY_, srcW_, srcH_}) regionRow->addWidget(w);
    regionRow->addWidget(wholeBtn);

    blendL_ = ispin(outputPage, 0, 4000);
    blendR_ = ispin(outputPage, 0, 4000);
    blendT_ = ispin(outputPage, 0, 4000);
    blendB_ = ispin(outputPage, 0, 4000);
    auto* blendRow = new QHBoxLayout;
    blendRow->addWidget(new QLabel(tr("left"), outputPage));
    blendRow->addWidget(blendL_);
    blendRow->addWidget(new QLabel(tr("right"), outputPage));
    blendRow->addWidget(blendR_);
    blendRow->addWidget(new QLabel(tr("top"), outputPage));
    blendRow->addWidget(blendT_);
    blendRow->addWidget(new QLabel(tr("bottom"), outputPage));
    blendRow->addWidget(blendB_);
    smooth_ = dspin(outputPage, 0.5, 8.0, 0.1, 2);
    gamma_ = dspin(outputPage, 1.0, 4.0, 0.1, 2);
    auto* curveRow = new QHBoxLayout;
    curveRow->addWidget(new QLabel(tr("smoothness"), outputPage));
    curveRow->addWidget(smooth_);
    curveRow->addWidget(new QLabel(tr("gamma"), outputPage));
    curveRow->addWidget(gamma_);
    curveRow->addStretch();
    bright_ = dspin(outputPage, 0.0, 200.0, 1.0, 0, QStringLiteral(" %"));

    auto* mapBtn = new QPushButton(tr("Edit mapping (warp)..."), outputPage);
    connect(mapBtn, &QPushButton::clicked, this, &OutputsDialog::editMapping);
    warpInfo_ = new QLabel(outputPage);
    warpInfo_->setStyleSheet(QStringLiteral("color: gray;"));
    auto* mapRow = new QHBoxLayout;
    mapRow->addWidget(mapBtn);
    mapRow->addWidget(warpInfo_, 1);

    status_ = new QLabel(outputPage);
    status_->setWordWrap(true);

    of->addRow(tr("Name:"), name_);
    of->addRow(tr("Type:"), type_);
    of->addRow(QString(), enabled_);
    of->addRow(tr("Destination:"), pages_);
    of->addRow(tr("Canvas:"), canvas_);
    of->addRow(tr("Region (x, y, w, h):"), regionRow);
    of->addRow(tr("Soft-edge:"), blendRow);
    of->addRow(QString(), curveRow);
    of->addRow(tr("Brightness:"), bright_);
    of->addRow(tr("Mapping:"), mapRow);
    of->addRow(tr("Status:"), status_);

    formStack_ = new QStackedWidget(this);
    formStack_->addWidget(outputPage);
    formStack_->addWidget(canvasPage);

    auto* top = new QHBoxLayout;
    auto* leftBox = new QWidget(this);
    leftBox->setLayout(left);
    leftBox->setFixedWidth(300);
    top->addWidget(leftBox);
    top->addWidget(formStack_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        commitForm();
        commitCanvas();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);

    // ---------------- ligações: tudo é aplicado ao vivo
    auto commitOut = [this] {
        if (loading_) return;
        commitForm();
        changed();
    };
    auto commitCv = [this] {
        if (loading_) return;
        commitCanvas();
        changed();
    };
    connect(name_, &QLineEdit::editingFinished, this, commitOut);
    connect(type_, &QComboBox::currentIndexChanged, this, [this, commitOut] {
        pages_->setCurrentIndex(type_->currentIndex());
        commitOut();
    });
    connect(enabled_, &QCheckBox::toggled, this, commitOut);
    connect(display_, &QComboBox::currentIndexChanged, this, commitOut);
    connect(canvas_, &QComboBox::currentIndexChanged, this, commitOut);
    connect(ndiName_, &QLineEdit::editingFinished, this, commitOut);
    connect(ndiAlpha_, &QCheckBox::toggled, this, commitOut);
    for (QSpinBox* s : {winW_, winH_, srcX_, srcY_, srcW_, srcH_, blendL_, blendR_, blendT_, blendB_})
        connect(s, &QSpinBox::valueChanged, this, commitOut);
    for (QDoubleSpinBox* s : {smooth_, gamma_, bright_}) connect(s, &QDoubleSpinBox::valueChanged, this, commitOut);
    connect(wholeBtn, &QPushButton::clicked, this, [this, commitOut] {
        loading_ = true;
        srcX_->setValue(0);
        srcY_->setValue(0);
        srcW_->setValue(0);
        srcH_->setValue(0);
        loading_ = false;
        commitOut();
    });
    connect(cName_, &QLineEdit::editingFinished, this, commitCv);
    connect(cW_, &QSpinBox::valueChanged, this, commitCv);
    connect(cH_, &QSpinBox::valueChanged, this, commitCv);
    connect(cPreset_, &QComboBox::currentIndexChanged, this, [this, commitCv] {
        if (loading_) return;
        const QSize s = cPreset_->currentData().toSize();
        if (s.isEmpty()) return;
        loading_ = true;
        cW_->setValue(s.width());
        cH_->setValue(s.height());
        loading_ = false;
        commitCv();
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (loading_ || row < 0) return;
        commitForm();
        {
            const QSignalBlocker b(canvasList_);
            canvasList_->clearSelection();
            canvasList_->setCurrentRow(-1);
        }
        showOutput(row);
    });
    connect(canvasList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (loading_ || row < 0) return;
        commitCanvas();
        {
            const QSignalBlocker b(list_);
            list_->clearSelection();
            list_->setCurrentRow(-1);
        }
        showCanvas(row);
    });

    auto* statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &OutputsDialog::updateStatus);
    statusTimer->start(1000);

    setDisplays(qtDisplays(), false);
    refreshDisplays();
    rebuildLists();
    if (!outputs_.empty()) list_->setCurrentRow(0);
    else canvasList_->setCurrentRow(0);
}

void OutputsDialog::changed() {
    rebuildLists();
    emit liveChanged();
}

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

void OutputsDialog::rebuildLists() {
    const bool wasLoading = loading_;
    loading_ = true;
    {
        const QSignalBlocker b1(canvasList_), b2(list_);
        canvasList_->clear();
        for (const auto& c : canvases_)
            canvasList_->addItem(QStringLiteral("%1  ·  %2 × %3").arg(qs(c.name)).arg(c.width).arg(c.height));
        if (currentCanvas_ >= 0 && currentCanvas_ < canvasList_->count() && formStack_->currentIndex() == 1)
            canvasList_->setCurrentRow(currentCanvas_);

        list_->clear();
        for (const auto& o : outputs_) {
            QString type;
            switch (o.type) {
            case OutputType::Display: type = tr("Display"); break;
            case OutputType::Window: type = tr("Window"); break;
            case OutputType::Ndi: type = QStringLiteral("NDI"); break;
            case OutputType::DeckLink: type = QStringLiteral("Blackmagic"); break;
            }
            QString cname;
            for (const auto& c : canvases_)
                if (c.id == o.canvasId || (o.canvasId.empty() && &c == &canvases_.front())) cname = qs(c.name);
            auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2  ·  %3").arg(qs(o.name), type, cname), list_);
            if (!o.enabled) item->setForeground(QColor(130, 130, 130));
        }
        if (current_ >= 0 && current_ < list_->count() && formStack_->currentIndex() == 0) list_->setCurrentRow(current_);
    }
    loading_ = wasLoading;
}

void OutputsDialog::showCanvas(int index) {
    currentCanvas_ = (index >= 0 && index < static_cast<int>(canvases_.size())) ? index : -1;
    formStack_->setCurrentIndex(1);
    if (currentCanvas_ < 0) return;
    loading_ = true;
    const Canvas& c = canvases_[static_cast<std::size_t>(currentCanvas_)];
    cName_->setText(qs(c.name));
    cW_->setValue(c.width);
    cH_->setValue(c.height);
    cPreset_->setCurrentIndex(0);
    for (int i = 1; i < cPreset_->count(); ++i)
        if (cPreset_->itemData(i).toSize() == QSize(c.width, c.height)) cPreset_->setCurrentIndex(i);
    loading_ = false;
}

void OutputsDialog::commitCanvas() {
    if (currentCanvas_ < 0 || currentCanvas_ >= static_cast<int>(canvases_.size()) || formStack_->currentIndex() != 1)
        return;
    Canvas& c = canvases_[static_cast<std::size_t>(currentCanvas_)];
    const QString n = cName_->text().trimmed();
    if (!n.isEmpty()) c.name = n.toStdString();
    c.width = cW_->value();
    c.height = cH_->value();
}

void OutputsDialog::showOutput(int index) {
    loading_ = true;
    formStack_->setCurrentIndex(0);
    current_ = (index >= 0 && index < static_cast<int>(outputs_.size())) ? index : -1;
    formStack_->widget(0)->setEnabled(current_ >= 0);
    if (current_ < 0) {
        name_->clear();
        status_->clear();
        loading_ = false;
        return;
    }
    const Output& o = outputs_[static_cast<std::size_t>(current_)];
    name_->setText(qs(o.name));
    type_->setCurrentIndex(std::max(0, type_->findData(static_cast<int>(o.type))));
    pages_->setCurrentIndex(type_->currentIndex());
    enabled_->setChecked(o.enabled);

    canvas_->clear();
    for (const auto& c : canvases_) canvas_->addItem(QStringLiteral("%1 (%2 × %3)").arg(qs(c.name)).arg(c.width).arg(c.height), qs(c.id));
    int ci = canvas_->findData(qs(o.canvasId));
    canvas_->setCurrentIndex(ci >= 0 ? ci : 0);

    display_->clear();
    int sel = -1;
    for (const auto& d : displays_) {
        display_->addItem(displayLabel(d), qs(d.id));
        if (!o.displayId.empty() && (d.id == o.displayId || d.name == o.displayId)) sel = display_->count() - 1;
    }
    if (o.displayId.empty()) {
        display_->addItem(tr("Screen number %1 (automatic)").arg(o.monitor + 1), QString());
        if (sel < 0) sel = display_->count() - 1;
    } else if (sel < 0) {
        display_->addItem(tr("%1 (disconnected)").arg(qs(o.displayId)), qs(o.displayId));
        sel = display_->count() - 1;
    }
    display_->setCurrentIndex(sel);
    winW_->setValue(o.windowWidth);
    winH_->setValue(o.windowHeight);
    ndiName_->setText(qs(o.ndiName));
    ndiAlpha_->setChecked(o.ndiAlpha);

    const OutputMapping& m = o.mapping;
    srcX_->setValue(static_cast<int>(m.srcX));
    srcY_->setValue(static_cast<int>(m.srcY));
    srcW_->setValue(static_cast<int>(m.srcW));
    srcH_->setValue(static_cast<int>(m.srcH));
    blendL_->setValue(static_cast<int>(m.blendLeft));
    blendR_->setValue(static_cast<int>(m.blendRight));
    blendT_->setValue(static_cast<int>(m.blendTop));
    blendB_->setValue(static_cast<int>(m.blendBottom));
    smooth_->setValue(m.blendSmoothness);
    gamma_->setValue(m.blendGamma);
    bright_->setValue(m.brightness * 100.0);
    warpInfo_->setText(m.mode == WarpMode::None          ? tr("no warp")
                       : m.mode == WarpMode::Perspective ? tr("perspective")
                                                         : tr("warping grid %1 × %2").arg(m.meshCols).arg(m.meshRows));

    displayWarning_->clear();
    for (const auto& d : displays_)
        if (o.type == OutputType::Display && d.primary && d.id == o.displayId)
            displayWarning_->setText(tr("Warning: this is the main screen, where VTHOST Studio is open. "
                                        "The output will cover the interface."));
    loading_ = false;
    updateStatus();
}

void OutputsDialog::commitForm() {
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size()) || formStack_->currentIndex() != 0) return;
    Output& o = outputs_[static_cast<std::size_t>(current_)];
    const QString n = name_->text().trimmed();
    if (!n.isEmpty()) o.name = n.toStdString();
    o.type = static_cast<OutputType>(type_->currentData().toInt());
    o.enabled = enabled_->isChecked();
    if (display_->currentIndex() >= 0) {
        const std::string id = display_->currentData().toString().toStdString();
        o.displayId = id;
        for (const auto& d : displays_)
            if (d.id == id) o.monitor = d.index;
    }
    if (canvas_->currentIndex() >= 0) o.canvasId = canvas_->currentData().toString().toStdString();
    o.windowWidth = winW_->value();
    o.windowHeight = winH_->value();
    o.ndiName = ndiName_->text().trimmed().toStdString();
    o.ndiAlpha = ndiAlpha_->isChecked();
    OutputMapping& m = o.mapping;
    m.srcX = srcX_->value();
    m.srcY = srcY_->value();
    m.srcW = srcW_->value();
    m.srcH = srcH_->value();
    m.blendLeft = blendL_->value();
    m.blendRight = blendR_->value();
    m.blendTop = blendT_->value();
    m.blendBottom = blendB_->value();
    m.blendSmoothness = smooth_->value();
    m.blendGamma = gamma_->value();
    m.brightness = bright_->value() / 100.0;
    m.sanitize();
}

void OutputsDialog::addOutput(OutputType type) {
    commitForm();
    Output o;
    for (int i = 1;; ++i) {
        const std::string id = "O" + std::to_string(i);
        if (std::none_of(outputs_.begin(), outputs_.end(), [&](const Output& x) { return x.id == id; })) {
            o.id = id;
            break;
        }
    }
    o.name = tr("Output %1").arg(outputs_.size() + 1).toStdString();
    o.type = type;
    if (currentCanvas_ >= 0 && currentCanvas_ < static_cast<int>(canvases_.size()))
        o.canvasId = canvases_[static_cast<std::size_t>(currentCanvas_)].id;
    o.mapping.sanitize();
    // Ecrã por omissão: o primeiro que não é o principal e ainda não está usado.
    if (type == OutputType::Display) {
        for (const auto& d : displays_) {
            const bool used = std::any_of(outputs_.begin(), outputs_.end(), [&](const Output& x) {
                return x.type == OutputType::Display && x.displayId == d.id;
            });
            if (!d.primary && !used) {
                o.displayId = d.id;
                o.monitor = d.index;
                break;
            }
        }
    }
    outputs_.push_back(o);
    current_ = static_cast<int>(outputs_.size()) - 1;
    formStack_->setCurrentIndex(0);
    changed();
    showOutput(current_);
}

void OutputsDialog::removeOutput() {
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size())) return;
    outputs_.erase(outputs_.begin() + current_);
    current_ = std::min(current_, static_cast<int>(outputs_.size()) - 1);
    changed();
    showOutput(current_);
}

void OutputsDialog::addCanvas() {
    commitCanvas();
    Canvas c;
    for (int i = 1;; ++i) {
        const std::string id = "C" + std::to_string(i);
        if (std::none_of(canvases_.begin(), canvases_.end(), [&](const Canvas& x) { return x.id == id; })) {
            c.id = id;
            break;
        }
    }
    c.name = tr("Canvas %1").arg(canvases_.size() + 1).toStdString();
    canvases_.push_back(c);
    currentCanvas_ = static_cast<int>(canvases_.size()) - 1;
    formStack_->setCurrentIndex(1);
    changed();
    showCanvas(currentCanvas_);
}

void OutputsDialog::removeCanvas() {
    if (currentCanvas_ < 0 || currentCanvas_ >= static_cast<int>(canvases_.size())) return;
    if (canvases_.size() <= 1) {
        QMessageBox::information(this, tr("Canvases"), tr("A project needs at least one canvas."));
        return;
    }
    const std::string id = canvases_[static_cast<std::size_t>(currentCanvas_)].id;
    canvases_.erase(canvases_.begin() + currentCanvas_);
    for (auto& o : outputs_)
        if (o.canvasId == id) o.canvasId.clear(); // passa para o primeiro canvas
    currentCanvas_ = 0;
    changed();
    showCanvas(currentCanvas_);
}

void OutputsDialog::layoutWizard() {
    commitCanvas();
    if (currentCanvas_ < 0) currentCanvas_ = 0;
    Canvas& cv = canvases_[static_cast<std::size_t>(currentCanvas_)];

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Layout — %1").arg(qs(cv.name)));
    auto* f = new QFormLayout(&dlg);
    auto* cols = ispin(&dlg, 1, 16, QString());
    auto* rows = ispin(&dlg, 1, 16, QString());
    cols->setValue(2);
    rows->setValue(1);
    auto* ow = ispin(&dlg, 16, 16384);
    auto* oh = ispin(&dlg, 16, 16384);
    ow->setValue(1920);
    oh->setValue(1080);
    auto* ovH = ispin(&dlg, 0, 4000);
    auto* ovV = ispin(&dlg, 0, 4000);
    ovH->setValue(0);
    auto* assign = new QCheckBox(tr("Create the outputs and assign them to the screens (left to right)"), &dlg);
    assign->setChecked(true);
    auto* result = new QLabel(&dlg);
    auto updateResult = [&] {
        int w = 0, h = 0;
        computeLayout(cols->value(), rows->value(), ow->value(), oh->value(), ovH->value(), ovV->value(), w, h);
        result->setText(tr("Canvas: %1 × %2 px, %3 outputs").arg(w).arg(h).arg(cols->value() * rows->value()));
    };
    for (QSpinBox* s : {cols, rows, ow, oh, ovH, ovV}) connect(s, &QSpinBox::valueChanged, &dlg, updateResult);
    updateResult();
    auto* grid = new QHBoxLayout;
    grid->addWidget(cols);
    grid->addWidget(new QLabel(QStringLiteral("×"), &dlg));
    grid->addWidget(rows);
    auto* res = new QHBoxLayout;
    res->addWidget(ow);
    res->addWidget(new QLabel(QStringLiteral("×"), &dlg));
    res->addWidget(oh);
    auto* ov = new QHBoxLayout;
    ov->addWidget(ovH);
    ov->addWidget(ovV);
    f->addRow(tr("Outputs (horizontal × vertical):"), grid);
    f->addRow(tr("Resolution of each output:"), res);
    f->addRow(tr("Overlap for soft-edge (horizontal, vertical):"), ov);
    f->addRow(QString(), assign);
    f->addRow(QString(), result);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    f->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    int w = 0, h = 0;
    const auto cells = computeLayout(cols->value(), rows->value(), ow->value(), oh->value(), ovH->value(), ovV->value(), w, h);
    cv.width = w;
    cv.height = h;
    const std::string cid = cv.id;
    // Reutiliza as saídas que já mostram este canvas (pela ordem) e cria as que faltam.
    std::vector<std::size_t> existing;
    for (std::size_t i = 0; i < outputs_.size(); ++i)
        if (outputs_[i].canvasId == cid || (outputs_[i].canvasId.empty() && currentCanvas_ == 0)) existing.push_back(i);
    std::vector<protocol::DisplayInfo> freeDisplays;
    for (const auto& d : displays_)
        if (!d.primary) freeDisplays.push_back(d);
    for (std::size_t k = 0; k < cells.size(); ++k) {
        Output* o = nullptr;
        if (k < existing.size()) {
            o = &outputs_[existing[k]];
        } else if (assign->isChecked()) {
            Output n;
            for (int i = 1;; ++i) {
                const std::string id = "O" + std::to_string(i);
                if (std::none_of(outputs_.begin(), outputs_.end(), [&](const Output& x) { return x.id == id; })) {
                    n.id = id;
                    break;
                }
            }
            n.name = tr("%1 %2").arg(qs(cv.name)).arg(k + 1).toStdString();
            n.type = OutputType::Display;
            outputs_.push_back(n);
            o = &outputs_.back();
            existing.push_back(outputs_.size() - 1);
        } else {
            break;
        }
        const OutputMapping& cm = cells[k].mapping;
        o->canvasId = cid;
        // Mantém o warp já afinado; muda só a região e o soft-edge.
        o->mapping.srcX = cm.srcX;
        o->mapping.srcY = cm.srcY;
        o->mapping.srcW = cm.srcW;
        o->mapping.srcH = cm.srcH;
        o->mapping.blendLeft = cm.blendLeft;
        o->mapping.blendRight = cm.blendRight;
        o->mapping.blendTop = cm.blendTop;
        o->mapping.blendBottom = cm.blendBottom;
        o->mapping.sanitize();
        if (assign->isChecked() && o->type == OutputType::Display && k < freeDisplays.size()) {
            o->displayId = freeDisplays[k].id;
            o->monitor = freeDisplays[k].index;
        }
    }
    changed();
    showCanvas(currentCanvas_);
}

void OutputsDialog::outputPixelSize(const Output& o, int& w, int& h) const {
    w = static_cast<int>(o.mapping.srcW);
    h = static_cast<int>(o.mapping.srcH);
    if (o.type == OutputType::Display)
        for (const auto& d : displays_)
            if (d.id == o.displayId) {
                w = d.width;
                h = d.height;
            }
    if (o.type == OutputType::Window) {
        w = o.windowWidth;
        h = o.windowHeight;
    }
    if (w <= 0 || h <= 0) {
        for (const auto& c : canvases_)
            if (c.id == o.canvasId || (o.canvasId.empty() && &c == &canvases_.front())) {
                w = c.width;
                h = c.height;
            }
    }
    if (w <= 0 || h <= 0) {
        w = 1920;
        h = 1080;
    }
}

void OutputsDialog::editMapping() {
    commitForm();
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size())) return;
    const Output o = outputs_[static_cast<std::size_t>(current_)];
    int w = 0, h = 0;
    outputPixelSize(o, w, h);
    const std::string outId = o.id;
    QPointer<OutputsDialog> self(this);
    EngineClient* engine = engine_;
    auto live = [engine, outId](const OutputMapping& m) {
        if (!engine || !engine->isConnected()) return;
        protocol::Command c;
        c.type = protocol::CommandType::SetOutputMapping;
        c.output = outId;
        c.mapping = m;
        engine->send(c);
    };
    // O test card ajuda a alinhar: liga-se ao abrir o editor (e volta ao estado anterior no fim).
    const bool hadTestCard = testCard_->isChecked();
    if (!hadTestCard) testCard_->setChecked(true);
    MappingEditor ed(qs(o.name), o.mapping, w, h, live, testCard_->isChecked(),
                     [self](bool on) {
                         if (self) self->testCard_->setChecked(on);
                     },
                     this);
    const bool ok = ed.exec() == QDialog::Accepted;
    if (ok) outputs_[static_cast<std::size_t>(current_)].mapping = ed.mapping();
    else live(o.mapping); // cancelar: repõe o mapping anterior no motor
    if (!hadTestCard) testCard_->setChecked(false);
    if (ok) changed();
    showOutput(current_);
}

void OutputsDialog::setTestCard(bool on) {
    if (!engine_ || !engine_->isConnected()) return;
    protocol::Command c;
    c.type = protocol::CommandType::SetTestCard;
    c.on = on;
    engine_->send(c);
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
    if (!engine_ || !engine_->isConnected() || !engine_->hasStatus()) {
        status_->setText(tr("Engine offline"));
        status_->setStyleSheet(QStringLiteral("color: gray;"));
        return;
    }
    for (const auto& s : engine_->lastStatus().outputs) {
        if (s.id != o.id) continue;
        if (s.ok) {
            status_->setText(QStringLiteral("● ") + (s.detail.empty() ? tr("OK") : qs(s.detail)));
            status_->setStyleSheet(QStringLiteral("color: #3c3;"));
        } else {
            status_->setText(QStringLiteral("● ") + qs(s.error));
            status_->setStyleSheet(QStringLiteral("color: #e44;"));
        }
        return;
    }
    status_->setText(tr("Applying..."));
    status_->setStyleSheet(QStringLiteral("color: gray;"));
}
