#include "outputs_dialog.h"

#include "output_editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
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

QDoubleSpinBox* dspin(QWidget* parent, double min, double max, int decimals, const QString& suffix, double step) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setDecimals(decimals);
    s->setSuffix(suffix);
    s->setSingleStep(step);
    s->setKeyboardTracking(false);
    s->setAccelerated(true);
    return s;
}

QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("font-weight: bold; color: #6cf5d2; padding-top: 8px;"));
    return l;
}

std::string sourceKey(const Slice& s) {
    if (s.source == SliceSourceKind::Composition) return "comp:" + s.compositionId;
    if (s.source == SliceSourceKind::Layer) return "layer:" + s.compositionId + ":" + s.layerId;
    return "media";
}

} // namespace

OutputsDialog::OutputsDialog(int width, int height, const std::vector<Output>& outputs,
                             const std::vector<Composition>& compositions, EngineClient* engine, QWidget* parent)
    : QDialog(parent), engine_(engine), outputs_(outputs), comps_(compositions) {
    setWindowTitle(tr("Advanced Output"));
    auto* layout = new QVBoxLayout(this);

    // Resolução de trabalho do MEDIAPLAYER (as composições têm a sua, no CUEPLAYER).
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
    resRow->addWidget(new QLabel(tr("MEDIAPLAYER resolution:"), this));
    resRow->addWidget(preset_);
    resRow->addWidget(width_);
    resRow->addWidget(new QLabel(QStringLiteral("×"), this));
    resRow->addWidget(height_);
    resRow->addStretch();
    layout->addLayout(resRow);

    // Árvore Screens > Slices + editor.
    auto* mid = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setMinimumWidth(240);
    tree_->setMaximumWidth(320);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item || loading_) return;
        commitScreenForm();
        commitSliceForm();
        current_ = item->data(0, Qt::UserRole).toInt();
        currentSlice_ = item->data(0, Qt::UserRole + 1).toInt();
        showCurrent();
    });
    left->addWidget(tree_, 1);
    auto* addRow = new QHBoxLayout;
    auto* addScreen = new QPushButton(tr("+ Screen"), this);
    auto* addWindow = new QPushButton(tr("+ Window"), this);
    auto* addNdi = new QPushButton(tr("+ NDI"), this);
    connect(addScreen, &QPushButton::clicked, this, [this] { addOutput(OutputType::Display); });
    connect(addWindow, &QPushButton::clicked, this, [this] { addOutput(OutputType::Window); });
    connect(addNdi, &QPushButton::clicked, this, [this] { addOutput(OutputType::Ndi); });
    addRow->addWidget(addScreen);
    addRow->addWidget(addWindow);
    addRow->addWidget(addNdi);
    left->addLayout(addRow);
    auto* sliceRow = new QHBoxLayout;
    auto* addSliceBtn = new QPushButton(tr("+ Slice"), this);
    addSliceBtn->setToolTip(tr("Add a slice to the selected screen"));
    auto* dup = new QPushButton(tr("Duplicate"), this);
    auto* remove = new QPushButton(tr("Remove"), this);
    connect(addSliceBtn, &QPushButton::clicked, this, &OutputsDialog::addSlice);
    connect(dup, &QPushButton::clicked, this, &OutputsDialog::duplicateCurrent);
    connect(remove, &QPushButton::clicked, this, &OutputsDialog::removeCurrent);
    sliceRow->addWidget(addSliceBtn);
    sliceRow->addWidget(dup);
    sliceRow->addWidget(remove);
    left->addLayout(sliceRow);
    mid->addLayout(left);

    right_ = new QStackedWidget(this);
    auto* empty = new QLabel(tr("Add a screen to send the image to a display, a window or the network."), right_);
    empty->setAlignment(Qt::AlignCenter);
    right_->addWidget(empty);
    right_->addWidget(buildScreenForm());
    right_->addWidget(buildSliceEditor());
    mid->addWidget(right_, 1);
    layout->addLayout(mid, 1);

    source_ = new QLabel(this);
    source_->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(source_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        commitScreenForm();
        commitSliceForm();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (auto* c : {screenCanvas_, inputCanvas_, outputCanvas_})
        c->setSourceInfo([this](const Slice& s) { return sourceSize(s); }, [this](const Slice& s) { return sourceName(s); });

    setDisplays(qtDisplays(), false);
    refreshDisplays();
    // Saídas antigas sem slices: um slice de ecrã inteiro com o MEDIAPLAYER.
    for (auto& o : outputs_)
        if (o.slices.empty()) {
            Slice s = makeFullSlice(SliceSourceKind::MediaPlayer, o.width, o.height);
            s.keepAspect = true;
            o.slices.push_back(std::move(s));
        }
    if (!outputs_.empty()) current_ = 0;
    rebuildTree();
    showCurrent();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &OutputsDialog::updateStatus);
    timer->start(500);
    resize(1240, 760);
}

