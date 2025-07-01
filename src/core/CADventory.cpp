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


CADventory::CADventory(int &argc, char *argv[], QObject* parent) : QObject(parent)
{
    // app-wide metadata
    QCoreApplication::setOrganizationName("BRL-CAD");
    QCoreApplication::setOrganizationDomain("brlcad.org");
    QCoreApplication::setApplicationName("CADventory");
    // TODO: set our version using config.h
    QCoreApplication::setApplicationVersion("0.2.0");

    // instantiate the model tagging object
    this->modelTagging = new ModelTagging();

    /*QString appName = QCoreApplication::applicationName();
    QString appVersion = QCoreApplication::applicationVersion();*/

    // ANSI escape codes for underlining and reset
    /*QString underlineStart = "\033[4m";
    QString underlineEnd = "\033[0m";*/

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

    // shutdown ollama if we started it
    if (m_ollamaProcess) {
        qDebug() << "Terminating Ollama server...";
        m_ollamaProcess->terminate();

        if (!m_ollamaProcess->waitForFinished(3000)) {
#ifdef _WIN32
            QProcess::execute("taskkill", QStringList() << "/F" << "/IM" << "ollama.exe");
#else
            QProcess::execute("killall", QStringList() << "-9" << "ollama");
#endif
        }

        qDebug() << "Ollama server terminated with exit code:" << m_ollamaProcess->exitCode();
        delete m_ollamaProcess;
        m_ollamaProcess = nullptr;
    } else {
        qDebug() << "No Ollama process to terminate";
    }

    delete window;
    delete splash;
    delete modelTagging;
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

bool CADventory::startOllamaServer() {
    if (m_ollamaProcess) {
        qDebug() << "Ollama server is already running.";
        return true;
    }

    m_ollamaProcess = new QProcess(this);
    m_ollamaProcess->setProgram(getModelTagging()->getOllamaPath());
    m_ollamaProcess->setArguments(QStringList() << "serve");

    connect(m_ollamaProcess, &QProcess::readyReadStandardOutput, [this]() {
	    QString output = m_ollamaProcess->readAllStandardOutput();
	    qDebug() << "Ollama server output:" << output;
	    });
    connect(m_ollamaProcess, &QProcess::readyReadStandardError, [this]() {
	    QString error = m_ollamaProcess->readAllStandardError();
	    qDebug() << "Ollama server error:" << error;
	    });

    m_ollamaProcess->start();
    return true;
}

void CADventory::checkAndSetupModels() {
    ModelTagging* tagging = getModelTagging();

    // Check if Ollama is available
    if (!tagging->checkOllamaAvailability()) {
        QMessageBox::critical(nullptr, "Missing Component",
            "Unable to find Ollama. Please contact support.");
        return;
    }

    if (!startOllamaServer()) {
		QMessageBox::critical(nullptr, "Error",
			"Failed to start Ollama server. Please check your installation.");
		return;
    }

    // Check if model exists, download if needed
    if (!tagging->checkModelAvailability("llama3")) {
        QMessageBox msgBox;
        msgBox.setText("The application needs to download the AI model (~4GB).");
        msgBox.setInformativeText("This will take several minutes but only happens once. Continue?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

        if (msgBox.exec() == QMessageBox::Yes) {
            QProgressDialog* progress = new QProgressDialog("Downloading AI model...", "Cancel", 0, 0, nullptr);
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumWidth(400);
            progress->setAutoClose(false);
            progress->show();

            QProcess* process = new QProcess(this);
            process->setProgram(tagging->getOllamaPath());
            process->setArguments(QStringList() << "pull" << "llama3");

            // Connect to process output
            connect(process, &QProcess::readyReadStandardOutput, [process, progress]() {
                QString output = process->readAllStandardOutput();
                progress->setLabelText("Downloading model: " + output.trimmed());
                qDebug() << "Ollama output:" << output;
                });

            // Connect to process error
            connect(process, &QProcess::readyReadStandardError, [process, progress]() {
                QString error = process->readAllStandardError();
                progress->setLabelText("Error: " + error.trimmed());
                qDebug() << "Ollama error:" << error;
                });

            // Connect to process finished
            connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                [this, process, progress](int exitCode, QProcess::ExitStatus exitStatus) {
                    progress->close();
                    progress->deleteLater();
                    process->deleteLater();

                    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
                        QMessageBox::information(nullptr, "Success", "AI model downloaded successfully.");
                    }
                    else {
                        QMessageBox::critical(nullptr, "Error",
                            "Failed to download AI model. Exit code: " + QString::number(exitCode));
                    }
                });

            process->start();
        }
    }
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

    QTimer::singleShot(500, this, &CADventory::checkAndSetupModels);
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


