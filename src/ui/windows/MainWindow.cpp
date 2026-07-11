// MainWindow.cpp

#include "CADventory.h"
#include "SettingWindow.h"
#include "MainWindow.h"
#include "LibraryWindow.h"
#include "Logger.h"

#include <iostream>
#include <QFileDialog>
#include <QPushButton>
#include <QSettings>
#include <QMessageBox>
#include <QMenuBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setMinimumSize(QSize(876, 600));
    resize(876, 600);
    ui.setupUi(this);
    setWindowTitle(QString("CADventory"));

    // Add our current version to label
    QString ver = QString::fromStdString(CADventory::instance()->version());
    ui.appNameVersionLabel->setText(QStringLiteral("CADventory v%1").arg(ver));

    // Adjust the + button label position
    QLayoutItem* item = ui.gridLayout->itemAt(0);
    QPushButton* addButton = qobject_cast<QPushButton*>(item->widget());
    if (addButton) {
        addButton->setStyleSheet("QPushButton { padding-top: -10px; }");
    }


    fileMenu = new QMenu(tr("&File"),this);
    editMenu = new QMenu(tr("&Edit"),this);
    windowMenu = new QMenu(tr("&Window"),this);
    removelib = new QMenu(tr("&Remove library"),this);

    ui.librarywidget->hide();



    QAction *set = new QAction(tr("&General Settings"),this);
    QAction *reset = new QAction(tr("&Reset"),this);

    windowMenu->addAction(set);
    fileMenu->addAction(reset);
    fileMenu->addMenu(removelib);


    menuBar()->addMenu(fileMenu);
    menuBar()->addMenu(windowMenu);

    settingWindow = new SettingWindow(this);
    connect(set,&QAction::triggered,this,&MainWindow::showSettingsWindow);
    connect(reset,&QAction::triggered,this,&MainWindow::resetting);



    // Load previously saved libraries
    size_t loaded = loadState();
    if (loaded) {
        LOG_DEBUG << "Loaded " << loaded << " previously registered libraries" << LOG_ENDL;
        updateStatusLabel("Choose a library to continue.");
    } else {
        updateStatusLabel("Add a library to begin indexing and organizing CAD files.");
    }

}

MainWindow::~MainWindow()
{
    // Clean up dynamically allocated libraries
    for (Library* lib : libraries) {
        delete lib;
    }
}

Library* MainWindow::addLibrary(const char* label, const char* path)
{
    for (Library* lib : libraries) {
        if (QString(lib->name()) == QString(label) && QString(lib->path()) == QString(path)) {
            LOG_DEBUG << "Library [" << label << "] already exists, skipping add." << LOG_ENDL;
            return nullptr;
        }
    }

    LOG_DEBUG << "Adding library [" << label << "] => " << path << LOG_ENDL;

    Library* newlib = new Library(label, path);
    libraries.push_back(newlib);

    QAction *lib = new QAction(tr(label),this);
    removelib->addAction(lib);

    connect(lib,&QAction::triggered,this, &MainWindow::removeLibrary);


    // Add a button for the new library
    addLibraryButton(label, path);

    return newlib;
}


void MainWindow::openLibrary()
{
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button)
        return;

    QString lookupKey = button->toolTip();
    Library* foundLibrary = nullptr;

    for (Library* lib : libraries) {
        if (QString(lib->name()) == lookupKey) {
            foundLibrary = lib;
            break;
        }
    }

    if (foundLibrary) {
        // ensure we have latest state of the database
        const QString loading_msg = "Opening: " + QString::fromUtf8(foundLibrary->name()) + "... (this could take a while)";
        this->updateStatusLabel(loading_msg.toStdString().c_str());
        foundLibrary->loadDatabase();

        LibraryWindow* libraryWindow = new LibraryWindow(this->centralWidget());
        LOG_DEBUG << "Opening library " << foundLibrary->name() << LOG_ENDL;

        // Set the main window pointer using a setter method
        libraryWindow->setMainWindow(this);


        ui.librarywidget=libraryWindow;

        ui.origin->hide();
        setWindowTitle(foundLibrary->name() + QString(" Library"));
       // ui.librarywidget->setAttribute(Qt::WA_DeleteOnClose);
        ui.librarywidget->show();

        // Load the library into the window (this should now start the indexing in the background)
        libraryWindow->loadFromLibrary(foundLibrary);


        LOG_DEBUG << "Loaded library " << foundLibrary->name() << LOG_ENDL;


    } else {
        QMessageBox::warning(this, "Library Not Found", "Could not find the library for " + lookupKey);
    }
}

void MainWindow::addLibraryButton(const char* label, const char* /*path*/)
{
    /* when we have more than this many buttons, we go smaller */
    const size_t LAYOUT_SHIFT = 20;
    const size_t COLUMNS = 5;
    static const QSize SMALLER_SIZE = QSize(128, 64);
    static const QSize DEFAULT_SIZE = QSize(128, 128);

    /* count how many buttons we got */
    size_t buttons = 0;
    QLayoutItem* item;
    for (size_t i = 0; i < (size_t)ui.gridLayout->count(); ++i) {
        item = ui.gridLayout->itemAt(i);
        if (!item) {
            break;
        }

        QPushButton* button = qobject_cast<QPushButton*>(item->widget());
        if (button)
            buttons++;
    }

    /* start making a new button */
    QPushButton *newButton = new QPushButton(label, this);
    if (buttons >= LAYOUT_SHIFT) {
        newButton->setFixedSize(SMALLER_SIZE);
    } else {
        newButton->setFixedSize(DEFAULT_SIZE);
    }
    QFont font = newButton->font();
    font.setPointSize(15);
    newButton->setFont(font);

    // Button label size adjustment, along with hover property
    QFontMetrics metrics(newButton->font());
    QString elidedText = metrics.elidedText(QString(label), Qt::ElideRight, newButton->width() - 10);
    newButton->setText(elidedText);
    newButton->setToolTip(QString(label));

    connect(newButton, &QPushButton::released, this, &MainWindow::openLibrary);

    /* add our new button */
    int row = (buttons) / COLUMNS;
    int column = (buttons) % COLUMNS;
    LOG_DEBUG << "row = "<< row << " column = "<< column << LOG_ENDL;
    ui.gridLayout->addWidget(newButton, row, column);

    /* once we have a lot of buttons, make them all smaller */
    if (buttons == LAYOUT_SHIFT) {
        for (int i = 0; i < ui.gridLayout->count(); ++i) {
            QLayoutItem* item = ui.gridLayout->itemAt(i);
            if (item && item->widget()) {
                QPushButton* button = qobject_cast<QPushButton*>(item->widget());
                if (button) {
                    button->setFixedSize(SMALLER_SIZE);
                }
            }
        }
    }
}