QWidget* OutputsDialog::buildScreenForm() {
    auto* page = new QWidget(this);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    auto* form = new QWidget(page);
    auto* f = new QFormLayout(form);
    name_ = new QLineEdit(form);
    enabled_ = new QCheckBox(tr("Enabled"), form);
    type_ = new QComboBox(form);
    type_->addItem(tr("Screen (full screen)"), static_cast<int>(OutputType::Display));
    type_->addItem(tr("Window"), static_cast<int>(OutputType::Window));
    type_->addItem(tr("NDI (network)"), static_cast<int>(OutputType::Ndi));
    f->addRow(tr("Name:"), name_);
    f->addRow(QString(), enabled_);
    f->addRow(tr("Type:"), type_);
    pages_ = new QStackedWidget(form);
    {
        auto* p = new QWidget(pages_);
        auto* pf = new QFormLayout(p);
        pf->setContentsMargins(0, 0, 0, 0);
        display_ = new QComboBox(p);
        display_->setMinimumWidth(320);
        auto* identifyBtn = new QPushButton(tr("Identify screens"), p);
        connect(identifyBtn, &QPushButton::clicked, this, &OutputsDialog::identify);
        pf->addRow(tr("Screen:"), display_);
        pf->addRow(QString(), identifyBtn);
        pages_->addWidget(p);
    }
    {
        auto* p = new QWidget(pages_);
        auto* pf = new QFormLayout(p);
        pf->setContentsMargins(0, 0, 0, 0);
        winW_ = new QSpinBox(p);
        winH_ = new QSpinBox(p);
        winW_->setRange(160, kMaxCanvasSize);
        winH_->setRange(90, kMaxCanvasSize);
        auto* row = new QHBoxLayout;
        row->addWidget(winW_);
        row->addWidget(new QLabel(QStringLiteral("×"), p));
        row->addWidget(winH_);
        pf->addRow(tr("Window size:"), row);
        pages_->addWidget(p);
    }
    {
        auto* p = new QWidget(pages_);
        auto* pf = new QFormLayout(p);
        pf->setContentsMargins(0, 0, 0, 0);
        ndiName_ = new QLineEdit(p);
        ndiName_->setPlaceholderText(tr("VTHOST - <output name>"));
        ndiAlpha_ = new QCheckBox(tr("Send with transparency (alpha)"), p);
        pf->addRow(tr("NDI name:"), ndiName_);
        pf->addRow(QString(), ndiAlpha_);
        auto* note = new QLabel(tr("Needs the free NDI Runtime / NDI Tools installed on this PC."), p);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: gray;"));
        pf->addRow(QString(), note);
        pages_->addWidget(p);
    }
    f->addRow(pages_);
    scrW_ = new QSpinBox(form);
    scrH_ = new QSpinBox(form);
    for (auto* s : {scrW_, scrH_}) {
        s->setRange(16, kMaxCanvasSize);
        s->setKeyboardTracking(false);
    }
    auto* res = new QHBoxLayout;
    res->addWidget(scrW_);
    res->addWidget(new QLabel(QStringLiteral("×"), form));
    res->addWidget(scrH_);
    auto* match = new QPushButton(tr("Match the screen"), form);
    match->setToolTip(tr("Use the resolution of the chosen display or window"));
    connect(match, &QPushButton::clicked, this, [this] {
        commitScreenForm();
        Output* o = currentOutput();
        if (!o) return;
        const QSize z = displaySize(*o);
        if (!z.isValid()) return;
        o->width = z.width();
        o->height = z.height();
        loadScreenForm();
        changed();
    });
    res->addWidget(match);
    res->addStretch();
    f->addRow(tr("Screen resolution:"), res);
    auto* resNote = new QLabel(tr("Slice positions are in pixels of this resolution."), form);
    resNote->setStyleSheet(QStringLiteral("color: gray;"));
    f->addRow(QString(), resNote);
    status_ = new QLabel(form);
    status_->setWordWrap(true);
    f->addRow(tr("State:"), status_);
    v->addWidget(form);
    screenCanvas_ = new SliceCanvas(SliceCanvas::Mode::Output, page);
    screenCanvas_->setEditable(false);
    connect(screenCanvas_, &SliceCanvas::sliceClicked, this, [this](int i) { select(current_, i); });
    v->addWidget(screenCanvas_, 1);

    connect(name_, &QLineEdit::editingFinished, this, &OutputsDialog::changed);
    connect(enabled_, &QCheckBox::toggled, this, &OutputsDialog::changed);
    connect(type_, &QComboBox::activated, this, &OutputsDialog::changed);
    connect(display_, &QComboBox::activated, this, &OutputsDialog::changed);
    for (auto* s : {winW_, winH_, scrW_, scrH_}) {
        s->setKeyboardTracking(false);
        connect(s, &QSpinBox::valueChanged, this, &OutputsDialog::changed);
    }
    connect(ndiName_, &QLineEdit::editingFinished, this, &OutputsDialog::changed);
    connect(ndiAlpha_, &QCheckBox::toggled, this, &OutputsDialog::changed);
    return page;
}

