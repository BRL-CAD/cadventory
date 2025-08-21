#pragma once

#if CADVENTORY_WITH_GUI
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QPixmap>
#include <QSplashScreen>
#include <QPointer>

#include "CADventory.h"
#include "SplashDialog.h"
#include "MainWindow.h"

namespace cad_ui {
    using AppT = QApplication;
    inline bool guiBuilt() noexcept { return true; }

    // keep minimal static state
    inline QPointer<QWidget>& splashRef() { static QPointer<QWidget> s; return s; }

    inline void showSplash(CADventory&) {
        // load splash image
        /* first look rel to binary */
        QString relativePathToBinary = QCoreApplication::applicationDirPath() + "/../share/splash.png";
        /* alternatively look rel to cwd */
        QString fallbackPath = QDir::current().absoluteFilePath("../src/splash.png");

        QPixmap pixmap;
        if (QFile::exists(relativePathToBinary)) {
            pixmap.load(relativePathToBinary);
        } else if (QFile::exists(fallbackPath)) {
            pixmap.load(fallbackPath);
        }
      
        if (pixmap.isNull()) {
            // TODO: do we want a black screen fallback if the pixmap fails?
            // pixmap = QPixmap(512, 512);
            // pixmap.fill(Qt::black);
            splashRef() = new SplashDialog();
        } else {
            auto *sc = new QSplashScreen(pixmap);
            sc->showMessage("Loading... please wait.", Qt::AlignLeft, Qt::black);
            splashRef() = sc;
        }
        splashRef()->show();

        // ensure the splash is displayed immediately
        QCoreApplication::processEvents();
    }

    inline void initMainWindow(CADventory& app) {
        static MainWindow* window = nullptr;
        if (!window) {
            // only init once
            window = new MainWindow();
            QObject::connect(&app, &CADventory::indexingComplete, window, &MainWindow::updateStatusLabel);
        }

        window->show();

        // teardown splash / startup dialog
        if (QSplashScreen *splat = qobject_cast<QSplashScreen*>(splashRef().data())) {
            splat->finish(window);
            delete splat;
        } else {
            QDialog *diag = static_cast<QDialog*>(splashRef().data());
            delete diag;
        }
        splashRef().clear();
    }
} // namespace cad_ui
#else
#include <QCoreApplication>

class CADventory;

namespace cad_ui {
    using AppT = QCoreApplication;
    inline bool guiBuilt() noexcept { return false; }
    inline void showSplash(CADventory&) {}
    inline void initMainWindow(CADventory&) {}
}
#endif /* CADVENTORY_WITH_GUI */
