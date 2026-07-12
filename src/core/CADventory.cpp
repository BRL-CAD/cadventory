#include "CADventory.h"
#include "UiShim.h"
#include "Logger.h"

#include <iostream>

#include <QCoreApplication>
#include <QTimer>
#include <QString>
#include <QDir>
#include <QSettings>
#include <QCommandLineParser>

#include "OllamaCLIService.h"
#include "JobManager.h"
#include "JobWorker.h"


static void addOptions(QCommandLineParser& parser) {
    // options
    QCommandLineOption indexOpt(QStringList{"index"},
                                "Index library (CLI, no GUI)",
                                "path");
    QCommandLineOption workerOpt(QStringList{"worker"},
                                "Library worker (CLI, no GUI)",
                                "path");
    QCommandLineOption resetOpt(QStringList{"r","reset"},
                                "Reset settings and model database");
    QCommandLineOption numCpusOpt(QStringList{"j", "num-cpus"},
                                "Number of worker threads",
                                "#");
    QCommandLineOption timeoutOpt(QStringList{"t", "timeout"},
                                "Worker job timeout value in seconds",
                                "#");
    // declare -v so parser accepts it; Logger class manually parses for stacking -v
    QCommandLineOption verboseOpt(QStringList{"v"},
                                "Increase verbosity (max logging at -vv)");
    // TODO: --worker (no gui worker)

    parser.addOption(indexOpt);
    parser.addOption(workerOpt);
    parser.addOption(resetOpt);
    parser.addOption(numCpusOpt);
    parser.addOption(timeoutOpt);
    parser.addOption(verboseOpt);
    parser.addHelpOption();
}

CADventory::CADventory(int &argc, char *argv[], QObject* parent) : QObject(parent)
{
    // register singleton
    s_instance = this;

    // app-wide metadata
    QCoreApplication::setOrganizationName("BRL-CAD");
    QCoreApplication::setOrganizationDomain("brlcad.org");
    QCoreApplication::setApplicationName("CADventory");
    QCoreApplication::setApplicationVersion(QString::fromStdString(version()));

    // cli options
    QCommandLineParser parser;
    addOptions(parser);
    parser.process(QCoreApplication::arguments());

    // Local AI settings are explicit, while PATH discovery remains a convenience fallback.
    QSettings settings;
    this->llm = std::make_unique<OllamaCliService>(settings.value("ai/ollamaExecutable").toString());
    this->tagger = std::make_unique<AIModelTagging>(*llm,
                                                    settings.value("ai/ollamaModel", "llama3").toString(),
                                                    this);

    // reset if requested
    if (parser.isSet("reset")) {
        // clear settings
        QSettings().clear();
        QSettings().sync();
    }

    // use Logger to parse the raw arguments so we can support stacking -vv
    Logger::instance().applyVerbosityFlags(QCoreApplication::arguments());

    if (parser.isSet("num-cpus")) {
        bool ok = false;
        int val = parser.value("num-cpus").toInt(&ok);
        if (ok && val >= 0) {
            QSettings().setValue("jobs/numThreads", val);
            QSettings().sync();
        }
    }

    if (parser.isSet("timeout")) {
        bool ok = false;
        int val = parser.value("timeout").toInt(&ok);
        if (ok && val >= 0) {
            QSettings().setValue("previewTimer", val);
            QSettings().sync();
        }
    }

    // choose our JobService
    if (parser.isSet("index")) {
        // use manager job service
        this->jobService = std::make_unique<JobManager>(parser.value("index"));

        // quit automatically after JobManager completes since we're just indexing
        auto Qt_cast_jobService = static_cast<QtJobServiceBase*>(jobService.get());
        connect(Qt_cast_jobService, &QtJobServiceBase::finished, qApp, &QCoreApplication::quit);
        connect(Qt_cast_jobService, &QtJobServiceBase::statsUpdated, Qt_cast_jobService,
                [&, barWidth = 30](const JobServiceStats& st) {
                    const std::size_t done = st.jobsNew;
                    const std::size_t total = st.modelsProcessed;

                    const double pct   = (total ? double(done) / double(total) : 1.0);
                    const int    fill  = int(pct * barWidth + 0.5);

                    LOG_RAW << '\r'
                            << '[' << std::string(fill, '=') << std::string(barWidth - fill, ' ')
                            << "] " << std::setw(3) << int(pct * 100.0) << "%";
                    if (total > 0 && done != total) {
                        LOG_RAW << " (" << done << '/' << total << ')';
                    } else {
                        LOG_RAW << " - inserted " << total << " files.";
                    }
                    LOG_RAW << std::flush;
                });

        // no gui
        this->gui = false;
    } else if (parser.isSet("worker")) {
        // JobWorker
        this->jobService = std::make_unique<JobWorker>(parser.value("worker"));

        // no gui
        this->gui = false;
    } else {
        this->jobService = std::make_unique<JobWorker>();
    }

    if (!cad_ui::guiBuilt() && !parser.isSet("worker") && !parser.isSet("index"))
        LOG_ERR << "Built headless: supply --index or --worker to do anything." << LOG_ENDL;
}


CADventory::~CADventory()
{
    LOG_DEBUG << "CADventory destructor called - cleaning up resources" << LOG_ENDL;

    if (jobService)
        jobService->stop();

    s_instance = nullptr;
}

void CADventory::run() {
    if (!this->gui) {
        // if we're running no gui, just start the service
        jobService->start();
        return;
    }

    // gui flow
    cad_ui::showSplash(*this);
    // slight delay and then show our main window
    QTimer::singleShot(250, this, [this]() {
        cad_ui::initMainWindow(*this);
    });
}

#if 0
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
    LOG_INFO << "Done loading." << LOG_ENDL;
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
    LOG_INFO << "Indexing..." << LOG_ENDL;
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
    LOG_INFO << "... (found" << f.indexed() << "files) indexing done." << LOG_ENDL;

    // TODO: we probably want to define these somewhere higher up as we expand support to more file types
    std::vector<std::string> geometryfilesuffixes{".g"};
    std::vector<std::string> imgfilesuffixes{".png", ".jpg", ".gif"};

    LOG_INFO << "Scanning..." << LOG_ENDL;
    std::vector<std::string> geometryfiles = f.findFilesWithSuffixes(geometryfilesuffixes);
    std::vector<std::string> imgfiles = f.findFilesWithSuffixes(imgfilesuffixes);
    LOG_INFO << "...scanning done." << LOG_ENDL;

    LOG_INFO << "Found" << geometryfiles.size() << "geometry files" << LOG_ENDL;
    LOG_INFO << "Found" << imgfiles.size() << "image files" << LOG_ENDL;

    for (const auto& file : geometryfiles) {
        LOG_INFO << "Geometry: " + QString::fromStdString(file) << LOG_ENDL;
    }
    // TODO: improve logging options verbosity
#if 0
  for (const auto& file : imgfiles) {
    LOG_INFO << "Image: " + QString::fromStdString(file) << LOG_ENDL;
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
#endif
