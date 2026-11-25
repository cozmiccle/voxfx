#pragma once
#include <QMainWindow>
#include <QSystemTrayIcon>

#include <functional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QHBoxLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QToolButton;

namespace vfx {

class App;
class MeterWidget;
class PadGrid;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(App& app, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QWidget* buildControls();
    QWidget* buildBoard();
    void buildTray();
    void rebuildPads();
    void refreshMics();
    void refreshStatus();
    void syncFromParams();
    void tickMeters();
    void padMenu(const QString& id, const QPoint& globalPos);
    void tickPads();
    void arrangeTopBar(int width);
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
    PadGrid* grid_;
    QSlider* padSize_;
    QPushButton* record_;
    QPushButton* add_ = nullptr;
    QPushButton* stop_ = nullptr;
    QWidget* bankBox_ = nullptr;
    QWidget* viewBox_ = nullptr;
    QWidget* boardHost_ = nullptr;
    QHBoxLayout* topRow1_ = nullptr;
    QHBoxLayout* topRow2_ = nullptr;
    bool topTwoRows_ = false;

    QSystemTrayIcon* tray_ = nullptr;
    QAction* trayMute_ = nullptr;
    QAction* trayFx_ = nullptr;
    bool quitting_ = false;
};

} // namespace vfx