QWidget* OutputsDialog::buildSliceEditor() {
    auto* page = new QWidget(this);
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(0, 0, 0, 0);

    sliceTabs_ = new QTabWidget(page);
    inputCanvas_ = new SliceCanvas(SliceCanvas::Mode::Input, sliceTabs_);
    auto* outPage = new QWidget(sliceTabs_);
    auto* ov = new QVBoxLayout(outPage);
    ov->setContentsMargins(0, 0, 0, 0);
    auto* toolRow = new QHBoxLayout;
    const QString names[3] = {tr("Transform"), tr("Corners"), tr("Edit points")};
    const QString tips[3] = {tr("Drag to move, drag the corners to resize"), tr("Corner pin: drag each corner"),
                             tr("Mesh: drag the points (set the number of points on the right)")};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton(outPage);
        b->setText(names[i]);
        b->setToolTip(tips[i]);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setChecked(i == 0);
        connect(b, &QToolButton::clicked, this, [this, i] {
            outputCanvas_->setTool(static_cast<SliceCanvas::Tool>(i));
            if (i == 2) {
                Slice* s = currentSliceRef();
                if (s && s->meshCols == 0) {
                    // Primeira vez: 3×3 pontos para começar.
                    s->meshCols = s->meshRows = 2;
                    s->mesh.clear();
                    for (int r = 0; r <= 2; ++r)
                        for (int c = 0; c <= 2; ++c) s->mesh.push_back({c / 2.0, r / 2.0});
                    loadSliceForm();
                    changed();
                }
            }
        });
        tools_[i] = b;
        toolRow->addWidget(b);
    }
    toolRow->addStretch();
    auto* hint = new QLabel(tr("Shift: snap to 10 px · Alt: fine"), outPage);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    toolRow->addWidget(hint);
    ov->addLayout(toolRow);
    outputCanvas_ = new SliceCanvas(SliceCanvas::Mode::Output, outPage);
    ov->addWidget(outputCanvas_, 1);
    sliceTabs_->addTab(inputCanvas_, tr("Input Selection"));
    sliceTabs_->addTab(outPage, tr("Output Transformation"));
    sliceTabs_->setCurrentIndex(1);
    h->addWidget(sliceTabs_, 1);
    for (auto* c : {inputCanvas_, outputCanvas_}) {
        connect(c, &SliceCanvas::editing, this, [this] {
            sendLive();
            loadSliceForm();
        });
        connect(c, &SliceCanvas::edited, this, [this] {
            loadSliceForm();
            changed();
        });
        connect(c, &SliceCanvas::sliceClicked, this, [this](int i) { select(current_, i); });
    }

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(340);
    scroll->setMaximumWidth(380);
    auto* form = new QWidget(scroll);
    auto* f = new QFormLayout(form);
    sName_ = new QLineEdit(form);
    sEnabled_ = new QCheckBox(tr("Enabled"), form);
    sSource_ = new QComboBox(form);
    sSource_->setToolTip(tr("What this slice shows: the MEDIAPLAYER, a whole composition or one layer"));
    f->addRow(tr("Name:"), sName_);
    f->addRow(QString(), sEnabled_);
    f->addRow(tr("Source:"), sSource_);

    f->addRow(sectionLabel(tr("Input Selection"), form));
    sInX_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 1);
    sInY_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 1);
    sInW_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 1);
    sInH_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 1);
    sInW_->setSpecialValueText(tr("whole source"));
    sInH_->setSpecialValueText(tr("whole source"));
    auto* inPos = new QHBoxLayout;
    inPos->addWidget(sInX_);
    inPos->addWidget(sInY_);
    auto* inSize = new QHBoxLayout;
    inSize->addWidget(sInW_);
    inSize->addWidget(sInH_);
    f->addRow(tr("X / Y:"), inPos);
    f->addRow(tr("Width / Height:"), inSize);
    auto* wholeSrc = new QPushButton(tr("Whole source"), form);
    connect(wholeSrc, &QPushButton::clicked, this, [this] {
        if (Slice* s = currentSliceRef()) {
            s->inX = s->inY = s->inW = s->inH = 0.0;
            loadSliceForm();
            changed();
        }
    });
    f->addRow(QString(), wholeSrc);

    f->addRow(sectionLabel(tr("Output Transformation"), form));
    sX_ = dspin(form, -kMaxCanvasSize, 2 * kMaxCanvasSize, 0, tr(" px"), 1);
    sY_ = dspin(form, -kMaxCanvasSize, 2 * kMaxCanvasSize, 0, tr(" px"), 1);
    sW_ = dspin(form, 1, 4 * kMaxCanvasSize, 0, tr(" px"), 1);
    sH_ = dspin(form, 1, 4 * kMaxCanvasSize, 0, tr(" px"), 1);
    auto* pos = new QHBoxLayout;
    pos->addWidget(sX_);
    pos->addWidget(sY_);
    auto* size = new QHBoxLayout;
    size->addWidget(sW_);
    size->addWidget(sH_);
    f->addRow(tr("Centre X / Y:"), pos);
    f->addRow(tr("Width / Height:"), size);
    sRot_ = dspin(form, -360, 360, 1, QStringLiteral("°"), 1);
    f->addRow(tr("Rotation:"), sRot_);
    sFlipH_ = new QCheckBox(tr("Flip H"), form);
    sFlipV_ = new QCheckBox(tr("Flip V"), form);
    auto* flips = new QHBoxLayout;
    flips->addWidget(sFlipH_);
    flips->addWidget(sFlipV_);
    f->addRow(QString(), flips);
    sAspect_ = new QCheckBox(tr("Keep the source proportions"), form);
    f->addRow(QString(), sAspect_);
    auto* fitRow = new QHBoxLayout;
    auto* fill = new QPushButton(tr("Fill screen"), form);
    connect(fill, &QPushButton::clicked, this, [this] {
        Slice* s = currentSliceRef();
        Output* o = currentOutput();
        if (!s || !o) return;
        s->x = o->width / 2.0;
        s->y = o->height / 2.0;
        s->width = o->width;
        s->height = o->height;
        s->rotation = 0.0;
        loadSliceForm();
        changed();
    });
    auto* matchIn = new QPushButton(tr("Size of the input"), form);
    matchIn->setToolTip(tr("1:1 pixels: the slice gets the size of its input selection"));
    connect(matchIn, &QPushButton::clicked, this, [this] {
        Slice* s = currentSliceRef();
        if (!s) return;
        const QSize src = sourceSize(*s);
        s->width = s->inW > 0 ? s->inW : src.width();
        s->height = s->inH > 0 ? s->inH : src.height();
        loadSliceForm();
        changed();
    });
    fitRow->addWidget(fill);
    fitRow->addWidget(matchIn);
    f->addRow(QString(), fitRow);
    auto* resetCorners = new QPushButton(tr("Reset corners and points"), form);
    connect(resetCorners, &QPushButton::clicked, this, [this] {
        if (Slice* s = currentSliceRef()) {
            s->cornerOffsets = {};
            s->meshCols = s->meshRows = 0;
            s->mesh.clear();
            loadSliceForm();
            changed();
        }
    });
    f->addRow(QString(), resetCorners);
    sMeshCols_ = new QSpinBox(form);
    sMeshRows_ = new QSpinBox(form);
    for (auto* s : {sMeshCols_, sMeshRows_}) {
        s->setRange(0, kMaxMeshDivisions);
        s->setSpecialValueText(tr("off"));
        s->setKeyboardTracking(false);
    }
    auto* mesh = new QHBoxLayout;
    mesh->addWidget(sMeshCols_);
    mesh->addWidget(new QLabel(QStringLiteral("×"), form));
    mesh->addWidget(sMeshRows_);
    f->addRow(tr("Edit points (divisions):"), mesh);

    f->addRow(sectionLabel(tr("Colour"), form));
    sBright_ = dspin(form, -100, 100, 0, QStringLiteral(" %"), 1);
    sContrast_ = dspin(form, -100, 100, 0, QStringLiteral(" %"), 1);
    sRed_ = dspin(form, -100, 100, 0, QStringLiteral(" %"), 1);
    sGreen_ = dspin(form, -100, 100, 0, QStringLiteral(" %"), 1);
    sBlue_ = dspin(form, -100, 100, 0, QStringLiteral(" %"), 1);
    f->addRow(tr("Brightness:"), sBright_);
    f->addRow(tr("Contrast:"), sContrast_);
    f->addRow(tr("Red:"), sRed_);
    f->addRow(tr("Green:"), sGreen_);
    f->addRow(tr("Blue:"), sBlue_);

    f->addRow(sectionLabel(tr("Soft Edge"), form));
    sSoft_ = new QCheckBox(tr("Soft edge (projector blending)"), form);
    f->addRow(QString(), sSoft_);
    sEdgeL_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 5);
    sEdgeR_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 5);
    sEdgeT_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 5);
    sEdgeB_ = dspin(form, 0, kMaxCanvasSize, 0, tr(" px"), 5);
    auto* lr = new QHBoxLayout;
    lr->addWidget(sEdgeL_);
    lr->addWidget(sEdgeR_);
    auto* tb = new QHBoxLayout;
    tb->addWidget(sEdgeT_);
    tb->addWidget(sEdgeB_);
    f->addRow(tr("Left / Right:"), lr);
    f->addRow(tr("Top / Bottom:"), tb);
    sGamma_ = dspin(form, 0.5, 4.0, 2, QString(), 0.05);
    sPower_ = dspin(form, 0.5, 8.0, 2, QString(), 0.1);
    f->addRow(tr("Gamma:"), sGamma_);
    f->addRow(tr("Power:"), sPower_);
    scroll->setWidget(form);
    h->addWidget(scroll);

    connect(sName_, &QLineEdit::editingFinished, this, &OutputsDialog::changed);
    connect(sSource_, &QComboBox::activated, this, &OutputsDialog::changed);
    for (auto* c : {sEnabled_, sFlipH_, sFlipV_, sAspect_, sSoft_}) connect(c, &QCheckBox::toggled, this, &OutputsDialog::changed);
    for (auto* s : {sInX_, sInY_, sInW_, sInH_, sX_, sY_, sW_, sH_, sRot_, sBright_, sContrast_, sRed_, sGreen_, sBlue_, sEdgeL_,
                    sEdgeR_, sEdgeT_, sEdgeB_, sGamma_, sPower_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &OutputsDialog::changed);
    for (auto* s : {sMeshCols_, sMeshRows_}) connect(s, &QSpinBox::valueChanged, this, &OutputsDialog::changed);
    return page;
}

