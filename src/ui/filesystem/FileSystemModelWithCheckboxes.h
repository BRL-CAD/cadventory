#ifndef FILESYSTEMMODELWITHCHECKBOXES_H
#define FILESYSTEMMODELWITHCHECKBOXES_H

#include <QFileSystemModel>
#include <QMutex>
#include <QMutexLocker>
#include "Model.h"

class FileSystemModelWithCheckboxes : public QFileSystemModel
{
    Q_OBJECT

public:
    FileSystemModelWithCheckboxes(Model* model, const QString& rootPath, QObject* parent = nullptr);
    ~FileSystemModelWithCheckboxes();

    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void refresh();

signals:
    void inclusionChanged(const QModelIndex& index, bool included);

private slots:
    void onDirectoryLoaded(const QString& path);

private:
    void initializeCheckStates(const QModelIndex& parentIndex);
    void updateChildren(const QModelIndex& index, Qt::CheckState state);
    void updateParent(const QModelIndex& index);

    Model* model;
    QString rootPath;
    QModelIndex rootIndex;

    mutable QMutex m_checkStatesMutex;
    mutable QHash<QString, Qt::CheckState> m_checkStates;
};

#endif // FILESYSTEMMODELWITHCHECKBOXES_H
