// ModelFilterProxyModel.h

#ifndef MODELFILTERPROXYMODEL_H
#define MODELFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

class ModelFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit ModelFilterProxyModel(QObject* parent = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
};

#endif // MODELFILTERPROXYMODEL_H
