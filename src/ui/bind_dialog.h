#pragma once
#include "input/binding_manager.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QTableWidget;

namespace vfx {

class App;

// "Press any key, pad or button…" learn dialog. Produces one Binding.
class BindDialog : public QDialog {
    Q_OBJECT
public:
    // soundId empty -> the dialog shows an action chooser (stop all, PTT, ...).
    BindDialog(App& app, const QString& soundId, const QString& soundTitle, QWidget* parent = nullptr);
    ~BindDialog() override;
    Binding result() const;

private:
    void captured(const InputEvent& ev);
    void updateConflict();

    App& app_;
    QString soundId_;
    InputEvent ev_;
    bool have_ = false;
    QLabel* prompt_;
    QLabel* conflict_;
    QCheckBox* thisDeviceOnly_;
    QComboBox* bank_;
    QComboBox* action_ = nullptr;
    QPushButton* ok_;
};

// Table of every binding with add/remove.
class BindingsDialog : public QDialog {
    Q_OBJECT
public:
    BindingsDialog(App& app, QWidget* parent = nullptr);

private:
    void refresh();
    App& app_;
    QTableWidget* table_;
};

} // namespace vfx
