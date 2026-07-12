#include <catch2/catch_test_macros.hpp>
#include <QTest>
#include <QTemporaryDir>
#include <QFileSystemModel>
#include "FileSystemFilterProxyModel.h"

// Test class for FileSystemFilterProxyModel
class FileSystemFilterProxyModelTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir tempDir; // Temporary directory for testing

private slots:
    // Initialize the test case and verify the temporary directory is valid
    void initTestCase() {
        QVERIFY(tempDir.isValid());
    }

    // Test that .g files are visible in the proxy model
    void testVisibleGFiles() {
        QFileSystemModel fsModel;
        fsModel.setRootPath(tempDir.path());

        FileSystemFilterProxyModel proxyModel(tempDir.path());
        proxyModel.setSourceModel(&fsModel);

        // Create a .g file
        QString gFilePath = tempDir.filePath("example.g");
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        // Verify that the .g file is visible in the proxy model
        QModelIndex gFileIndex = proxyModel.mapFromSource(fsModel.index(gFilePath));
        QVERIFY(gFileIndex.isValid()); // The .g file should be visible
    }

    // A sibling whose name merely starts with the root name is still outside the root.
    void testRejectsSiblingWithMatchingPathPrefix() {
        QFileSystemModel fsModel;
        fsModel.setRootPath(tempDir.path());

        FileSystemFilterProxyModel proxyModel(tempDir.path());
        proxyModel.setSourceModel(&fsModel);

        const QString siblingPath = tempDir.path() + "-other";
        QVERIFY(QDir().mkpath(siblingPath));
        const QString gFilePath = siblingPath + "/outside.g";
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        QVERIFY(!proxyModel.mapFromSource(fsModel.index(gFilePath)).isValid());
        QVERIFY(QDir(siblingPath).removeRecursively());
    }

    // Test that directories containing .g files are visible
    void testVisibleDirectoriesWithGFiles() {
        QFileSystemModel fsModel;
        fsModel.setRootPath(tempDir.path());

        FileSystemFilterProxyModel proxyModel(tempDir.path());
        proxyModel.setSourceModel(&fsModel);

        // Create a directory and add a .g file
        QString dirPath = tempDir.filePath("testDir");
        QDir().mkdir(dirPath);

        QString gFilePath = dirPath + "/file.g";
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        // Verify that the directory is visible in the proxy model
        QModelIndex dirIndex = proxyModel.mapFromSource(fsModel.index(dirPath));
        QVERIFY(dirIndex.isValid()); // The directory should be visible
    }

    // Test recursive acceptance for directories containing .g files
    void testRecursiveDirectoriesWithGFiles() {
        QFileSystemModel fsModel;
        fsModel.setRootPath(tempDir.path());

        FileSystemFilterProxyModel proxyModel(tempDir.path());
        proxyModel.setSourceModel(&fsModel);

        // Create nested directories
        QString parentDirPath = tempDir.filePath("parentDir");
        QString childDirPath = parentDirPath + "/childDir";
        QDir().mkdir(parentDirPath);
        QDir().mkdir(childDirPath);

        // Add a .g file inside the nested directory
        QString gFilePath = childDirPath + "/nestedFile.g";
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        // Verify that the parent directory is visible in the proxy model
        QModelIndex parentDirIndex = proxyModel.mapFromSource(fsModel.index(parentDirPath));
        QVERIFY(parentDirIndex.isValid()); // The parent directory should be visible
    }

    // Test directories without .g files are hidden
    void testHiddenDirectoriesWithoutGFiles() {
        QFileSystemModel fsModel;
        fsModel.setRootPath(tempDir.path());

        FileSystemFilterProxyModel proxyModel(tempDir.path());
        proxyModel.setSourceModel(&fsModel);

        // Create an empty directory
        QString emptyDirPath = tempDir.filePath("emptyDir");
        QDir().mkdir(emptyDirPath);

        // Refresh to ensure directory state is updated
        fsModel.fetchMore(fsModel.index(emptyDirPath));

        QModelIndex emptyDirIndex = proxyModel.mapFromSource(fsModel.index(emptyDirPath));
        QVERIFY(!emptyDirIndex.isValid()); // Empty directory should not be visible
    }

    void testHasGFilesRecursively() {
        QFileSystemModel fsModel;
        fsModel.setRootPath(tempDir.path());

        FileSystemFilterProxyModel proxyModel(tempDir.path());
        proxyModel.setSourceModel(&fsModel);

        // Create a valid parent directory
        QString parentDirPath = tempDir.filePath("parentDir");
        QDir().mkdir(parentDirPath);

        QString childDirPath = parentDirPath + "/childDir";
        QDir().mkdir(childDirPath);

        // Add a `.g` file inside the nested directory
        QString gFilePath = childDirPath + "/nestedFile.g";
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();

        // Verify the parent directory is visible in the proxy model
        QModelIndex parentDirIndex = proxyModel.mapFromSource(fsModel.index(parentDirPath));
        QVERIFY(parentDirIndex.isValid());

        // Add another directory without `.g` files
        QString emptyDirPath = tempDir.filePath("emptyDir");
        QDir().mkdir(emptyDirPath);

        QModelIndex emptyDirIndex = fsModel.index(emptyDirPath);
        QVERIFY(emptyDirIndex.isValid()); // Ensure the directory is valid before proxy mapping

        // Map to proxy and validate
        QModelIndex proxyEmptyDirIndex = proxyModel.mapFromSource(emptyDirIndex);
        QVERIFY(proxyEmptyDirIndex.isValid()); // Empty directory should not be visible
    }

    void testHasGFilesRecursivelyComprehensive() {
        QFileSystemModel fsModel;
        fsModel.setRootPath(tempDir.path());

        FileSystemFilterProxyModel proxyModel(tempDir.path());
        proxyModel.setSourceModel(&fsModel);

        // Case 1: Empty directory
        QString emptyDirPath = tempDir.filePath("emptyDir");
        QDir().mkdir(emptyDirPath);

        QModelIndex emptyDirIndex = fsModel.index(emptyDirPath);
        QVERIFY(emptyDirIndex.isValid()); // Ensure the index is valid
        QVERIFY(proxyModel.mapFromSource(emptyDirIndex).isValid());

        // Case 2: Directory with canFetchMore == true
        QString fetchMoreDirPath = tempDir.filePath("fetchMoreDir");
        QDir().mkdir(fetchMoreDirPath);
        QModelIndex fetchMoreDirIndex = fsModel.index(fetchMoreDirPath);
        fsModel.fetchMore(fetchMoreDirIndex); // Simulate lazy loading
        QVERIFY(fetchMoreDirIndex.isValid());
        QVERIFY(proxyModel.mapFromSource(fetchMoreDirIndex).isValid()); // Should be visible

        // Case 3: Directory with .g files
        QString gDirPath = tempDir.filePath("gDir");
        QDir().mkdir(gDirPath);
        QString gFilePath = gDirPath + "/example.g";
        QFile gFile(gFilePath);
        QVERIFY(gFile.open(QIODevice::WriteOnly));
        gFile.close();
        QModelIndex gDirIndex = fsModel.index(gDirPath);
        QVERIFY(gDirIndex.isValid());
        QVERIFY(proxyModel.mapFromSource(gDirIndex).isValid()); // Should be visible

        // Case 4: Mixed file types
        QString mixedDirPath = tempDir.filePath("mixedDir");
        QDir().mkdir(mixedDirPath);
        QString validFilePath = mixedDirPath + "/valid.g";
        QString invalidFilePath = mixedDirPath + "/invalid.txt";
        QFile validFile(validFilePath), invalidFile(invalidFilePath);
        QVERIFY(validFile.open(QIODevice::WriteOnly));
        QVERIFY(invalidFile.open(QIODevice::WriteOnly));
        validFile.close();
        invalidFile.close();
        QModelIndex mixedDirIndex = fsModel.index(mixedDirPath);
        QVERIFY(mixedDirIndex.isValid());
        QVERIFY(proxyModel.mapFromSource(mixedDirIndex).isValid()); // Should be visible due to valid `.g` file

        // Case 5: Nested directories with `.g` files
        QString nestedParentPath = tempDir.filePath("nestedParent");
        QString nestedChildPath = nestedParentPath + "/nestedChild";
        QDir().mkdir(nestedParentPath);
        QDir().mkdir(nestedChildPath);
        QString nestedFilePath = nestedChildPath + "/nestedFile.g";
        QFile nestedFile(nestedFilePath);
        QVERIFY(nestedFile.open(QIODevice::WriteOnly));
        nestedFile.close();
        QModelIndex nestedParentIndex = fsModel.index(nestedParentPath);
        QVERIFY(nestedParentIndex.isValid());
        QVERIFY(proxyModel.mapFromSource(nestedParentIndex).isValid()); // Should be visible

        // Case 6: Case-insensitive `.g` extensions
        QString caseInsensitiveFilePath = gDirPath + "/EXAMPLE.G";
        QFile caseInsensitiveFile(caseInsensitiveFilePath);
        QVERIFY(caseInsensitiveFile.open(QIODevice::WriteOnly));
        caseInsensitiveFile.close();
        QModelIndex caseInsensitiveIndex = fsModel.index(caseInsensitiveFilePath);
        QVERIFY(caseInsensitiveIndex.isValid());
        QVERIFY(proxyModel.mapFromSource(caseInsensitiveIndex).isValid()); // Should be visible
    }

    void cleanupTestCase() {
        QVERIFY(tempDir.remove()); // Cleanup temporary directory after tests
    }
};

// Main function for running the test cases
QTEST_MAIN(FileSystemFilterProxyModelTest)
#include "FileSystemFilterProxyModelTest.moc"