int OutputsDialog::canvasWidth() const { return width_->value(); }
int OutputsDialog::canvasHeight() const { return height_->value(); }

Output* OutputsDialog::currentOutput() {
    return current_ >= 0 && current_ < static_cast<int>(outputs_.size()) ? &outputs_[static_cast<std::size_t>(current_)] : nullptr;
}

Slice* OutputsDialog::currentSliceRef() {
    Output* o = currentOutput();
    if (!o || currentSlice_ < 0 || currentSlice_ >= static_cast<int>(o->slices.size())) return nullptr;
    return &o->slices[static_cast<std::size_t>(currentSlice_)];
}

QSize OutputsDialog::sourceSize(const Slice& s) const {
    if (s.source != SliceSourceKind::MediaPlayer)
        for (const auto& c : comps_)
            if (c.id == s.compositionId) return {c.width, c.height};
    return {width_->value(), height_->value()};
}

QString OutputsDialog::sourceName(const Slice& s) const {
    if (s.source == SliceSourceKind::MediaPlayer) return QStringLiteral("MEDIAPLAYER");
    for (const auto& c : comps_) {
        if (c.id != s.compositionId) continue;
        if (s.source == SliceSourceKind::Composition) return qs(c.name);
        if (const CompLayer* l = c.findLayer(s.layerId)) return qs(c.name) + QStringLiteral(" › ") + qs(l->name);
    }
    return tr("(missing source)");
}

