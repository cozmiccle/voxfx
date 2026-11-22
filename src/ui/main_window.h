#pragma once
#include <QMainWindow>
#include <QSystemTrayIcon>

#include <functional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QToolButton;

namespace vfx {

class App;
class MeterWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(App& app, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QWidget* buildControls();
    QWidget* buildBoard();
    void buildTray();
    void rebuildPads();
    void refreshMics();
    void refreshStatus();
    void syncFromParams();
    void tickMeters();
    void padMenu(const QString& id, QToolButton* pad, const QPoint& pos);
    void bindSound(const QString& id);
    void calibrateGate();
    void importLinkDialog();
    QSlider* dbSlider(int min, int max, float value, QLabel* readout, std::function<void(float)> set);

    App& app_;
    QComboBox* mic_;
    QLabel* micStatus_;
    QSlider* micGain_; QLabel* micGainVal_;
    QPushButton* mute_;
    QCheckBox* ptt_;
    QGroupBox* cleanup_;
    QSlider* nsMix_;
    QSlider* gate_; QLabel* gateVal_;
    QPushButton* calibrate_;
    QCheckBox* comp_;
    QCheckBox* limiter_;
    QGroupBox* voice_;
    QComboBox* preset_;
    QSlider* sfxGain_; QLabel* sfxGainVal_;
    QSlider* duck_; QLabel* duckVal_;
    QGroupBox* monitor_;
    QSlider* monGain_; QLabel* monGainVal_;
    QCheckBox* monMic_;
    MeterWidget* meters_;

    QLineEdit* search_;
    QLabel* bankLabel_;
    QWidget* padHost_;
    QGridLayout* padGrid_;

    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayMute_ = nullptr;
    QAction* trayFx_ = nullptr;
    bool quitting_ = false;
};

} // namespace vfx
