#pragma once
#include <QDialog>

class QTableWidget;
class QLabel;

namespace vfx {

class App;

// Lists input devices (macro pads, keyboards, gamepads, MIDI) and lets you
// "grab" a device so its keys only reach voxfx.
class DevicesDialog : public QDialog {
    Q_OBJECT
public:
    DevicesDialog(App& app, QWidget* parent = nullptr);

private:
    void refresh();
    App& app_;
    QTableWidget* table_;
    QLabel* midi_;
    QLabel* perm_;
};

} // namespace vfx