QSize OutputsDialog::displaySize(const Output& o) const {
    if (o.type == OutputType::Window) return {o.windowWidth, o.windowHeight};
    if (o.type == OutputType::Display)
        for (const auto& d : displays_)
            if ((!o.displayId.empty() && d.id == o.displayId) || (o.displayId.empty() && d.index == o.monitor))
                return {d.width, d.height};
    return {};
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
    if (current_ >= 0 && currentSlice_ < 0) loadScreenForm();
}

void OutputsDialog::rebuildTree() {
    loading_ = true;
    tree_->clear();
    QTreeWidgetItem* sel = nullptr;
    for (int i = 0; i < static_cast<int>(outputs_.size()); ++i) {
        const Output& o = outputs_[static_cast<std::size_t>(i)];
        QString t = qs(o.name.empty() ? o.id : o.name);
        if (o.type == OutputType::Ndi) t += QStringLiteral("  [NDI]");
        else if (o.type == OutputType::Window) t += QStringLiteral("  [") + tr("window") + QStringLiteral("]");
        t += QStringLiteral("  %1×%2").arg(o.width).arg(o.height);
        if (!o.enabled) t += QStringLiteral("  (") + tr("off") + QStringLiteral(")");
        auto* item = new QTreeWidgetItem(tree_, {t});
        item->setData(0, Qt::UserRole, i);
        item->setData(0, Qt::UserRole + 1, -1);
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        if (i == current_ && currentSlice_ < 0) sel = item;
        for (int k = 0; k < static_cast<int>(o.slices.size()); ++k) {
            const Slice& s = o.slices[static_cast<std::size_t>(k)];
            QString st = qs(s.name) + QStringLiteral(" — ") + sourceName(s);
            if (!s.enabled) st += QStringLiteral("  (") + tr("off") + QStringLiteral(")");
            auto* child = new QTreeWidgetItem(item, {st});
            child->setData(0, Qt::UserRole, i);
            child->setData(0, Qt::UserRole + 1, k);
            if (i == current_ && k == currentSlice_) sel = child;
        }
        item->setExpanded(true);
    }
    if (sel) tree_->setCurrentItem(sel);
    loading_ = false;
}

