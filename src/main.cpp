// voxfx: PipeWire voice changer + soundboard.
//
//   voxfx                 start the app (or show the running one)
//   voxfx ctl <command>   control the running app, e.g. from a compositor key binding:
//                         play <name>, stop, mute, unmute, toggle-mute, ptt-down,
//                         ptt-up, preset <name>, bank <n>, list, show
#include "app.h"
#include "ui/main_window.h"
#include "ui/sound_dialogs.h"
#include "ui/theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include <cstdio>

namespace {

int runCtl(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QStringList words;
    for (int i = 2; i < argc; ++i) words << QString::fromLocal8Bit(argv[i]);
    if (words.isEmpty()) {
        fprintf(stderr, "usage: voxfx ctl play <name> | stop | mute | unmute | toggle-mute | "
                        "ptt-down | ptt-up | preset <name> | bank <n> | list | show\n");
        return 2;
    }
    QLocalSocket s;
    s.connectToServer(vfx::App::kCtlSocket);
    if (!s.waitForConnected(1000)) { fprintf(stderr, "voxfx is not running\n"); return 1; }
    s.write((words.join(' ') + "\n").toUtf8());
    s.flush();
    QByteArray reply;
    while (s.waitForReadyRead(1500)) {
        reply += s.readAll();
        if (reply.endsWith('\n')) break;
    }
    fputs(reply.constData(), stdout);
    return reply.startsWith("error") ? 1 : 0;
}

bool alreadyRunning() {
    QLocalSocket s;
    s.connectToServer(vfx::App::kCtlSocket);
    if (!s.waitForConnected(300)) return false;
    s.write("show\n");
    s.flush();
    s.waitForReadyRead(500);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && QByteArray(argv[1]) == "ctl") return runCtl(argc, argv);

    QApplication qapp(argc, argv);
    QApplication::setApplicationName("voxfx");
    QApplication::setOrganizationName("voxfx");
    QApplication::setDesktopFileName("voxfx");
    QApplication::setQuitOnLastWindowClosed(false); // keeps running in the tray

    if (alreadyRunning()) return 0;

    // Dark by default; "light" or "system" can be picked in the window.
    vfx::theme::apply(QSettings("voxfx", "voxfx").value("ui/theme", "dark").toString());

    vfx::App app;
    QString err;
    if (!app.start(&err)) {
        QMessageBox::critical(nullptr, "voxfx",
                              "Could not start the audio engine:\n" + err +
                                  "\n\nvoxfx needs PipeWire (most current distros use it by default).");
        return 1;
    }
    vfx::MainWindow w(app);
    if (!app.settings().startMinimized || !QSystemTrayIcon::isSystemTrayAvailable()) w.show();
    QObject::connect(&qapp, &QApplication::aboutToQuit, &app, &vfx::App::shutdown);

    // Dev aid: with VOXFX_DEV set, `voxfx ctl dev-screenshot <file>` saves the window;
    // "record:<file>" or "edit:<sound id>:<file>" capture those dialogs instead.
    QObject::connect(&app, &vfx::App::devScreenshotRequested, &w, [&w, &app](const QString& arg) {
        QDialog* d = nullptr;
        QString path = arg;
        if (arg.startsWith("record:")) {
            d = new vfx::RecordDialog(app, &w);
            path = arg.section(':', 1);
        } else if (arg.startsWith("edit:")) {
            d = new vfx::SoundDialog(app, arg.section(':', 1, 1), &w);
            path = arg.section(':', 2);
        }
        if (arg.startsWith("size:")) { // size:<w>x<h>:<file>
            QStringList wh = arg.section(':', 1, 1).split('x');
            w.resize(wh.value(0).toInt(), wh.value(1).toInt());
            path = arg.section(':', 2);
            QTimer::singleShot(300, &w, [&w, path] { w.grab().save(path); });
            return;
        }
        if (!d) { w.show(); w.grab().save(path); return; }
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
        QTimer::singleShot(400, d, [d, path] { d->grab().save(path); d->close(); });
    });
    // Dev aid for headless runs/CI: VOXFX_DEV_SCREENSHOT=out.png saves the window
    // after a short delay; VOXFX_DEV_QUIT_MS=<ms> quits after that long.
    if (QString shot = qEnvironmentVariable("VOXFX_DEV_SCREENSHOT"); !shot.isEmpty())
        QTimer::singleShot(2500, &w, [&w, shot] { w.show(); w.grab().save(shot); });
    if (int ms = qEnvironmentVariableIntValue("VOXFX_DEV_QUIT_MS"); ms > 0)
        QTimer::singleShot(ms, &qapp, &QApplication::quit);
    return qapp.exec();
}
