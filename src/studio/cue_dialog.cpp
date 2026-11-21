#include "cue_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <algorithm>

using namespace vthost;

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QString& s) { return s.toStdString(); }
} // namespace

CueDialog::CueDialog(const Project& project, int column, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Cue settings"));
    const Column empty;
    const bool valid = column >= 0 && column < static_cast<int>(project.columns.size());
    const Column& col = valid ? project.columns[static_cast<std::size_t>(column)] : empty;

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    name_ = new QLineEdit(qs(col.name), this);
    form->addRow(tr("Name:"), name_);

    transition_ = new QComboBox(this);
    transition_->addItem(tr("Dissolve (crossfade)"), static_cast<int>(TransitionType::Crossfade));
    transition_->addItem(tr("Fade through black"), static_cast<int>(TransitionType::FadeThroughBlack));
    transition_->addItem(tr("Cut"), static_cast<int>(TransitionType::Cut));
    transition_->setCurrentIndex(std::max(0, transition_->findData(static_cast<int>(col.transition))));
    form->addRow(tr("Transition into this cue:"), transition_);

    duration_ = new QDoubleSpinBox(this);
    duration_->setRange(0.0, 60.0);
    duration_->setDecimals(2);
    duration_->setSingleStep(0.25);
    duration_->setSuffix(tr(" s"));
    duration_->setValue(col.fadeSeconds);
    form->addRow(tr("Transition duration:"), duration_);

    follow_ = new QComboBox(this);
    follow_->addItem(tr("No (wait for GO)"), static_cast<int>(FollowMode::None));
    follow_->addItem(tr("When the videos of this cue end"), static_cast<int>(FollowMode::MediaEnd));
    follow_->addItem(tr("After a fixed time"), static_cast<int>(FollowMode::Timer));
    follow_->setCurrentIndex(std::max(0, follow_->findData(static_cast<int>(col.follow))));
    form->addRow(tr("Go to the next cue automatically:"), follow_);

    followSeconds_ = new QDoubleSpinBox(this);
    followSeconds_->setRange(0.1, 86400.0);
    followSeconds_->setDecimals(1);
    followSeconds_->setSuffix(tr(" s"));
    followSeconds_->setValue(col.followSeconds);
    form->addRow(tr("Time:"), followSeconds_);

    jumpTo_ = new QComboBox(this);
    jumpTo_->addItem(tr("Next cue in the list"), QString());
    for (int i = 0; i < static_cast<int>(project.columns.size()); ++i) {
        const Column& c = project.columns[static_cast<std::size_t>(i)];
        QString label = QStringLiteral("%1  %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(qs(c.name));
        if (i == column) label += QStringLiteral("  (") + tr("repeat this cue") + QStringLiteral(")");
        jumpTo_->addItem(label, qs(c.id));
    }
    jumpTo_->setCurrentIndex(std::max(0, jumpTo_->findData(qs(col.jumpTo))));
    form->addRow(tr("After this cue, go to:"), jumpTo_);

    auto* hint = new QLabel(tr("\"After this cue, go to\" is used by GO next (Space) and by the automatic follow. "
                               "Example: at the last cue choose cue 03 to loop back to it. "
                               "With \"When the videos end\" the videos of this cue play once."),
                            this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(transition_, &QComboBox::currentIndexChanged, this, [this](int) { updateEnabled(); });
    connect(follow_, &QComboBox::currentIndexChanged, this, [this](int) { updateEnabled(); });
    updateEnabled();
    setMinimumWidth(460);
}

void CueDialog::updateEnabled() {
    duration_->setEnabled(transition_->currentData().toInt() != static_cast<int>(TransitionType::Cut));
    followSeconds_->setEnabled(follow_->currentData().toInt() == static_cast<int>(FollowMode::Timer));
}

void CueDialog::applyTo(Column& column) const {
    const QString name = name_->text().trimmed();
    if (!name.isEmpty()) column.name = ss(name);
    column.transition = static_cast<TransitionType>(transition_->currentData().toInt());
    column.fadeSeconds = duration_->value();
    column.follow = static_cast<FollowMode>(follow_->currentData().toInt());
    column.followSeconds = followSeconds_->value();
    column.jumpTo = ss(jumpTo_->currentData().toString());
}
