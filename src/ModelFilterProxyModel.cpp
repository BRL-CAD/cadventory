#include "ModelFilterProxyModel.h"
#include "Model.h"
#include <QRegularExpression>
#include <QVariant>
#include <QMetaType>


ModelFilterProxyModel::ModelFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
}


bool ModelFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    // Check if the model is included and processed
    if (!sourceModel()->data(index, Model::IsIncludedRole).toBool() ||
        !sourceModel()->data(index, Model::IsProcessedRole).toBool()) {
        return false;
    }

    // Proceed with existing filter logic
    QVariant data = sourceModel()->data(index, filterRole());

    if (filterRegularExpression().pattern().isEmpty()) {
        return true;
    }

    if (filterRole() == Model::TagsRole) {
        // Handle tag list search
        QStringList tags = data.toStringList();
		qDebug() << "Tags:" << tags;
        for (const QString& tag : tags) {
            if (tag.contains(filterRegularExpression())) {
                return true;
            }
        }
        return false;
    }

    // Generic string comparison fallback
    QString dataString;
    if (data.typeId() == QMetaType::Bool) {
        dataString = data.toBool() ? "1" : "0";
    }
    else {
        dataString = data.toString();
    }

    return dataString.contains(filterRegularExpression());
}
