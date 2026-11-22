#include "ui/devices_dialog.h"

#include "app.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace vfx {

DevicesDialog::DevicesDialog(App& app, QWidget* parent) : QDialog(parent), app_(app) {
    setWindowTitle("Input devices");
    auto* lay = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Any key on these devices can be bound to a sound or action.<br>"
        "<b>Grab</b> a dedicated macro pad or spare keypad so its keys only reach voxfx "
        "and never type into other apps. <i>Don't grab your main keyboard.</i>");
    intro->setWordWrap(true);
    lay->addWidget(intro);

    perm_ = new QLabel;
    perm_->setWordWrap(true);
    perm_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(perm_);

    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({"Device", "Type", "Grab"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_);

    midi_ = new QLabel;
    midi_->setWordWrap(true);
    lay->addWidget(midi_);

    auto* row = new QHBoxLayout;
    auto* tone = new QCheckBox("Add a test-tone microphone (to check routing without talking)");
    row->addWidget(tone);
    row->addStretch();
    auto* close = new QPushButton("Close");
    row->addWidget(close);
    lay->addLayout(row);
    connect(tone, &QCheckBox::toggled, this, [this](bool on) { app_.engine().setTestTone(on); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto* timer = new QTimer(this); // pick up hot-plugged devices
    connect(timer, &QTimer::timeout, this, &DevicesDialog::refresh);
    timer->start(2000);
    refresh();
    resize(620, 420);
}

void DevicesDialog::refresh() {
    QString perm = QString::fromStdString(app_.evdev().permissionProblem());
    perm_->setText(perm.isEmpty() ? QString()
                                  : "⚠ " + perm + "<br><code>sudo usermod -aG input $USER</code>");
    perm_->setVisible(!perm.isEmpty());

    auto devs = app_.evdev().devices();
    std::sort(devs.begin(), devs.end(), [](auto& a, auto& b) { return a.name < b.name; });
    table_->setRowCount(int(devs.size()));
    const QStringList grabbed = app_.settings().grabbedDevices;
    for (int i = 0; i < int(devs.size()); ++i) {
        const auto& d = devs[size_t(i)];
        QString key = QString::fromStdString(d.key);
        table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(d.name)));
        table_->setItem(i, 1, new QTableWidgetItem(d.gamepad ? "Gamepad" : d.keyboard ? "Keys" : "Buttons"));
        auto* cb = new QCheckBox;
        cb->setChecked(grabbed.contains(key));
        cb->setToolTip(d.grabbed ? "Grabbed" : "Not grabbed");
        connect(cb, &QCheckBox::toggled, this, [this, key, name = d.name, kb = d.keyboard](bool on) {
            if (on && kb &&
                QMessageBox::warning(this, "Grab device",
                                     QString("Grab \"%1\"?\n\nIts keys will stop typing in every other app "
                                             "until you untick this. Only do this for a dedicated macro pad.")
                                         .arg(QString::fromStdString(name)),
                                     QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) {
                QMetaObject::invokeMethod(this, &DevicesDialog::refresh, Qt::QueuedConnection);
                return;
            }
            QStringList g = app_.settings().grabbedDevices;
            g.removeAll(key);
            if (on) g << key;
            app_.setGrabbedDevices(g);
        });
        auto* w = new QWidget;
        auto* l = new QHBoxLayout(w);
        l->setContentsMargins(8, 0, 8, 0);
        l->addWidget(cb);
        table_->setCellWidget(i, 2, w);
    }

    auto midi = app_.midi().connectedDevices();
    QStringList names;
    for (auto& m : midi) names << QString::fromStdString(m);
    if (!app_.midiError().isEmpty()) midi_->setText("MIDI: " + app_.midiError());
    else midi_->setText(names.isEmpty() ? "MIDI: no controllers connected (plug one in any time)."
                                        : "MIDI: " + names.join(", "));
}

} // namespace vfx
