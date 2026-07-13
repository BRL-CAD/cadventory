#include <QtTest/QtTest>
#include <QApplication>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QDebug>

#include <algorithm>
#include <filesystem>

#include "MainWindow.h"
#include "LibraryWindow.h"
#include "Library.h"
#include "Model.h"
#include "ModelFilterProxyModel.h"
#include "ModelCardDelegate.h"
#include "IndexingWorker.h"
#include "FilesystemIndexer.h"
#include "ProcessGFiles.h"
#include "GeometryBrowserDialog.h"


class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();       // Set up any common resources before tests
    void cleanupTestCase();    // Clean up resources after tests

    void testInitialization();
    void testAddLibrary();
    void testClearLibraries();
    void testSaveAndLoadState();
    void testAddLibraryButtonClick();
    void testLibraryCardLabel();
    void testStatusLabelUpdate();

private:
    MainWindow *mainWindow;
};

void MainWindowTest::initTestCase() {
    // set org and application name since QSettings uses these
    QCoreApplication::setOrganizationName("cadventory_MWT");
    QCoreApplication::setApplicationName("MainWindowTest");

    // let qt make our tempdir
    QString tempPath = QDir::tempPath();

    // use ini in tempdir for easier reproducability and plaintext debugging
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempPath);
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // zero settings and create our main window
    QSettings settings;
    settings.clear();
    settings.sync();
    mainWindow = new MainWindow();
}

void MainWindowTest::cleanupTestCase() {
    delete mainWindow;

    // clean up test directory (ie where we've written settings)
    QSettings settings;
    QDir settingsDir = QFileInfo(settings.fileName()).dir();
    if (settingsDir.exists())
	settingsDir.removeRecursively();
}

void MainWindowTest::testInitialization() {
    QCOMPARE(mainWindow->getLibraries().size(), size_t(0));
}

void MainWindowTest::testAddLibrary() {
    mainWindow->addLibrary("Test Library", BRLCAD_BUILD);
    QCOMPARE(mainWindow->getLibraries().size(), size_t(1));
    QCOMPARE(QString(mainWindow->getLibraries()[0]->name()), QString("Test Library"));
}

void MainWindowTest::testClearLibraries() {
    mainWindow->addLibrary("Library 1", BRLCAD_BUILD);
    //mainWindow->addLibrary("Library 2", "/path/to/library2");
    mainWindow->clearLibraries();
    QCOMPARE(mainWindow->getLibraries().size(), size_t(0));
}

void MainWindowTest::testSaveAndLoadState() {
    mainWindow->clearLibraries();
    mainWindow->addLibrary("Persistent Library", BRLCAD_BUILD);
    mainWindow->publicSaveState();

    QSettings settings;
    QCOMPARE(settings.beginReadArray("libraries"), 1);
    settings.setArrayIndex(0);
    QCOMPARE(settings.value("name").toString(), QString("Persistent Library"));
    settings.endArray();

    mainWindow->clearLibraries();  // Clear and then load
    QCOMPARE(mainWindow->getLibraries().size(), size_t(0));

    mainWindow->publicLoadState();
    QCOMPARE(mainWindow->getLibraries().size(), size_t(1));
    QCOMPARE(QString(mainWindow->getLibraries()[0]->name()), QString("Persistent Library"));
}

void MainWindowTest::testAddLibraryButtonClick() {
    mainWindow->clearLibraries();
    //QMetaObject::invokeMethod(mainWindow, "on_addLibraryButton_clicked");
    QCOMPARE(mainWindow->getLibraries().size(), size_t(0));
}

void MainWindowTest::testLibraryCardLabel() {
    const QString libraryName = QStringLiteral("Labeled Library");
    mainWindow->addLibrary(libraryName.toUtf8().constData(), BRLCAD_BUILD);

    const QList<QPushButton*> buttons = mainWindow->findChildren<QPushButton*>();
    const auto card = std::find_if(buttons.cbegin(), buttons.cend(),
        [&libraryName](const QPushButton* button) {
            return button->toolTip() == libraryName;
        });

    QVERIFY(card != buttons.cend());
    QCOMPARE((*card)->text(), libraryName);
    QCOMPARE((*card)->accessibleName(), libraryName);
}

void MainWindowTest::testStatusLabelUpdate() {
    mainWindow->updateStatusLabel("Status Updated");
    QCOMPARE(mainWindow->getStatusLabel(), QString("Status Updated"));
}

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