void OutputsDialog::select(int screenIndex, int sliceIndex) {
    commitScreenForm();
    commitSliceForm();
    current_ = screenIndex;
    currentSlice_ = sliceIndex;
    rebuildTree();
    showCurrent();
}

void OutputsDialog::showCurrent() {
    Output* o = currentOutput();
    if (!o) {
        right_->setCurrentIndex(0);
        return;
    }
    if (currentSliceRef()) {
        right_->setCurrentIndex(2);
        loadSliceForm();
    } else {
        currentSlice_ = -1;
        right_->setCurrentIndex(1);
        loadScreenForm();
    }
}

void OutputsDialog::loadScreenForm() {
    Output* o = currentOutput();
    screenCanvas_->setOutput(o, -1);
    if (!o) return;
    loading_ = true;
    name_->setText(qs(o->name));
    enabled_->setChecked(o->enabled);
    type_->setCurrentIndex(std::max(0, type_->findData(static_cast<int>(o->type))));
    pages_->setCurrentIndex(o->type == OutputType::Display ? 0 : o->type == OutputType::Window ? 1 : 2);
    display_->clear();
    int sel = -1;
    for (const auto& d : displays_) {
        display_->addItem(displayLabel(d), qs(d.id));
        if ((!o->displayId.empty() && (d.id == o->displayId || d.name == o->displayId)) ||
            (o->displayId.empty() && d.index == o->monitor))
            sel = display_->count() - 1;
    }
    if (sel < 0 && !o->displayId.empty()) {
        display_->addItem(tr("%1 (not connected)").arg(qs(o->displayId)), qs(o->displayId));
        sel = display_->count() - 1;
    }
    display_->setCurrentIndex(std::max(0, sel));
    winW_->setValue(o->windowWidth);
    winH_->setValue(o->windowHeight);
    ndiName_->setText(qs(o->ndiName));
    ndiAlpha_->setChecked(o->ndiAlpha);
    scrW_->setValue(o->width);
    scrH_->setValue(o->height);
    loading_ = false;
    updateStatus();
}

void OutputsDialog::loadSliceForm() {
    Output* o = currentOutput();
    Slice* s = currentSliceRef();
    inputCanvas_->setOutput(o, currentSlice_);
    outputCanvas_->setOutput(o, currentSlice_);
    if (!s) return;
    loading_ = true;
    if (!sName_->hasFocus()) sName_->setText(qs(s->name));
    sEnabled_->setChecked(s->enabled);
    sSource_->clear();
    sSource_->addItem(QStringLiteral("MEDIAPLAYER"), QStringLiteral("media"));
    for (const auto& c : comps_) {
        sSource_->addItem(tr("Composition: %1").arg(qs(c.name)), QStringLiteral("comp:") + qs(c.id));
        for (auto it = c.layers.rbegin(); it != c.layers.rend(); ++it)
            sSource_->addItem(QStringLiteral("    ") + tr("%1 › %2").arg(qs(c.name), qs(it->name)),
                              QStringLiteral("layer:%1:%2").arg(qs(c.id), qs(it->id)));
    }
    int idx = sSource_->findData(qs(sourceKey(*s)));
    if (idx < 0) {
        sSource_->addItem(tr("(missing source)"), qs(sourceKey(*s)));
        idx = sSource_->count() - 1;
    }
    sSource_->setCurrentIndex(idx);
    const QSize src = sourceSize(*s);
    sInX_->setMaximum(src.width());
    sInY_->setMaximum(src.height());
    sInW_->setMaximum(src.width());
    sInH_->setMaximum(src.height());
    sInX_->setValue(s->inX);
    sInY_->setValue(s->inY);
    sInW_->setValue(s->inW);
    sInH_->setValue(s->inH);
    sX_->setValue(s->x);
    sY_->setValue(s->y);
    sW_->setValue(s->width);
    sH_->setValue(s->height);
    sRot_->setValue(s->rotation);
    sFlipH_->setChecked(s->flipH);
    sFlipV_->setChecked(s->flipV);
    sAspect_->setChecked(s->keepAspect);
    sMeshCols_->setValue(s->meshCols);
    sMeshRows_->setValue(s->meshRows);
    sBright_->setValue(s->brightness * 100.0);
    sContrast_->setValue(s->contrast * 100.0);
    sRed_->setValue(s->red * 100.0);
    sGreen_->setValue(s->green * 100.0);
    sBlue_->setValue(s->blue * 100.0);
    sSoft_->setChecked(s->softEdge);
    sEdgeL_->setValue(s->edgeLeft);
    sEdgeR_->setValue(s->edgeRight);
    sEdgeT_->setValue(s->edgeTop);
    sEdgeB_->setValue(s->edgeBottom);
    sGamma_->setValue(s->edgeGamma);
    sPower_->setValue(s->edgePower);
    for (auto* w : std::initializer_list<QWidget*>{sEdgeL_, sEdgeR_, sEdgeT_, sEdgeB_, sGamma_, sPower_}) w->setEnabled(s->softEdge);
    loading_ = false;
}

