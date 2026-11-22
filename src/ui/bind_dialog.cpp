#include "ui/bind_dialog.h"

#include "app.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace vfx {

namespace {
QString actionLabel(Action a) {
    switch (a) {
        case Action::PlaySound: return "Play sound";
        case Action::StopAll: return "Stop all sounds";
        case Action::PushToTalk: return "Push-to-talk (hold)";
        case Action::ToggleMute: return "Mute / unmute mic";
        case Action::ToggleFx: return "Voice effects on/off";
        case Action::NextPreset: return "Next voice preset";
        case Action::BankNext: return "Next bank";
        case Action::BankPrev: return "Previous bank";
    }
    return "?";
}
} // namespace

BindDialog::BindDialog(App& app, const QString& soundId, const QString& soundTitle, QWidget* parent)
    : QDialog(parent), app_(app), soundId_(soundId) {
    setWindowTitle(soundId.isEmpty() ? "New binding" : "Bind \"" + soundTitle + "\"");
    auto* lay = new QVBoxLayout(this);

    prompt_ = new QLabel("<b>Press a key, macro-pad key, MIDI pad or gamepad button…</b><br>"
                         "<small>Hold Ctrl/Shift/Alt for a keyboard combo. Esc cancels.</small>");
    prompt_->setWordWrap(true);
    prompt_->setMinimumHeight(70);
    lay->addWidget(prompt_);

    QString perm = QString::fromStdString(app.evdev().permissionProblem());
    if (!perm.isEmpty()) {
        auto* warn = new QLabel("⚠ " + perm + " Keyboards, macro pads and gamepads won't be detected.");
        warn->setWordWrap(true);
        lay->addWidget(warn);
    }

    auto* form = new QFormLayout;
    if (soundId.isEmpty()) {
        action_ = new QComboBox;
        for (Action a : {Action::StopAll, Action::PushToTalk, Action::ToggleMute, Action::ToggleFx,
                         Action::NextPreset, Action::BankNext, Action::BankPrev})
            action_->addItem(actionLabel(a), int(a));
        form->addRow("Action", action_);
    }
    thisDeviceOnly_ = new QCheckBox("Only on the device I pressed");
    thisDeviceOnly_->setChecked(true);
    thisDeviceOnly_->setToolTip("Untick to react to this key on any keyboard / pad.");
    form->addRow("", thisDeviceOnly_);
    bank_ = new QComboBox;
    bank_->addItem("All banks", -1);
    for (int i = 0; i < BindingManager::kNumBanks; ++i) bank_->addItem(QString("Bank %1").arg(i + 1), i);
    form->addRow("Bank", bank_);
    lay->addLayout(form);

    conflict_ = new QLabel;
    conflict_->setWordWrap(true);
    lay->addWidget(conflict_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setEnabled(false);
    auto* again = buttons->addButton("Capture again", QDialogButtonBox::ResetRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(again, &QPushButton::clicked, this, [this] {
        have_ = false;
        ok_->setEnabled(false);
        prompt_->setText("<b>Press a key, pad or button…</b>");
        conflict_->clear();
        app_.bindings().startLearn([this](const InputEvent& e) {
            QMetaObject::invokeMethod(this, [this, e] { captured(e); }, Qt::QueuedConnection);
        });
    });
    connect(thisDeviceOnly_, &QCheckBox::toggled, this, &BindDialog::updateConflict);
    connect(bank_, &QComboBox::currentIndexChanged, this, &BindDialog::updateConflict);

    // The callback runs on an input thread; hop to the GUI thread.
    app_.bindings().startLearn([this](const InputEvent& e) {
        QMetaObject::invokeMethod(this, [this, e] { captured(e); }, Qt::QueuedConnection);
    });
    resize(420, sizeHint().height());
}

BindDialog::~BindDialog() { app_.bindings().cancelLearn(); }

void BindDialog::captured(const InputEvent& ev) {
    if (ev.control == "key:1") { reject(); return; } // KEY_ESC
    ev_ = ev;
    have_ = true;
    ok_->setEnabled(true);
    prompt_->setText(QString("Captured: <b>%1</b><br><small>%2</small>")
                         .arg(QString::fromStdString(modsToString(ev.mods) + ev.label).toHtmlEscaped(),
                              QString::fromStdString(ev.deviceLabel).toHtmlEscaped()));
    updateConflict();
}

Binding BindDialog::result() const {
    Binding b;
    b.device = thisDeviceOnly_->isChecked() ? ev_.device : "*";
    b.deviceLabel = ev_.deviceLabel;
    b.control = ev_.control;
    b.controlLabel = ev_.label;
    b.mods = ev_.mods;
    b.bank = bank_->currentData().toInt();
    b.action = action_ ? Action(action_->currentData().toInt()) : Action::PlaySound;
    b.soundId = soundId_.toStdString();
    return b;
}

void BindDialog::updateConflict() {
    if (!have_) return;
    Binding other;
    Binding mine = result();
    if (app_.bindings().findConflict(mine, &other) &&
        !(other.action == mine.action && other.soundId == mine.soundId)) {
        QString what = other.action == Action::PlaySound
            ? [&] {
                  const SoundEntry* e = app_.library().find(QString::fromStdString(other.soundId));
                  return e ? "\"" + e->title + "\"" : QString("a sound");
              }()
            : actionLabel(other.action);
        conflict_->setText(other.bank == mine.bank
            ? "⚠ Already bound to " + what + " — it will be replaced."
            : "ℹ Also bound to " + what + " in another bank; the bank-specific binding wins.");
    } else {
        conflict_->clear();
    }
}

// ============================================================================

BindingsDialog::BindingsDialog(App& app, QWidget* parent) : QDialog(parent), app_(app) {
    setWindowTitle("Bindings");
    auto* lay = new QVBoxLayout(this);
    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({"Trigger", "Bank", "Does"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("Add action binding…");
    add->setToolTip("Bind stop-all, push-to-talk, mute, banks… Sounds are bound from their pad's menu.");
    auto* remove = new QPushButton("Remove selected");
    auto* close = new QPushButton("Close");
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch();
    row->addWidget(close);
    lay->addLayout(row);

    connect(add, &QPushButton::clicked, this, [this] {
        BindDialog d(app_, QString(), QString(), this);
        if (d.exec() == QDialog::Accepted) {
            app_.bindings().add(d.result());
            app_.saveLibrary();
            emit app_.bindingsChanged();
            refresh();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        auto rows = table_->selectionModel()->selectedRows();
        std::vector<int> idx;
        for (auto& r : rows) idx.push_back(r.row());
        std::sort(idx.rbegin(), idx.rend());
        for (int i : idx) app_.bindings().removeAt(size_t(i));
        app_.saveLibrary();
        emit app_.bindingsChanged();
        refresh();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    refresh();
    resize(640, 400);
}

void BindingsDialog::refresh() {
    auto all = app_.bindings().bindings();
    table_->setRowCount(int(all.size()));
    for (int i = 0; i < int(all.size()); ++i) {
        const Binding& b = all[size_t(i)];
        QString does = actionLabel(b.action);
        if (b.action == Action::PlaySound) {
            const SoundEntry* e = app_.library().find(QString::fromStdString(b.soundId));
            does = "Play \"" + (e ? e->title : QString("missing sound")) + "\"";
        }
        table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(b.describe())));
        table_->setItem(i, 1, new QTableWidgetItem(b.bank < 0 ? "All" : QString::number(b.bank + 1)));
        table_->setItem(i, 2, new QTableWidgetItem(does));
    }
}

} // namespace vfx
