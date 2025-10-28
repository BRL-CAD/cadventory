#include "QtModelsService.h"

#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <QString>

#if CADVENTORY_WITH_GUI
#include <QPixmap>
#endif

QtModelsService::QtModelsService(const std::string& libraryPath, QObject* parent)
    : QAbstractListModel(parent), m_mgr(std::make_unique<ModelsManager>(libraryPath)) {

    // connect manager to this service
    QPointer<QtModelsService> self(this);
    m_mgr->subscribe([self] {
        if (!self)
            return;

        QMetaObject::invokeMethod(
            self, [self] {
                if (!self)
                    return;
                self->beginResetModel();
                self->endResetModel();
            },
            Qt::QueuedConnection
        );
    });

    // prime
    refresh();
}

QtModelsService::~QtModelsService() {
    // zero out so we dont get callbacks after destruction
    if (m_mgr)
        m_mgr->subscribe(ModelsManager::Subscriber{});
}

int QtModelsService::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_mgr)
        return 0;
  
    const auto& snap = m_mgr->getAll_snapshot();
    return static_cast<int>(snap.size());
}

QVariant QtModelsService::data(const QModelIndex& index, int role) const {
    if (!m_mgr)
        return 0;

    const auto& snap = m_mgr->getAll_snapshot();
    if (!validIndex(index, static_cast<int>(snap.size())))
        return QVariant();

    const ModelData& modelData = snap[static_cast<size_t>(index.row())];
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
        case TagsRole: {
            QStringList tagList;
            tagList.reserve(static_cast<int>(modelData.tags.size()));
            for (const auto& tag : modelData.tags)
                tagList.push_back(QString::fromStdString(tag));
            return tagList;
        }
        case ThumbnailRole:
            // TODO/FIXME: optimize - we probably dont need to load on EVERY data() call
            return {};
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
    if (!m_mgr)
        return false;
    const auto& snap = m_mgr->getAll_snapshot();

    if (!validIndex(index, static_cast<int>(snap.size())))
        return false;

    const int id = snap[static_cast<size_t>(index.row())].id;
    switch (role) {
        case IsSelectedRole:
            return m_mgr->setModelSelected(id, value.toBool());
        case IsIncludedRole:
            return m_mgr->setModelIncluded(id, value.toBool());
        case IsProcessedRole:
            return m_mgr->setModelProcessed(id, value.toBool());
        default:
            break;
    }

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

void QtModelsService::refresh() {
    if (m_mgr)
        m_mgr->refresh();
}

void QtModelsService::selectAllIncluded(bool v) {
    if (m_mgr)
        m_mgr->selectAllIncluded(v);
}

bool QtModelsService::validIndex(const QModelIndex& idx, int nRows) noexcept {
    return idx.isValid() && !idx.parent().isValid() && idx.row() >= 0 && idx.row() < nRows;
}
