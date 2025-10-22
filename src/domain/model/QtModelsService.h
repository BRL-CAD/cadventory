#pragma once

#include <QAbstractListModel>
#include <string>

#include "ModelsManager.h"

Q_DECLARE_METATYPE(ModelData)

class QtModelsService final : public QAbstractListModel, public ModelsManager {
    Q_OBJECT
public:
    enum ModelRoles {
        IdRole = Qt::UserRole + 1,
        ShortNameRole,
        PrimaryFileRole,
        OverrideInfoRole,
        TitleRole,
        ThumbnailRole,
        AuthorRole,
        FilePathRole,
        LibraryNameRole,
        IsSelectedRole,
        IsIncludedRole,
        IsProcessedRole,
	TagsRole
    };

    explicit QtModelsService(const std::string& libraryPath, QObject* parent = nullptr);
    ~QtModelsService() override;

    // Override QAbstringListModel methods
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int,QByteArray> roleNames() const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
};