void MainWindow::updateStatusLabel(const char* status)
{
    ui.indexingStatus->setText(status);

    // ensure we display immediately
    QCoreApplication::processEvents();
}

QString MainWindow::getStatusLabel(){
    return ui.indexingStatus->text();
}

void MainWindow::on_addLibraryButton_clicked()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("Select Folder"), ".");
    if (!folderPath.isEmpty()) {
        QString name = QFileInfo(folderPath).fileName();

        QString adding = "Adding: \'" + name + "\'... (this could take a minute)";
        this->updateStatusLabel(adding.toStdString().c_str());

        // Add the library and its button
        if (Library* lib = addLibrary(name.toStdString().c_str(), folderPath.toStdString().c_str())) {
            // added library - index and show how many rows in db
            size_t files = lib->indexFiles();
            QString libCount = files ? QString("Found ") + QString::number(files) + QString(" indexed file(s) in ") + name :
                                       QString("No entries found in .cadventory. Run with --index");
            this->updateStatusLabel(libCount.toStdString().c_str());
        } else {
            // didn't add.
            QString alreadyExists = "Could not add \'" + name + "\'. Is it already added?";
            this->updateStatusLabel(alreadyExists.toStdString().c_str());
        }

        saveState();
    }
}

size_t MainWindow::saveState()
{
    QSettings settings;
    settings.beginWriteArray("libraries");
    size_t index = 0;
    for (auto lib : libraries) {

        settings.setArrayIndex(index++);
        settings.setValue("name", lib->name());
        settings.setValue("path", lib->path());
    }
    settings.endArray();

    return index;
}

size_t MainWindow::loadState()
{
    QSettings settings;
    size_t size = settings.beginReadArray("libraries");
    for (size_t i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString name = settings.value("name").toString();
        QString path = settings.value("path").toString();


        (void)addLibrary(name.toStdString().c_str(), path.toStdString().c_str());
    }
    settings.endArray();

    return size;
}


void MainWindow::clearLibraries()
{
    libraries.clear();
}

void MainWindow::showSettingsWindow()
{
    settingWindow->show();
}

void MainWindow::setPreviewFlag(bool state)
{
    previewFlag = state;
}

void MainWindow::returnCentralWidget()
{

    ui.librarywidget->hide();
    setWindowTitle( QString("Main Window"));
    ui.origin->show();

}


void MainWindow::resetting()
{


    for (int i = ui.gridLayout->count(); i>0 ; --i) {
        QLayoutItem* item = ui.gridLayout->takeAt(i);
        if (item && item->widget()) {
            delete item->widget();
            delete item;

        }
    }

    for (Library* lib : libraries) {
        delete lib;
    }

    libraries.erase(
        std::remove_if(
            libraries.begin(),
            libraries.end(),
            [](Library*) {
                return true;  // Remove all
            }),
        libraries.end()
        );

    QLayoutItem* item = ui.gridLayout->takeAt(0);
    ui.gridLayout->addWidget(item->widget(),0,0);
QSettings settings;
settings.clear();
settings.sync();
}

void MainWindow::removeLibrary()
{

    QAction* action = qobject_cast<QAction*>(sender());

    if (!action)
        return;

    QString lookupKey = action->text();
    Library* foundLibrary = nullptr;

    for (Library* lib : libraries) {

        if (QString(lib->name()) == lookupKey) {
            foundLibrary = lib;
            break;
        }
    }

    if (foundLibrary) {


        for (size_t i = (size_t)ui.gridLayout->count()-1; i > (size_t)0 ; --i) {
            QLayoutItem* item = ui.gridLayout->itemAt(i);

            QPushButton* button = qobject_cast<QPushButton*>(item->widget());
            if (button && button->text() == QString::fromLocal8Bit(foundLibrary->name())){
                LOG_DEBUG << ui.gridLayout->count() << LOG_ENDL;
                ui.gridLayout->removeWidget(button);
                delete button;
                LOG_DEBUG << ui.gridLayout->count() << LOG_ENDL;




                break;

            }
        }

        for (size_t i = (size_t)ui.gridLayout->count()-1; i > (size_t)0 ; --i) {
            QLayoutItem* item = ui.gridLayout->itemAt(i);

            QPushButton* button = qobject_cast<QPushButton*>(item->widget());
            const size_t COLUMNS = 5;
            int row = i / COLUMNS;
            int column = i % COLUMNS;
            ui.gridLayout->addWidget(button,row,column);
        }
        fileMenu->removeAction(action);
        delete action;
        libraries.erase(std::remove(libraries.begin(), libraries.end(), foundLibrary), libraries.end());

        saveState();

    } else {
        QMessageBox::warning(this, "Library Not Found", "Could not find the library for " + lookupKey);
    }


}
