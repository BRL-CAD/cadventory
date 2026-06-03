#include "FileSystemModelWithCheckboxes.h"

#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QFileIconProvider>

#include <filesystem>

namespace {
bool isGModelFile(const QFileInfo& fileInfo) {
    return fileInfo.isFile() && fileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0;
}

class NullIconProvider : public QFileIconProvider {
public:
  QIcon icon(IconType) const override { return QIcon(); }
  QIcon icon(const QFileInfo&) const override { return QIcon(); }
};
} // namespace

FileSystemModelWithCheckboxes::FileSystemModelWithCheckboxes(const QString& rootPath, QObject* parent)
    : QFileSystemModel(parent),
      rootPath(QDir::cleanPath(rootPath)),
      m_hiddenPaths(this->rootPath.toStdString()),
      m_models(this->rootPath.toStdString())
{
    // Set filters to display directories and files
    setFilter(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);

    // lightweight icon loading
    setIconProvider(new NullIconProvider);

    connect(this, &QFileSystemModel::directoryLoaded, this, &FileSystemModelWithCheckboxes::onDirectoryLoaded);

    m_models.refresh();
    rootIndex = setRootPath(this->rootPath);
}

FileSystemModelWithCheckboxes::~FileSystemModelWithCheckboxes()
{
}

void FileSystemModelWithCheckboxes::initializeCheckStates(const QModelIndex& parentIndex)
{
    int rowCount = this->rowCount(parentIndex);

    for (int i = 0; i < rowCount; ++i) {
        QModelIndex index = this->index(i, 0, parentIndex);
        QString path = filePath(index);
        QFileInfo fileInfo = this->fileInfo(index);

        if (isDir(index)) {
            // dont recursively walk
            continue;
        }

        if (isGModelFile(fileInfo)) {
            ModelData modelData = lookupModelData(path);
            if (modelData.id < 0) {
                auto inserted = ensureModelForFile(fileInfo);
                if (!inserted)
                    continue;
                modelData = *inserted;
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

QVariant FileSystemModelWithCheckboxes::data(const QModelIndex& index, int role) const
{
    if (role == Qt::CheckStateRole && index.column() == 0) {
        QString path = filePath(index);
        QFileInfo fileInfo = this->fileInfo(index);

        // For directories, determine check state based on .g files only
        if (isDir(index)) {
            const QString dirPath = fileInfo.absoluteFilePath();
            if (!m_loadedDirs.contains(dirPath))
                // if dir isn't loaded yet - move on
                return QVariant();

            int checkedCount = 0;
            int uncheckedCount = 0;
            int gFileCount = 0;

            // iterate rows - only consider directory relevant if it has .g in it
            int rowCount = this->rowCount(index);
            for (int i = 0; i < rowCount; ++i) {
                QModelIndex childIndex = this->index(i, 0, index);
                QFileInfo childFileInfo = this->fileInfo(childIndex);

                if (childFileInfo.isDir()) {
                    const QString childDirPath = childFileInfo.absoluteFilePath();

                    if (m_loadedDirs.contains(childDirPath)) {
                        QVariant childData = data(childIndex, Qt::CheckStateRole);
                        if (childData.isValid()) {
                            Qt::CheckState childState = static_cast<Qt::CheckState>(childData.toInt());
                            if (childState == Qt::Checked)
                                ++checkedCount;
                            else if (childState == Qt::Unchecked)
                                ++uncheckedCount;
                            else {
                                ++checkedCount; // Partially checked counts as both
                                ++uncheckedCount;
                            }
                            ++gFileCount;
                        }
                    }
                } else if (childFileInfo.suffix().compare("g", Qt::CaseInsensitive) == 0) {
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
        } else if (isGModelFile(fileInfo)) {
            // this file is a .g
            QMutexLocker locker(&m_checkStatesMutex);
            if (m_checkStates.contains(path))
                return m_checkStates[path];
            else
                return Qt::Unchecked;
        } else {
            // For non-.g files, return no checkbox
            return QVariant();
        }
    }

    return QFileSystemModel::data(index, role);
}

bool FileSystemModelWithCheckboxes::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role == Qt::CheckStateRole && index.column() == 0) {
        QString path = filePath(index);
        Qt::CheckState state = static_cast<Qt::CheckState>(value.toInt());
        bool included = (state == Qt::Checked);
        QFileInfo fileInfo = this->fileInfo(index);

        if (isDir(index)) {
            const QString dirPath = fileInfo.absoluteFilePath();

            // if dir is not loaded yet, remember state and update later
            if (!m_loadedDirs.contains(dirPath)) {
                m_pendingDirState[dirPath] = state;
                emit dataChanged(index, index, {Qt::CheckStateRole});
                return true;
            }

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
        } else if (isGModelFile(fileInfo)) {
            ModelData modelData = lookupModelData(path);
            if (modelData.id >= 0) {
                if (!m_models.setModelIncluded(modelData.id, included))
                    return false;
            } else if (included) {
                auto inserted = ensureModelForFile(fileInfo);
                if (!inserted)
                    return false;
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

    if (index.column() == 0) {
        if (fileInfo.isDir()) {
            if (m_loadedDirs.contains(fileInfo.absoluteFilePath())) {
                // Directory loaded: show checkbox if it contains .g files or subdirectories with .g files
                QVariant checkStateData = data(index, Qt::CheckStateRole);
                if (checkStateData.isValid())
                    return defaultFlags | Qt::ItemIsUserCheckable;
            }
        } else if (isGModelFile(fileInfo)) {
            // .g file: show checkbox
            return defaultFlags | Qt::ItemIsUserCheckable;
        }
    }

    return defaultFlags;
}

void FileSystemModelWithCheckboxes::updateChildren(const QModelIndex& index, Qt::CheckState state)
{
    int rowCount = this->rowCount(index);
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex childIndex = this->index(i, 0, index);
        QString path = filePath(childIndex);
        QFileInfo fileInfo = this->fileInfo(childIndex);

        if (fileInfo.isDir()) {
            if (!m_loadedDirs.contains(path)) {
                // if dir isn't loaded yet - defer update to when it is
                m_pendingDirState[path] = state;
                continue;
            }

            {
                QMutexLocker locker(&m_checkStatesMutex);
                m_checkStates[path] = state;
            }

            emit dataChanged(childIndex, childIndex, {Qt::CheckStateRole});
            updateChildren(childIndex, state);
        } else if (isGModelFile(fileInfo)) {
            bool included = (state == Qt::Checked);
            ModelData modelData = lookupModelData(path);

            if (modelData.id >= 0) {
                if (!m_models.setModelIncluded(modelData.id, included))
                    continue;
            } else if (included) {
                if (!ensureModelForFile(fileInfo))
                    continue;
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
    for (int i = 0; i < rowCount; ++i) {
        QModelIndex siblingIndex = this->index(i, 0, parentIndex);
        QFileInfo fileInfo = this->fileInfo(siblingIndex);

        if (fileInfo.isDir()) {
            QVariant siblingData = data(siblingIndex, Qt::CheckStateRole);
            if (siblingData.isValid()) {
                Qt::CheckState siblingState = static_cast<Qt::CheckState>(siblingData.toInt());
                if (siblingState == Qt::Checked)
                    ++checkedCount;
                else if (siblingState == Qt::Unchecked)
                    ++uncheckedCount;
                else {
                    ++checkedCount;
                    ++uncheckedCount;
                }
                ++gFileCount;
            }
        } else if (isGModelFile(fileInfo)) {
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
    m_models.refresh();

    {
        QMutexLocker locker(&m_checkStatesMutex);
        m_checkStates.clear();
    }

    // Reset the model
    beginResetModel();

    m_loadedDirs.clear();
    m_pendingDirState.clear();

    // Reinitialize the model
    setRootPath(rootPath);
    rootIndex = index(rootPath);

    // Reinitialize check states starting from the root index
    initializeCheckStates(rootIndex);

    endResetModel();
}

void FileSystemModelWithCheckboxes::onDirectoryLoaded(const QString& path)
{
    m_loadedDirs.insert(path);

    QModelIndex index = this->index(path);
    initializeCheckStates(index);

    if (m_pendingDirState.contains(path)) {
        // apply deferred folder toggle
        const Qt::CheckState desired = m_pendingDirState.take(path);
        updateChildren(index, desired);
    }

    emit dataChanged(index, index, {Qt::CheckStateRole});
}

std::string FileSystemModelWithCheckboxes::relativePathFor(const QString& absolutePath) const
{
    return m_hiddenPaths.relativeToLibrary(absolutePath.toStdString());
}

ModelData FileSystemModelWithCheckboxes::lookupModelData(const QString& absolutePath) const
{
    const std::string relativePath = relativePathFor(absolutePath);
    ModelData modelData = m_models.getModelByFilePath(relativePath);
    if (modelData.id >= 0)
        return modelData;

    // Bridge old absolute-path rows while the broader migration is still underway.
    return m_models.getModelByFilePath(std::filesystem::path(absolutePath.toStdString()).generic_string());
}

std::optional<ModelData> FileSystemModelWithCheckboxes::ensureModelForFile(const QFileInfo& fileInfo)
{
    ModelData modelData{};
    modelData.short_name = fileInfo.fileName().toStdString();
    modelData.file_path = relativePathFor(fileInfo.absoluteFilePath());
    modelData.is_included = true;
    modelData.is_selected = false;
    modelData.is_processed = false;

    auto inserted = m_models.insertModel(modelData);
    if (inserted)
        return inserted;

    ModelData existing = lookupModelData(fileInfo.absoluteFilePath());
    if (existing.id >= 0)
        return existing;

    return std::nullopt;
}
