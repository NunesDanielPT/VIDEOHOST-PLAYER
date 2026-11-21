#include "mapping_editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace vthost;

namespace {
const QColor kOrange(255, 136, 0);
constexpr double kHandleRadius = 9.0;
} // namespace

// ------------------------------------------------------------------ MappingCanvas (sem textos)

MappingCanvas::MappingCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(560, 340);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void MappingCanvas::setMapping(const OutputMapping& m, int outW, int outH) {
    m_ = m;
    m_.sanitize();
    outW_ = std::max(16, outW);
    outH_ = std::max(16, outH);
    update();
}

QRectF MappingCanvas::area() const {
    // A saída ocupa o centro; à volta há margem para pontos arrastados para fora.
    const QRectF r = QRectF(rect()).adjusted(50, 40, -50, -40);
    const double s = std::min(r.width() / outW_, r.height() / outH_);
    const double w = outW_ * s, h = outH_ * s;
    return {r.center().x() - w / 2, r.center().y() - h / 2, w, h};
}

QPointF MappingCanvas::toScreen(const WarpPoint& p) const {
    const QRectF a = area();
    return {a.left() + p.x * a.width(), a.top() + p.y * a.height()};
}

WarpPoint MappingCanvas::fromScreen(const QPointF& p) const {
    const QRectF a = area();
    return {(p.x() - a.left()) / a.width(), (p.y() - a.top()) / a.height()};
}

WarpPoint MappingCanvas::handlePos(int handle) const {
    if (handle >= 0 && handle < 4) return m_.corners[static_cast<std::size_t>(handle)];
    if (handle >= 100) {
        const int i = handle - 100;
        const int c = i % (m_.meshCols + 1), r = i / (m_.meshCols + 1);
        return warpPoint(m_, static_cast<double>(c) / m_.meshCols, static_cast<double>(r) / m_.meshRows);
    }
    return {};
}

int MappingCanvas::hitTest(const QPointF& pos) const {
    if (m_.mode == WarpMode::None) return -1;
    if (m_.mode == WarpMode::Mesh) {
        for (int i = 0; i < static_cast<int>(m_.mesh.size()); ++i) {
            const int c = i % (m_.meshCols + 1), r = i / (m_.meshCols + 1);
            const bool isCorner = (c == 0 || c == m_.meshCols) && (r == 0 || r == m_.meshRows);
            if (isCorner) continue; // os cantos da malha coincidem com os da perspetiva
            if (QLineF(toScreen(handlePos(100 + i)), pos).length() <= kHandleRadius) return 100 + i;
        }
    }
    for (int k = 0; k < 4; ++k)
        if (QLineF(toScreen(m_.corners[static_cast<std::size_t>(k)]), pos).length() <= kHandleRadius + 3) return k;
    return -1;
}

void MappingCanvas::moveHandle(int handle, const WarpPoint& to) {
    if (handle >= 0 && handle < 4) {
        m_.corners[static_cast<std::size_t>(handle)] = to;
    } else if (handle >= 100 && handle - 100 < static_cast<int>(m_.mesh.size())) {
        WarpPoint meshSpace;
        if (unwarpPerspective(m_, to, meshSpace)) m_.mesh[static_cast<std::size_t>(handle - 100)] = meshSpace;
    }
    m_.sanitize();
    update();
    emit edited();
}

void MappingCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(34, 34, 34));
    const QRectF a = area();
    // Moldura da saída (o ecrã/projetor real).
    p.setPen(QPen(QColor(90, 90, 90), 1, Qt::DashLine));
    p.drawRect(a);

    // Grelha deformada: a imagem da região do canvas depois do warp.
    const int lines = 16, steps = 48;
    auto polyline = [&](bool alongU, double fixed) {
        QPolygonF poly;
        for (int s = 0; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            poly << toScreen(alongU ? warpPoint(m_, t, fixed) : warpPoint(m_, fixed, t));
        }
        return poly;
    };
    p.setPen(QPen(QColor(255, 136, 0, 110), 1));
    for (int i = 1; i < lines; ++i) {
        p.drawPolyline(polyline(true, static_cast<double>(i) / lines));
        p.drawPolyline(polyline(false, static_cast<double>(i) / lines));
    }
    p.setPen(QPen(kOrange, 2));
    p.drawPolyline(polyline(true, 0.0));
    p.drawPolyline(polyline(true, 1.0));
    p.drawPolyline(polyline(false, 0.0));
    p.drawPolyline(polyline(false, 1.0));

    // Soft-edge: zonas sombreadas na grelha (antes do warp, aproximado).
    auto shade = [&](double u0, double u1, double v0, double v1) {
        QPolygonF poly;
        poly << toScreen(warpPoint(m_, u0, v0)) << toScreen(warpPoint(m_, u1, v0)) << toScreen(warpPoint(m_, u1, v1))
             << toScreen(warpPoint(m_, u0, v1));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(80, 160, 255, 50));
        p.drawPolygon(poly);
    };
    if (m_.blendLeft > 0) shade(0, m_.blendLeft / outW_, 0, 1);
    if (m_.blendRight > 0) shade(1 - m_.blendRight / outW_, 1, 0, 1);
    if (m_.blendTop > 0) shade(0, 1, 0, m_.blendTop / outH_);
    if (m_.blendBottom > 0) shade(0, 1, 1 - m_.blendBottom / outH_, 1);

    if (m_.mode == WarpMode::None) return;
    // Pontos da malha.
    if (m_.mode == WarpMode::Mesh) {
        for (int i = 0; i < static_cast<int>(m_.mesh.size()); ++i) {
            const int c = i % (m_.meshCols + 1), r = i / (m_.meshCols + 1);
            if ((c == 0 || c == m_.meshCols) && (r == 0 || r == m_.meshRows)) continue;
            const QPointF pt = toScreen(handlePos(100 + i));
            p.setPen(QPen(Qt::black, 1));
            p.setBrush(selected_ == 100 + i ? kOrange : QColor(240, 240, 240));
            p.drawEllipse(pt, 5.5, 5.5);
        }
    }
    // Cantos.
    for (int k = 0; k < 4; ++k) {
        const QPointF pt = toScreen(m_.corners[static_cast<std::size_t>(k)]);
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(selected_ == k ? QColor(255, 255, 255) : kOrange);
        p.drawRect(QRectF(pt.x() - 7, pt.y() - 7, 14, 14));
    }
}

void MappingCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    selected_ = hitTest(e->position());
    dragging_ = selected_ >= 0 && e->button() == Qt::LeftButton;
    update();
}

void MappingCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) {
        setCursor(hitTest(e->position()) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
    moveHandle(selected_, fromScreen(e->position()));
}

void MappingCanvas::mouseReleaseEvent(QMouseEvent*) { dragging_ = false; }

void MappingCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Tab) {
        // Percorre cantos e pontos.
        std::vector<int> handles = {0, 1, 2, 3};
        if (m_.mode == WarpMode::Mesh)
            for (int i = 0; i < static_cast<int>(m_.mesh.size()); ++i) {
                const int c = i % (m_.meshCols + 1), r = i / (m_.meshCols + 1);
                if (!((c == 0 || c == m_.meshCols) && (r == 0 || r == m_.meshRows))) handles.push_back(100 + i);
            }
        auto it = std::find(handles.begin(), handles.end(), selected_);
        selected_ = (it == handles.end() || std::next(it) == handles.end()) ? handles.front() : *std::next(it);
        update();
        return;
    }
    if (selected_ < 0 || m_.mode == WarpMode::None) {
        QWidget::keyPressEvent(e);
        return;
    }
    const double stepPx = (e->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
    double dx = 0, dy = 0;
    switch (e->key()) {
    case Qt::Key_Left: dx = -stepPx; break;
    case Qt::Key_Right: dx = stepPx; break;
    case Qt::Key_Up: dy = -stepPx; break;
    case Qt::Key_Down: dy = stepPx; break;
    default: QWidget::keyPressEvent(e); return;
    }
    WarpPoint pos = handlePos(selected_);
    pos.x += dx / outW_;
    pos.y += dy / outH_;
    moveHandle(selected_, pos);
}

