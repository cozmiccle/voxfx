#pragma once
#include "common/types.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;

namespace vfx {

class App;

// Thumbnail + "Choose…" / "Remove"; also accepts a dropped image file.
class ImagePicker : public QWidget {
    Q_OBJECT
public:
    explicit ImagePicker(const QString& current, QWidget* parent = nullptr);
    QString path() const { return path_; }
    bool changed() const { return changed_; }

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void set(const QString& p);
    QLabel* thumb_;
    QPushButton* remove_;
    QString path_;
    bool changed_ = false;
};

// Small horizontal level meter fed from the engine meters.
class LevelBar : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
    void setLevel(float linearPeak);
    QSize sizeHint() const override { return {240, 10}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float db_ = -90.f;
};

// Record a new sound from the mic (processed voice), preview it, name it, save it.
class RecordDialog : public QDialog {
    Q_OBJECT
public:
    RecordDialog(App& app, QWidget* parent = nullptr);
    ~RecordDialog() override;

protected:
    void reject() override;

private:
    void toggle();
    void startTake();
    void stopTake();
    void tick();
    void save();
    void setState(const QString& text, bool error = false);

    App& app_;
    QLineEdit* name_;
    ImagePicker* image_;
    QPushButton* rec_;
    QPushButton* preview_;
    QPushButton* save_;
    QCheckBox* trim_;
    QLabel* time_;
    QLabel* state_;
    LevelBar* level_;
    QTimer* timer_;
    QString take_; // recorded file waiting to be saved
};

// Name, image, volume and play mode of an existing sound.
class SoundDialog : public QDialog {
    Q_OBJECT
public:
    SoundDialog(App& app, const QString& id, QWidget* parent = nullptr);
    void accept() override;

private:
    App& app_;
    QString id_;
    QLineEdit* name_;
    ImagePicker* image_;
    QSlider* trim_;
    QComboBox* mode_;
};

} // namespace vfx
