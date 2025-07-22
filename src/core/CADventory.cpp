#include "CADventory.h"

#include <iostream>

#include <QCoreApplication>
#include <QPixmap>
#include <QTimer>
#include <QString>
#include <QDir>
#include <QSettings>
#include <QMessageBox>
#include <QProgressDialog>

#include "MainWindow.h"
#include "SplashDialog.h"
#include "FilesystemIndexer.h"
#include "OllamaCLIService.h"


CADventory::CADventory(int &argc, char *argv[], QObject* parent) : QObject(parent)
{
    // register singleton
    s_instance = this;

    // app-wide metadata
    QCoreApplication::setOrganizationName("BRL-CAD");
    QCoreApplication::setOrganizationDomain("brlcad.org");
    QCoreApplication::setApplicationName("CADventory");
    // TODO: set our version using config.h
    QCoreApplication::setApplicationVersion("0.2.0");

    // choose our llm backend
    // TODO: if/when we have more than one backend use a factory
    // TODO: have a top-level define for our ollama path
    this->llm = std::make_unique<OllamaCliService>(/*OLLAMA_PATH*/);

    // instantiate the model tagging object
    // TODO: have a top-level define (or settings option) for our desired model
    this->tagger = std::make_unique<AIModelTagging>(*llm, "llama3", this);

    // if any arg is specified, assume CLI-mode
    if (argc > 1) {
        this->gui = false;
        connect(this, &CADventory::indexingComplete, qApp, &QCoreApplication::quit);

        // could be separate setting, but let CLI-mode also wipe out all settings
        QSettings settings;
        settings.clear();
        settings.sync();
    }
}


CADventory::~CADventory()
{
    qDebug() << "CADventory destructor called - cleaning up resources";

    s_instance = nullptr;
    delete window;
    delete splash;
}

void CADventory::run() {
    // slight delay to let the splash screen settle
    QTimer::singleShot(250, this, [this]() {
        std::string home = QDir::homePath().toStdString();

        // if we didn't get a homestr, start in the current dir
        if (home.empty())
            home = ".";

        // start indexing
        this->indexDirectory(home.c_str());
    });
}

void CADventory::initMainWindow()
{
    window = new MainWindow();

    connect(this, &CADventory::indexingComplete, static_cast<MainWindow*>(window), &MainWindow::updateStatusLabel);

    window->show();

    // teardown splash / startup dialog
    if (QSplashScreen *splat = qobject_cast<QSplashScreen*>(splash)) {
        splat->finish(window);
        delete splat;
    } else {
        QDialog *diag = static_cast<QDialog*>(splash);
        delete diag;
    }

    // sanity
    splash = nullptr;
    qInfo() << "Done loading.";
}


void CADventory::showSplash()
{
    if (!this->gui)
        return;

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
        splash = new SplashDialog();
    } else {
        splash = new QSplashScreen(pixmap);
        static_cast<QSplashScreen*>(splash)->showMessage("Loading... please wait.", Qt::AlignLeft, Qt::black);
    }
    splash->show();

    // ensure the splash is displayed immediately
    QCoreApplication::processEvents();
}


void CADventory::indexDirectory(const char *path)
{
    qInfo() << "Indexing...";
    FilesystemIndexer f(path);

    f.setProgressCallback([this](const std::string& msg) {
        static size_t counter = 0;
        static const int MAX_MSG = 80;

        /* NOTE: only displaying every 1000 directories */
        if (counter++ % 1000 == 0 && splash) {
            std::string message = msg;

            // fill in '...' if we're at our max message size
            if (message.size() > MAX_MSG - 3) {
                message.resize(MAX_MSG - 3);
                message.append("...");
            }

            if (QSplashScreen* sc = qobject_cast<QSplashScreen*>(splash))
                sc->showMessage(QString::fromStdString(message), Qt::AlignLeft, Qt::white);

            QCoreApplication::processEvents(); // keep UI responsive
        }
    });

    f.indexDirectory(path);
    qInfo() << "... (found" << f.indexed() << "files) indexing done.";

    // TODO: we probably want to define these somewhere higher up as we expand support to more file types
    std::vector<std::string> geometryfilesuffixes{".g"};
    std::vector<std::string> imgfilesuffixes{".png", ".jpg", ".gif"};

    qInfo() << "Scanning...";
    std::vector<std::string> geometryfiles = f.findFilesWithSuffixes(geometryfilesuffixes);
    std::vector<std::string> imgfiles = f.findFilesWithSuffixes(imgfilesuffixes);
    qInfo() << "...scanning done.";

    qInfo() << "Found" << geometryfiles.size() << "geometry files";
    qInfo() << "Found" << imgfiles.size() << "image files";

    for (const auto& file : geometryfiles) {
        qInfo() << "Geometry: " + QString::fromStdString(file);
    }
    // TODO: improve logging options verbosity
#if 0
  for (const auto& file : imgfiles) {
    qInfo() << "Image: " + QString::fromStdString(file);
  }
#endif

    loaded = true;
    initMainWindow();

    // update the main window
    QString message = QString("Indexed %1 files (%2 geometry, %3 images)")
                        .arg(f.indexed())
                        .arg(geometryfiles.size())
                        .arg(imgfiles.size());

    emit indexingComplete(message.toUtf8().constData());
}


