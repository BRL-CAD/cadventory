#pragma once

#include <QAbstractListModel>
#include <QString>

#include <memory>
#include <string>
#include <string_view>

#include "ModelsManager.h"

class QtModelsListModel final : public QAbstractListModel {
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
    TagsRole,
    LongNameRole,
    ModelersRole,
    ModelTypeRole,
    AliasesRole,
    SuitabilityRole,
    ClassificationRole,
    OwnerOrgRole,
    SourceOrgRole
    };

    explicit QtModelsListModel(const std::string& libraryPath, QObject* parent = nullptr);
    ~QtModelsListModel() override;

    // Override QAbstringListModel methods
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    // Convenience helpers
    Q_INVOKABLE void refresh();                 // pull from repo -> cache -> reset
    Q_INVOKABLE void selectAllIncluded(bool v); // forwards to manager

private:
    std::unique_ptr<ModelsManager> m_mgr;

    // helpers
    static bool validIndex(const QModelIndex& idx, int nrows) noexcept;
};