// ------------------------------------------------------------------ MappingEditor

MappingEditor::MappingEditor(const QString& outputName, const OutputMapping& mapping, int outW, int outH, LiveFn live,
                             bool testCard, std::function<void(bool)> setTestCard, QWidget* parent)
    : QDialog(parent), live_(std::move(live)) {
    setWindowTitle(tr("Mapping — %1").arg(outputName));
    resize(980, 680);

    canvas_ = new MappingCanvas(this);
    OutputMapping m = mapping;
    m.sanitize();
    canvas_->setMapping(m, outW, outH);

    mode_ = new QComboBox(this);
    mode_->addItem(tr("No warp"), static_cast<int>(WarpMode::None));
    mode_->addItem(tr("Perspective (4 corners)"), static_cast<int>(WarpMode::Perspective));
    mode_->addItem(tr("Warping grid (corners + points)"), static_cast<int>(WarpMode::Mesh));
    cols_ = new QSpinBox(this);
    cols_->setRange(1, kMaxMeshDivisions);
    rows_ = new QSpinBox(this);
    rows_->setRange(1, kMaxMeshDivisions);
    auto* reset = new QPushButton(tr("Reset"), this);
    auto* test = new QCheckBox(tr("Display test card"), this);
    test->setChecked(testCard);
    hint_ = new QLabel(tr("Drag the corners and points. Arrows move the selected point by 1 pixel (Shift: 10), "
                          "Tab selects the next point. Changes are shown live on the output."),
                       this);
    hint_->setWordWrap(true);
    hint_->setStyleSheet(QStringLiteral("color: gray;"));

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Warp:"), this));
    bar->addWidget(mode_);
    bar->addSpacing(12);
    bar->addWidget(new QLabel(tr("Grid:"), this));
    bar->addWidget(cols_);
    bar->addWidget(new QLabel(QStringLiteral("×"), this));
    bar->addWidget(rows_);
    bar->addSpacing(12);
    bar->addWidget(reset);
    bar->addStretch();
    bar->addWidget(test);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(canvas_, 1);
    layout->addWidget(hint_);
    layout->addWidget(buttons);

    syncControls();

    connect(canvas_, &MappingCanvas::edited, this, &MappingEditor::sendLive);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this, outW, outH] {
        if (loading_) return;
        OutputMapping mm = canvas_->mapping();
        mm.mode = static_cast<WarpMode>(mode_->currentData().toInt());
        canvas_->setMapping(mm, outW, outH);
        syncControls();
        sendLive();
    });
    auto regrid = [this, outW, outH] {
        if (loading_) return;
        OutputMapping mm = canvas_->mapping();
        mm.meshCols = cols_->value();
        mm.meshRows = rows_->value();
        mm.resetMesh(); // nova grelha regular (os cantos mantêm-se)
        canvas_->setMapping(mm, outW, outH);
        sendLive();
    };
    connect(cols_, &QSpinBox::valueChanged, this, regrid);
    connect(rows_, &QSpinBox::valueChanged, this, regrid);
    connect(reset, &QPushButton::clicked, this, [this, outW, outH] {
        if (QMessageBox::question(this, tr("Reset"), tr("Remove all warping from this output?")) != QMessageBox::Yes)
            return;
        OutputMapping mm = canvas_->mapping();
        mm.corners = {{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        mm.resetMesh();
        canvas_->setMapping(mm, outW, outH);
        sendLive();
    });
    connect(test, &QCheckBox::toggled, this, [setTestCard](bool on) {
        if (setTestCard) setTestCard(on);
    });
}

void MappingEditor::syncControls() {
    loading_ = true;
    const OutputMapping& m = canvas_->mapping();
    mode_->setCurrentIndex(std::max(0, mode_->findData(static_cast<int>(m.mode))));
    cols_->setValue(m.meshCols);
    rows_->setValue(m.meshRows);
    cols_->setEnabled(m.mode == WarpMode::Mesh);
    rows_->setEnabled(m.mode == WarpMode::Mesh);
    loading_ = false;
}

void MappingEditor::sendLive() {
    if (live_) live_(canvas_->mapping());
}
