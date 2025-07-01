#ifndef FILESYSTEMFILTERPROXYMODEL_H
#define FILESYSTEMFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

class FileSystemFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FileSystemFilterProxyModel(const QString& rootPath, QObject* parent = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    bool hasGFilesRecursively(const QModelIndex& index) const;
    QString rootPath;
};

#endif // FILESYSTEMFILTERPROXYMODEL_H
