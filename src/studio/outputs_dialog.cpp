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

OutputsDialog::OutputsDialog(const std::vector<Output>& outputs, EngineClient* engine, QWidget* parent)
    : QDialog(parent), engine_(engine), outputs_(outputs) {
    setWindowTitle(tr("Outputs"));
    resize(860, 480);

    // ---- lista à esquerda
    list_ = new QListWidget(this);
    list_->setMinimumWidth(260);
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
    auto* leftButtons = new QHBoxLayout;
    leftButtons->addWidget(add);
    leftButtons->addWidget(remove);
    auto* left = new QVBoxLayout;
    left->addWidget(list_, 1);
    left->addLayout(leftButtons);

    // ---- formulário à direita
    name_ = new QLineEdit(this);
    type_ = new QComboBox(this);
    type_->addItem(tr("Display (fullscreen)"), static_cast<int>(OutputType::Display));
    type_->addItem(tr("Window"), static_cast<int>(OutputType::Window));
    type_->addItem(tr("NDI"), static_cast<int>(OutputType::Ndi));
    type_->addItem(tr("Blackmagic (SDI / HDMI)"), static_cast<int>(OutputType::DeckLink));
    enabled_ = new QCheckBox(tr("Enabled"), this);

    pages_ = new QStackedWidget(this);
    // Ecrã
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
    fd->addStretch();
    // Janela
    auto* pWindow = new QWidget(pages_);
    auto* fw = new QFormLayout(pWindow);
    winW_ = new QSpinBox(pWindow);
    winW_->setRange(160, kMaxCanvasSize);
    winH_ = new QSpinBox(pWindow);
    winH_->setRange(90, kMaxCanvasSize);
    fw->addRow(tr("Width (px):"), winW_);
    fw->addRow(tr("Height (px):"), winH_);
    // NDI
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
    // Blackmagic
    auto* pDeck = new QWidget(pages_);
    auto* fdk = new QVBoxLayout(pDeck);
    auto* deckNote = new QLabel(
        tr("Blackmagic DeckLink / UltraStudio / Intensity output is in development. "
           "It will need Blackmagic Desktop Video installed. The output is saved in the project and will start "
           "working when support arrives in an update."),
        pDeck);
    deckNote->setWordWrap(true);
    fdk->addWidget(deckNote);
    fdk->addStretch();
    pages_->addWidget(pDisplay);
    pages_->addWidget(pWindow);
    pages_->addWidget(pNdi);
    pages_->addWidget(pDeck);

    status_ = new QLabel(this);
    status_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), name_);
    form->addRow(tr("Type:"), type_);
    form->addRow(QString(), enabled_);
    form->addRow(tr("Destination:"), pages_);
    form->addRow(tr("Status:"), status_);

    auto* top = new QHBoxLayout;
    top->addLayout(left);
    top->addLayout(form, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        commitForm();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(buttons);

    // Qualquer alteração no formulário fica logo guardada na saída selecionada.
    auto commit = [this] {
        if (!loading_) {
            commitForm();
            rebuildList();
        }
    };
    connect(name_, &QLineEdit::editingFinished, this, commit);
    connect(type_, &QComboBox::currentIndexChanged, this, [this, commit] {
        pages_->setCurrentIndex(type_->currentIndex());
        commit();
    });
    connect(enabled_, &QCheckBox::toggled, this, commit);
    connect(display_, &QComboBox::currentIndexChanged, this, commit);
    connect(winW_, &QSpinBox::valueChanged, this, commit);
    connect(winH_, &QSpinBox::valueChanged, this, commit);
    connect(ndiName_, &QLineEdit::editingFinished, this, commit);
    connect(ndiAlpha_, &QCheckBox::toggled, this, commit);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!loading_) commitForm();
        showOutput(row);
    });

    auto* statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &OutputsDialog::updateStatus);
    statusTimer->start(1000);

    setDisplays(qtDisplays(), false);
    refreshDisplays();
    rebuildList();
    if (!outputs_.empty()) list_->setCurrentRow(0);
    else showOutput(-1);
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
    showOutput(current_);
}

void OutputsDialog::rebuildList() {
    const bool wasLoading = loading_;
    loading_ = true;
    const int row = current_;
    list_->clear();
    for (const auto& o : outputs_) {
        QString type;
        switch (o.type) {
        case OutputType::Display: type = tr("Display"); break;
        case OutputType::Window: type = tr("Window"); break;
        case OutputType::Ndi: type = QStringLiteral("NDI"); break;
        case OutputType::DeckLink: type = QStringLiteral("Blackmagic"); break;
        }
        auto* item = new QListWidgetItem(QStringLiteral("%1  ·  %2").arg(qs(o.name), type), list_);
        if (!o.enabled) item->setForeground(QColor(130, 130, 130));
    }
    if (row >= 0 && row < list_->count()) list_->setCurrentRow(row);
    loading_ = wasLoading;
}

void OutputsDialog::showOutput(int index) {
    loading_ = true;
    current_ = (index >= 0 && index < static_cast<int>(outputs_.size())) ? index : -1;
    const bool valid = current_ >= 0;
    for (QWidget* w : {static_cast<QWidget*>(name_), static_cast<QWidget*>(type_), static_cast<QWidget*>(enabled_),
                       static_cast<QWidget*>(pages_)})
        w->setEnabled(valid);
    if (!valid) {
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

    display_->clear();
    int sel = -1;
    for (const auto& d : displays_) {
        display_->addItem(displayLabel(d), qs(d.id));
        if (!o.displayId.empty() && (d.id == o.displayId || d.name == o.displayId)) sel = display_->count() - 1;
    }
    if (o.displayId.empty()) {
        // Projeto antigo: só o número do ecrã.
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

    // Aviso: ecrã principal (onde está a interface) — como no Millumin.
    displayWarning_->clear();
    for (const auto& d : displays_)
        if (o.type == OutputType::Display && d.primary && d.id == o.displayId)
            displayWarning_->setText(tr("Warning: this is the main screen, where VTHOST Studio is open. "
                                        "The output will cover the interface."));
    loading_ = false;
    updateStatus();
}

void OutputsDialog::commitForm() {
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size())) return;
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
    o.windowWidth = winW_->value();
    o.windowHeight = winH_->value();
    o.ndiName = ndiName_->text().trimmed().toStdString();
    o.ndiAlpha = ndiAlpha_->isChecked();
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
    rebuildList();
    list_->setCurrentRow(current_);
}

void OutputsDialog::removeOutput() {
    if (current_ < 0 || current_ >= static_cast<int>(outputs_.size())) return;
    outputs_.erase(outputs_.begin() + current_);
    current_ = std::min(current_, static_cast<int>(outputs_.size()) - 1);
    rebuildList();
    showOutput(current_);
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
    status_->setText(tr("Not applied yet (press OK)."));
    status_->setStyleSheet(QStringLiteral("color: gray;"));
}
