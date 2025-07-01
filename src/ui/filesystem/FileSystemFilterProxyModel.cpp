#include "FileSystemFilterProxyModel.h"
#include <QFileSystemModel>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

FileSystemFilterProxyModel::FileSystemFilterProxyModel(const QString& rootPath, QObject* parent)
    : QSortFilterProxyModel(parent), rootPath(QDir::cleanPath(rootPath))
{
    setRecursiveFilteringEnabled(true);
}

bool FileSystemFilterProxyModel::filterAcceptsRow(int sourceRow,
                                                  const QModelIndex& sourceParent) const {
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    QFileSystemModel* fsModel = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fsModel)
        return false;

    QFileInfo fileInfo = fsModel->fileInfo(index);
    QString itemPath = fileInfo.absoluteFilePath();

    if (itemPath == rootPath) {
        // Accept the root path unconditionally
        return true;
    }

    if (!itemPath.startsWith(rootPath))
        return false;

    if (fileInfo.isDir()) {
        // Accept directories that have .g files or directories with .g files
        if (fsModel->canFetchMore(index)) {
            // Directory hasn't loaded yet, accept it for now
            return true;
        }
        return hasGFilesRecursively(index);
    } else {
        // Accept the file only if it has a .g extension
        return fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0;
    }
}

bool FileSystemFilterProxyModel::hasGFilesRecursively(const QModelIndex& index) const {
    QFileSystemModel* fsModel = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fsModel)
        return false;

    if (fsModel->canFetchMore(index)) {
        return true;
    }

    int rowCount = fsModel->rowCount(index);
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex childIndex = fsModel->index(i, 0, index);
        QFileInfo fileInfo = fsModel->fileInfo(childIndex);

        if (fileInfo.isDir()) {
            if (hasGFilesRecursively(childIndex))
                return true;
        } else {
            if (fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
                return true;
        }
    }
    return false;
}
