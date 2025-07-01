#include "FileSystemModelWithCheckboxes.h"

#include <QDir>
#include <QDebug>
#include <QFileInfo>

FileSystemModelWithCheckboxes::FileSystemModelWithCheckboxes(Model* model, const QString& rootPath, QObject* parent)
    : QFileSystemModel(parent), model(model), rootPath(QDir::cleanPath(rootPath))
{
    // Set filters to display directories and files
    setFilter(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);

    connect(this, &QFileSystemModel::directoryLoaded, this, &FileSystemModelWithCheckboxes::onDirectoryLoaded);

    rootIndex = setRootPath(this->rootPath);
}

FileSystemModelWithCheckboxes::~FileSystemModelWithCheckboxes()
{
}

void FileSystemModelWithCheckboxes::initializeCheckStates(const QModelIndex& parentIndex)
{
    int rowCount = this->rowCount(parentIndex);

    for (int i = 0; i < rowCount; ++i)
    {
        QModelIndex index = this->index(i, 0, parentIndex);
        QString path = filePath(index);

        if (isDir(index))
        {
            // Recursively initialize child items
            initializeCheckStates(index);
        }
        else
        {
            QFileInfo fileInfo = this->fileInfo(index);
            if (fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
            {
                std::string filePathStd = QDir::cleanPath(path).toStdString();
                ModelData modelData = model->getModelByFilePath(filePathStd);

                if (modelData.id == 0)
                {
                    // Model not found in database, create a new one
                    modelData.short_name = fileInfo.fileName().toStdString();
                    modelData.file_path = filePathStd;
                    modelData.is_included = true;
                    modelData.is_selected = false;
                    modelData.is_processed = false;

                    model->insertModel(modelData);

                    // Fetch the inserted model to get the assigned id
                    modelData = model->getModelByFilePath(filePathStd);
                }

                // Update checkStates based on is_included
                Qt::CheckState state = modelData.is_included ? Qt::Checked : Qt::Unchecked;
                {
                    QMutexLocker locker(&m_checkStatesMutex);
                    m_checkStates[path] = state;
                }
            }
        }
    }
}

QVariant FileSystemModelWithCheckboxes::data(const QModelIndex& index, int role) const
{
    if (role == Qt::CheckStateRole && index.column() == 0)
    {
        QString path = filePath(index);
        QFileInfo fileInfo = this->fileInfo(index);

        // For directories, determine check state based on .g files only
        if (isDir(index))
        {
            int checkedCount = 0;
            int uncheckedCount = 0;
            int gFileCount = 0;

            int rowCount = this->rowCount(index);
            for (int i = 0; i < rowCount; ++i)
            {
                QModelIndex childIndex = this->index(i, 0, index);
                QFileInfo childFileInfo = this->fileInfo(childIndex);

                if (childFileInfo.isDir())
                {
                    QVariant childData = data(childIndex, Qt::CheckStateRole);
                    if (childData.isValid())
                    {
                        Qt::CheckState childState = static_cast<Qt::CheckState>(childData.toInt());
                        if (childState == Qt::Checked)
                            ++checkedCount;
                        else if (childState == Qt::Unchecked)
                            ++uncheckedCount;
                        else
                        {
                            ++checkedCount; // Partially checked counts as both
                            ++uncheckedCount;
                        }
                        ++gFileCount;
                    }
                }
                else if (childFileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
                {
                    QMutexLocker locker(&m_checkStatesMutex);
                    Qt::CheckState childState = Qt::Unchecked;
                    QString childPath = childFileInfo.absoluteFilePath();
                    if (m_checkStates.contains(childPath))
                        childState = m_checkStates[childPath];

                    if (childState == Qt::Checked)
                        ++checkedCount;
                    else
                        ++uncheckedCount;

                    ++gFileCount;
                }
            }

            if (gFileCount == 0)
                return QVariant(); // No .g files or subdirectories with .g files

            if (checkedCount == gFileCount)
                return Qt::Checked;
            else if (uncheckedCount == gFileCount)
                return Qt::Unchecked;
            else
                return Qt::PartiallyChecked;
        }
        else if (fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
        {
            QMutexLocker locker(&m_checkStatesMutex);
            if (m_checkStates.contains(path))
                return m_checkStates[path];
            else
                return Qt::Unchecked;
        }
        else
        {
            // For non-.g files, return no checkbox
            return QVariant();
        }
    }

    return QFileSystemModel::data(index, role);
}

bool FileSystemModelWithCheckboxes::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role == Qt::CheckStateRole && index.column() == 0)
    {
        QString path = filePath(index);
        Qt::CheckState state = static_cast<Qt::CheckState>(value.toInt());
        bool included = (state == Qt::Checked);
        QFileInfo fileInfo = this->fileInfo(index);

        if (isDir(index))
        {
            // Update all .g files and subdirectories recursively
            updateChildren(index, state);

            // Update this folder's state
            {
                QMutexLocker locker(&m_checkStatesMutex);
                m_checkStates[path] = state;
            }

            emit dataChanged(index, index, {Qt::CheckStateRole});

            // Update parent check state
            updateParent(index);

            return true;
        }
        else if (fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
        {
            std::string filePathStd = path.toStdString();
            ModelData modelData = model->getModelByFilePath(filePathStd);

            if (modelData.id != 0)
            {
                modelData.is_included = included;
                model->updateModel(modelData.id, modelData);
            }
            else if (included)
            {
                // Model not found, create a new one
                modelData.short_name = fileInfo.fileName().toStdString();
                modelData.file_path = filePathStd;
                modelData.is_included = true;
                modelData.is_processed = false;
                modelData.is_selected = false;

                model->insertModel(modelData);

                // Fetch the inserted model to get the assigned id
                modelData = model->getModelByFilePath(filePathStd);
            }

            {
                QMutexLocker locker(&m_checkStatesMutex);
                m_checkStates[path] = state;
            }

            emit dataChanged(index, index, {Qt::CheckStateRole});
            emit inclusionChanged(index, included);

            // Update parent check state
            updateParent(index);

            return true;
        }
    }

    return QFileSystemModel::setData(index, value, role);
}

Qt::ItemFlags FileSystemModelWithCheckboxes::flags(const QModelIndex& index) const
{
    Qt::ItemFlags defaultFlags = QFileSystemModel::flags(index);
    QFileInfo fileInfo = this->fileInfo(index);

    if (index.column() == 0)
    {
        if (fileInfo.isDir())
        {
            // Directory: show checkbox if it contains .g files or subdirectories with .g files
            QVariant checkStateData = data(index, Qt::CheckStateRole);
            if (checkStateData.isValid())
                return defaultFlags | Qt::ItemIsUserCheckable;
        }
        else if (fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
        {
            // .g file: show checkbox
            return defaultFlags | Qt::ItemIsUserCheckable;
        }
    }

    return defaultFlags;
}

void FileSystemModelWithCheckboxes::updateChildren(const QModelIndex& index, Qt::CheckState state)
{
    int rowCount = this->rowCount(index);
    for (int i = 0; i < rowCount; ++i)
    {
        QModelIndex childIndex = this->index(i, 0, index);
        QString path = filePath(childIndex);
        QFileInfo fileInfo = this->fileInfo(childIndex);

        if (fileInfo.isDir())
        {
            // Recursively update subdirectories
            updateChildren(childIndex, state);

            {
                QMutexLocker locker(&m_checkStatesMutex);
                m_checkStates[path] = state;
            }

            emit dataChanged(childIndex, childIndex, {Qt::CheckStateRole});
        }
        else if (fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
        {
            bool included = (state == Qt::Checked);
            std::string filePathStd = path.toStdString();
            ModelData modelData = model->getModelByFilePath(filePathStd);

            if (modelData.id != 0)
            {
                modelData.is_included = included;
                model->updateModel(modelData.id, modelData);
            }
            else if (included)
            {
                // Model not found, create a new one
                modelData.short_name = fileInfo.fileName().toStdString();
                modelData.file_path = filePathStd;
                modelData.is_included = true;
                modelData.is_selected = false;
                modelData.is_processed = false;
                model->insertModel(modelData);

                // Fetch the inserted model to get the assigned id
                modelData = model->getModelByFilePath(filePathStd);
            }

            {
                QMutexLocker locker(&m_checkStatesMutex);
                m_checkStates[path] = state;
            }

            emit dataChanged(childIndex, childIndex, {Qt::CheckStateRole});
            emit inclusionChanged(childIndex, included);
        }
    }
}

void FileSystemModelWithCheckboxes::updateParent(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    QModelIndex parentIndex = index.parent();
    if (!parentIndex.isValid())
        return;

    int checkedCount = 0;
    int uncheckedCount = 0;
    int gFileCount = 0;

    int rowCount = this->rowCount(parentIndex);
    for (int i = 0; i < rowCount; ++i)
    {
        QModelIndex siblingIndex = this->index(i, 0, parentIndex);
        QFileInfo fileInfo = this->fileInfo(siblingIndex);

        if (fileInfo.isDir())
        {
            QVariant siblingData = data(siblingIndex, Qt::CheckStateRole);
            if (siblingData.isValid())
            {
                Qt::CheckState siblingState = static_cast<Qt::CheckState>(siblingData.toInt());
                if (siblingState == Qt::Checked)
                    ++checkedCount;
                else if (siblingState == Qt::Unchecked)
                    ++uncheckedCount;
                else
                {
                    ++checkedCount;
                    ++uncheckedCount;
                }
                ++gFileCount;
            }
        }
        else if (fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0)
        {
            QVariant siblingData = data(siblingIndex, Qt::CheckStateRole);
            Qt::CheckState siblingState = static_cast<Qt::CheckState>(siblingData.toInt());
            if (siblingState == Qt::Checked)
                ++checkedCount;
            else if (siblingState == Qt::Unchecked)
                ++uncheckedCount;
            ++gFileCount;
        }
    }

    if (gFileCount == 0)
        return;

    Qt::CheckState parentState;
    if (checkedCount == gFileCount)
        parentState = Qt::Checked;
    else if (uncheckedCount == gFileCount)
        parentState = Qt::Unchecked;
    else
        parentState = Qt::PartiallyChecked;

    QString parentPath = filePath(parentIndex);
    {
        QMutexLocker locker(&m_checkStatesMutex);
        m_checkStates[parentPath] = parentState;
    }

    emit dataChanged(parentIndex, parentIndex, {Qt::CheckStateRole});

    // Recursively update parent
    updateParent(parentIndex);
}

void FileSystemModelWithCheckboxes::refresh()
{
    {
        QMutexLocker locker(&m_checkStatesMutex);
        m_checkStates.clear();
    }

    // Reset the model
    beginResetModel();

    // Reinitialize the model
    setRootPath(rootPath);
    rootIndex = index(rootPath);

    // Reinitialize check states starting from the root index
    initializeCheckStates(rootIndex);

    endResetModel();
}

void FileSystemModelWithCheckboxes::onDirectoryLoaded(const QString& path)
{
    QModelIndex index = this->index(path);
    initializeCheckStates(index);
}
