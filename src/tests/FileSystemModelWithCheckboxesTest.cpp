#include <catch2/catch_test_macros.hpp>
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "FileSystemModelWithCheckboxes.h"
#include "Model.h"

// Test class for FileSystemModelWithCheckboxes
class FileSystemModelWithCheckboxesTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir tempDir; // Temporary directory for testing

private slots:
    // Initialize the test case and verify the temporary directory is valid
    void initTestCase() {
        QVERIFY(tempDir.isValid());
    }

    // Test to verify data initialization using refresh
    void testRefreshUpdatesCheckStates() {
        Model model(tempDir.path().toStdString());
        FileSystemModelWithCheckboxes fileSystemModel(&model, tempDir.path());

        // Create a mock .g file
        QString gFilePath = tempDir.filePath("example.g");
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        // Insert the file into the database with correct ModelData initialization
        ModelData modelData;
        modelData.id = 1; // ID as integer
        modelData.short_name = "example.g";
        modelData.file_path = gFilePath.toStdString();
        modelData.is_included = true; // Ensure inclusion flag matches the type
        modelData.is_selected = false;
        modelData.is_processed = false;
        model.insertModel(modelData);

        // Call refresh and verify check states
        fileSystemModel.refresh();

        QModelIndex gFileIndex = fileSystemModel.index(gFilePath);
        QVariant checkState = fileSystemModel.data(gFileIndex, Qt::CheckStateRole);

        QCOMPARE(checkState.isValid(), true); // Ensure the data exists
    }

    // Test to verify parent-child checkbox interactions through setData
    void testParentChildCheckboxUpdate() {
        Model model(tempDir.path().toStdString());
        FileSystemModelWithCheckboxes fileSystemModel(&model, tempDir.path());

        // Create directory and .g files
        QString dirPath = tempDir.filePath("folder");
        QDir().mkdir(dirPath);

        QString gFilePath1 = dirPath + "/child1.g";
        QFile gFile1(gFilePath1);
        QVERIFY(gFile1.open(QIODevice::WriteOnly));
        gFile1.close();

        QString gFilePath2 = dirPath + "/child2.g";
        QFile gFile2(gFilePath2);
        QVERIFY(gFile2.open(QIODevice::WriteOnly));
        gFile2.close();

        // Refresh the model to initialize states
        fileSystemModel.refresh();

        QModelIndex dirIndex = fileSystemModel.index(dirPath);
        QModelIndex gFileIndex1 = fileSystemModel.index(gFilePath1);
        QModelIndex gFileIndex2 = fileSystemModel.index(gFilePath2);

        // Check the directory and verify the child items are updated
        fileSystemModel.setData(dirIndex, Qt::Checked, Qt::CheckStateRole);
        QCOMPARE(fileSystemModel.data(gFileIndex1, Qt::CheckStateRole).toInt(), Qt::Checked);
        QCOMPARE(fileSystemModel.data(gFileIndex2, Qt::CheckStateRole).toInt(), Qt::Checked);

        // Uncheck one child and verify parent becomes partially checked
        fileSystemModel.setData(gFileIndex1, Qt::Unchecked, Qt::CheckStateRole);
        QCOMPARE(fileSystemModel.data(dirIndex, Qt::CheckStateRole).toInt(), Qt::PartiallyChecked);
    }

    // Test to verify inclusionChanged signal is emitted
    void testInclusionChangedSignal() {
        Model model(tempDir.path().toStdString());
        FileSystemModelWithCheckboxes fileSystemModel(&model, tempDir.path());

        // Create a mock .g file
        QString gFilePath = tempDir.filePath("signalTest.g");
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        // Refresh the model to initialize states
        fileSystemModel.refresh();

        QModelIndex gFileIndex = fileSystemModel.index(gFilePath);

        // Connect to the signal and verify it is emitted correctly
        QSignalSpy signalSpy(&fileSystemModel, &FileSystemModelWithCheckboxes::inclusionChanged);

        fileSystemModel.setData(gFileIndex, Qt::Checked, Qt::CheckStateRole);
        QCOMPARE(signalSpy.count(), 1); // Verify signal was emitted once

        QList<QVariant> arguments = signalSpy.takeFirst();
        QVERIFY(arguments.at(0).toModelIndex() == gFileIndex);
        QVERIFY(arguments.at(1).toBool() == true);
    }

    // Test to verify data retrieval for non-.g files
    void testDataForNonGFiles() {
        Model model(tempDir.path().toStdString());
        FileSystemModelWithCheckboxes fileSystemModel(&model, tempDir.path());

        // Create a non-.g file
        QString txtFilePath = tempDir.filePath("example.txt");
        QFile txtFile(txtFilePath);
        QVERIFY(txtFile.open(QIODevice::WriteOnly));
        txtFile.close();

        QModelIndex txtFileIndex = fileSystemModel.index(txtFilePath);

        // Verify the CheckStateRole for non-.g file
        QVariant checkState = fileSystemModel.data(txtFileIndex, Qt::CheckStateRole);
        QVERIFY(!checkState.isValid()); // No checkbox for non-.g files
    }

    void testFlagsForDirectoriesAndGFiles() {
        Model model(tempDir.path().toStdString());
        FileSystemModelWithCheckboxes fileSystemModel(&model, tempDir.path());

        // Create directory and .g file
        QString dirPath = tempDir.filePath("testDir");
        QDir().mkdir(dirPath);

        QString gFilePath = dirPath + "/testModel.g";
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        QString txtFilePath = dirPath + "/testFile.txt";
        QFile txtFile(txtFilePath);
        QVERIFY(txtFile.open(QIODevice::WriteOnly));
        txtFile.close();

        // Refresh the model to initialize the states
        fileSystemModel.refresh();

        QModelIndex dirIndex = fileSystemModel.index(dirPath);
        QModelIndex gFileIndex = fileSystemModel.index(gFilePath);
        QModelIndex txtFileIndex = fileSystemModel.index(txtFilePath);

        // Test flags for directory
        Qt::ItemFlags dirFlags = fileSystemModel.flags(dirIndex);
        QVERIFY(dirFlags & Qt::ItemIsUserCheckable); // Directory should have checkable flag

        // Test flags for .g file
        Qt::ItemFlags gFileFlags = fileSystemModel.flags(gFileIndex);
        QVERIFY(gFileFlags & Qt::ItemIsUserCheckable); // .g file should have checkable flag

        // Test flags for non-.g file
        Qt::ItemFlags txtFileFlags = fileSystemModel.flags(txtFileIndex);
        QVERIFY(!(txtFileFlags & Qt::ItemIsUserCheckable)); // Non-.g file should not have checkable flag
    }

    void testOnDirectoryLoaded() {
        Model model(tempDir.path().toStdString());
        FileSystemModelWithCheckboxes fileSystemModel(&model, tempDir.path());

        // Create directory and .g files
        QString dirPath = tempDir.filePath("loadedDir");
        QDir().mkdir(dirPath);

        QString gFilePath1 = dirPath + "/file1.g";
        QFile gFile1(gFilePath1);
        QVERIFY(gFile1.open(QIODevice::WriteOnly));
        gFile1.close();

        QString gFilePath2 = dirPath + "/file2.g";
        QFile gFile2(gFilePath2);
        QVERIFY(gFile2.open(QIODevice::WriteOnly));
        gFile2.close();

        // Insert files into the database with correct ModelData initialization
        ModelData modelData1;
        modelData1.id = 1; // ID as integer
        modelData1.short_name = "file1.g";
        modelData1.file_path = gFilePath1.toStdString();
        modelData1.is_included = true; // Ensure inclusion flag matches the type
        modelData1.is_selected = false;
        modelData1.is_processed = false;
        model.insertModel(modelData1);

        ModelData modelData2;
        modelData2.id = 2; // ID as integer
        modelData2.short_name = "file2.g";
        modelData2.file_path = gFilePath2.toStdString();
        modelData2.is_included = true;
        modelData2.is_selected = false;
        modelData2.is_processed = false;
        model.insertModel(modelData2);

        // Trigger onDirectoryLoaded indirectly by loading the directory
        QSignalSpy directoryLoadedSpy(&fileSystemModel, &QFileSystemModel::directoryLoaded);
        fileSystemModel.setRootPath(dirPath);

        // qt is really slow to index new files on windows. Add a slight delay
        QThread::msleep(2000);

        // Wait for the directoryLoaded signal
        QVERIFY(directoryLoadedSpy.wait());

        // Verify check states for .g files in the loaded directory
        QModelIndex gFileIndex1 = fileSystemModel.index(gFilePath1);
        QModelIndex gFileIndex2 = fileSystemModel.index(gFilePath2);

        QVariant checkState1 = fileSystemModel.data(gFileIndex1, Qt::CheckStateRole);
        QVariant checkState2 = fileSystemModel.data(gFileIndex2, Qt::CheckStateRole);

        QCOMPARE(checkState1.isValid(), true);
        QCOMPARE(checkState1.toInt(), Qt::Checked); // Ensure file1.g is Checked
        QCOMPARE(checkState2.isValid(), true);
        QCOMPARE(checkState2.toInt(), Qt::Checked); // Ensure file2.g is Checked
    }
};

// Main function for running the test cases
QTEST_MAIN(FileSystemModelWithCheckboxesTest)
#include "FileSystemModelWithCheckboxesTest.moc"