void OutputsDialog::commitScreenForm() {
    Output* o = currentOutput();
    if (!o || currentSlice_ >= 0 || loading_) return;
    o->name = name_->text().trimmed().toStdString();
    o->enabled = enabled_->isChecked();
    o->type = static_cast<OutputType>(type_->currentData().toInt());
    if (o->type == OutputType::Display && display_->currentIndex() >= 0) {
        o->displayId = display_->currentData().toString().toStdString();
        for (const auto& d : displays_)
            if (d.id == o->displayId) o->monitor = d.index;
    }
    o->windowWidth = winW_->value();
    o->windowHeight = winH_->value();
    o->ndiName = ndiName_->text().trimmed().toStdString();
    o->ndiAlpha = ndiAlpha_->isChecked();
    o->width = scrW_->value();
    o->height = scrH_->value();
}

void OutputsDialog::commitSliceForm() {
    Slice* s = currentSliceRef();
    if (!s || loading_) return;
    const QString n = sName_->text().trimmed();
    if (!n.isEmpty()) s->name = n.toStdString();
    s->enabled = sEnabled_->isChecked();
    const QString key = sSource_->currentData().toString();
    if (key == QLatin1String("media")) {
        s->source = SliceSourceKind::MediaPlayer;
    } else if (key.startsWith(QLatin1String("comp:"))) {
        s->source = SliceSourceKind::Composition;
        s->compositionId = key.mid(5).toStdString();
        s->layerId.clear();
    } else if (key.startsWith(QLatin1String("layer:"))) {
        const QStringList parts = key.split(QLatin1Char(':'));
        if (parts.size() == 3) {
            s->source = SliceSourceKind::Layer;
            s->compositionId = parts[1].toStdString();
            s->layerId = parts[2].toStdString();
        }
    }
    s->inX = sInX_->value();
    s->inY = sInY_->value();
    s->inW = sInW_->value();
    s->inH = sInH_->value();
    s->x = sX_->value();
    s->y = sY_->value();
    s->width = sW_->value();
    s->height = sH_->value();
    s->rotation = sRot_->value();
    s->flipH = sFlipH_->isChecked();
    s->flipV = sFlipV_->isChecked();
    s->keepAspect = sAspect_->isChecked();
    const int cols = sMeshCols_->value(), rows = sMeshRows_->value();
    if (cols != s->meshCols || rows != s->meshRows) {
        s->meshCols = cols;
        s->meshRows = rows;
        s->mesh.clear();
        if (cols > 0 && rows > 0)
            for (int r = 0; r <= rows; ++r)
                for (int c = 0; c <= cols; ++c)
                    s->mesh.push_back({static_cast<double>(c) / cols, static_cast<double>(r) / rows});
    }
    s->brightness = sBright_->value() / 100.0;
    s->contrast = sContrast_->value() / 100.0;
    s->red = sRed_->value() / 100.0;
    s->green = sGreen_->value() / 100.0;
    s->blue = sBlue_->value() / 100.0;
    s->softEdge = sSoft_->isChecked();
    s->edgeLeft = sEdgeL_->value();
    s->edgeRight = sEdgeR_->value();
    s->edgeTop = sEdgeT_->value();
    s->edgeBottom = sEdgeB_->value();
    s->edgeGamma = sGamma_->value();
    s->edgePower = sPower_->value();
    s->clamp();
}

void OutputsDialog::changed() {
    if (loading_) return;
    commitScreenForm();
    commitSliceForm();
    rebuildTree();
    if (Output* o = currentOutput()) {
        pages_->setCurrentIndex(o->type == OutputType::Display ? 0 : o->type == OutputType::Window ? 1 : 2);
        screenCanvas_->update();
    }
    if (currentSliceRef()) loadSliceForm();
    emit liveChanged();
}

void OutputsDialog::sendLive() {
    const Output* o = currentOutput();
    if (!o || !engine_ || !engine_->isConnected()) return;
    protocol::Command c;
    c.type = protocol::CommandType::SetOutput;
    c.json = outputToJson(*o);
    engine_->sendLatest(QStringLiteral("output:") + qs(o->id), c);
}

