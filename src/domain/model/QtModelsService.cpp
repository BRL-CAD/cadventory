#include "QtModelsService.h"

#include <QMetaObject>
#include <QStringList>
#include <QString>

QtModelsService::QtModelsService(const std::string& libraryPath, QObject* parent)
    : QAbstractListModel(parent), ModelsManager(libraryPath) {

    // Subscribe the *same object* to its own manager changes
    subscribe([this] {
        if (thread() == QThread::currentThread()) {
            beginResetModel();
            endResetModel(); 
        } else {
            QMetaObject::invokeMethod(this, 
                                      [this] { beginResetModel(); endResetModel(); }, 
                                      Qt::QueuedConnection
                                     );
        }
    });
}

QtModelsService::~QtModelsService() = default;

int QtModelsService::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
  
    return static_cast<int>(ModelsManager::getAll_snapshot().size());
}

QVariant QtModelsService::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(getAll_snapshot().size()))
        return QVariant();

    const ModelData& modelData = getAll_snapshot().at(static_cast<size_t>(index.row()));

    switch (role) {
        case Qt::DisplayRole:
        case ShortNameRole:
            return QString::fromStdString(modelData.short_name);
        case IdRole:
            return modelData.id;
        case PrimaryFileRole:
            return QString::fromStdString(modelData.primary_file);
        case OverrideInfoRole:
            return QString::fromStdString(modelData.override_info);
        case TitleRole:
            return QString::fromStdString(modelData.title);
	case TagsRole:
            QStringList tagList;
            for (auto& tag : modelData.tags)
                tagList.append(QString::fromStdString(tag));
            return tagList;
        case ThumbnailRole:
    #if CADVENTORY_WITH_GUI
          if (!modelData.thumbnail.empty()) {
            QPixmap thumbnail;
            thumbnail.loadFromData(
                reinterpret_cast<const uchar*>(modelData.thumbnail.data()),
                static_cast<uint>(modelData.thumbnail.size()), "PNG");
            return thumbnail;
          }
    #endif
            return QVariant();
        case AuthorRole:
            return QString::fromStdString(modelData.author);
        case FilePathRole:
            return QString::fromStdString(modelData.file_path);
        case LibraryNameRole:
            return QString::fromStdString(modelData.library_name);
        case IsSelectedRole:
            return modelData.is_selected;
        case IsIncludedRole:
            return modelData.is_included;
        case IsProcessedRole:
            return modelData.is_processed;
        default:
            return QVariant();
    }
}

bool QtModelsService::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(getAll_snapshot().size()))
        return false;

    const auto modelData = getAll_snapshot()[index.row()];

    if (role == IsSelectedRole)
        return setModelSelected(modelData.id, value.toBool());
    else if (role == IsIncludedRole)
        return setModelIncluded(modelData.id, value.toBool());

    return false;
}

Qt::ItemFlags QtModelsService::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QHash<int, QByteArray> QtModelsService::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[ShortNameRole] = "short_name";
    roles[PrimaryFileRole] = "primary_file";
    roles[OverrideInfoRole] = "override_info";
    roles[TitleRole] = "title";
    roles[ThumbnailRole] = "thumbnail";
    roles[AuthorRole] = "author";
    roles[FilePathRole] = "file_path";
    roles[LibraryNameRole] = "library_name";
    roles[IsSelectedRole] = "is_selected";
    roles[IsProcessedRole] = "is_processed";
    roles[IsIncludedRole] = "is_included";
    roles[TagsRole] = "tags";

    return roles;
}