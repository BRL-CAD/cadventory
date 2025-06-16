#include <QtTest/QtTest>
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QDir>
#include <QTemporaryDir>

#include "../MainWindow.h"
#include "../LibraryWindow.h"
#include "../Library.h"
#include "../Model.h"
#include "../ModelFilterProxyModel.h"
#include "../ModelCardDelegate.h"
#include "../IndexingWorker.h"
#include "../FilesystemIndexer.h"
#include "../ProcessGFiles.h"
#include "../GeometryBrowserDialog.h"
#include "../ReportGenerationWindow.h"

class TestLibraryWindowGUI : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testLibraryWindowVisible();
    void testLibraryHomeButton();
    void cleanupTestCase();

private:
    LibraryWindow* libraryWindow;
};

// Initialize the LibraryWindow instance for testing
void TestLibraryWindowGUI::initTestCase()
{
    libraryWindow = new LibraryWindow();
    libraryWindow->show();
}

// Test if the LibraryWindow is visible after opening
void TestLibraryWindowGUI::testLibraryWindowVisible()
{
    QVERIFY(libraryWindow->isVisible());
}

// Test for Library's 'home' button existence and functionality
void TestLibraryWindowGUI::testLibraryHomeButton()
{
    QPushButton* addButton = libraryWindow->findChild<QPushButton*>("backButton");
    QVERIFY(addButton != nullptr);
    QTest::mouseClick(addButton, Qt::LeftButton);

    // library window should go away after we click the home button
    QVERIFY(!libraryWindow->isVisible());
}

// Clean up after tests
void TestLibraryWindowGUI::cleanupTestCase()
{
    delete libraryWindow;
}

QTEST_MAIN(TestLibraryWindowGUI)
#include "LibraryWindowTest.moc"