void OutputsDialog::addOutput(OutputType type) {
    commitScreenForm();
    commitSliceForm();
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
    const QSize z = displaySize(o);
    o.width = z.isValid() ? z.width() : width_->value();
    o.height = z.isValid() ? z.height() : height_->value();
    Slice s = makeFullSlice(SliceSourceKind::MediaPlayer, o.width, o.height);
    s.keepAspect = true;
    o.slices.push_back(std::move(s));
    outputs_.push_back(std::move(o));
    current_ = static_cast<int>(outputs_.size()) - 1;
    currentSlice_ = -1;
    rebuildTree();
    showCurrent();
    emit liveChanged();
}

void OutputsDialog::addSlice() {
    commitScreenForm();
    commitSliceForm();
    Output* o = currentOutput();
    if (!o) return;
    if (o->slices.size() >= 64) return;
    std::string id;
    for (int i = 1;; ++i) {
        id = "S" + std::to_string(i);
        if (std::none_of(o->slices.begin(), o->slices.end(), [&](const Slice& x) { return x.id == id; })) break;
    }
    // Slice novo: metade do screen, ao centro, com a fonte do slice actual (se houver).
    Slice s = makeFullSlice(SliceSourceKind::MediaPlayer, o->width, o->height, id);
    s.name = tr("Slice %1").arg(o->slices.size() + 1).toStdString();
    s.width = o->width / 2.0;
    s.height = o->height / 2.0;
    if (const Slice* cur = currentSliceRef()) {
        s.source = cur->source;
        s.compositionId = cur->compositionId;
        s.layerId = cur->layerId;
    }
    o->slices.push_back(std::move(s));
    currentSlice_ = static_cast<int>(o->slices.size()) - 1;
    rebuildTree();
    showCurrent();
    emit liveChanged();
}

void OutputsDialog::duplicateCurrent() {
    commitScreenForm();
    commitSliceForm();
    Output* o = currentOutput();
    if (!o) return;
    if (const Slice* cur = currentSliceRef()) {
        if (o->slices.size() >= 64) return;
        Slice s = *cur;
        for (int i = 1;; ++i) {
            s.id = "S" + std::to_string(i);
            if (std::none_of(o->slices.begin(), o->slices.end(), [&](const Slice& x) { return x.id == s.id; })) break;
        }
        s.name += tr(" (copy)").toStdString();
        s.x += 40;
        s.y += 40;
        o->slices.push_back(std::move(s));
        currentSlice_ = static_cast<int>(o->slices.size()) - 1;
    } else {
        Output copy = *o;
        for (int i = 1;; ++i) {
            copy.id = "O" + std::to_string(i);
            if (std::none_of(outputs_.begin(), outputs_.end(), [&](const Output& x) { return x.id == copy.id; })) break;
        }
        copy.name += tr(" (copy)").toStdString();
        copy.type = OutputType::Window; // não ocupa outro ecrã por engano
        outputs_.push_back(std::move(copy));
        current_ = static_cast<int>(outputs_.size()) - 1;
    }
    rebuildTree();
    showCurrent();
    emit liveChanged();
}

void OutputsDialog::removeCurrent() {
    Output* o = currentOutput();
    if (!o) return;
    if (currentSliceRef()) {
        o->slices.erase(o->slices.begin() + currentSlice_);
        currentSlice_ = o->slices.empty() ? -1 : std::min(currentSlice_, static_cast<int>(o->slices.size()) - 1);
    } else {
        outputs_.erase(outputs_.begin() + current_);
        current_ = outputs_.empty() ? -1 : 0;
        currentSlice_ = -1;
    }
    rebuildTree();
    showCurrent();
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
    const Output* o = current_ >= 0 && current_ < static_cast<int>(outputs_.size()) ? &outputs_[static_cast<std::size_t>(current_)] : nullptr;
    if (!o) return;
    if (!engine_ || !engine_->hasStatus() || !engine_->isConnected()) {
        status_->setText(tr("Engine not running."));
        return;
    }
    for (const auto& os : engine_->lastStatus().outputs)
        if (os.id == o->id) {
            QString t = qs(os.detail);
            if (!os.error.empty()) t += (t.isEmpty() ? QString() : QStringLiteral("\n")) + QStringLiteral("⚠ ") + qs(os.error);
            status_->setText(t.isEmpty() ? (os.ok ? tr("OK") : tr("Not running")) : t);
            status_->setStyleSheet(os.ok && os.error.empty() ? QString() : QStringLiteral("color: #e66;"));
            return;
        }
    status_->setText(o->enabled ? tr("Waiting for the engine...") : tr("Off"));
}
