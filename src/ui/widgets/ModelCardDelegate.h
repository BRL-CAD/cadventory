#ifndef MODELCARDDELEGATE_H
#define MODELCARDDELEGATE_H

#include <QStyledItemDelegate>
#include <QPixmap>

class ModelCardDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ModelCardDelegate(QObject* parent = nullptr);

    // Overridden methods
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option, const QModelIndex& index) override;

    // Helper methods to calculate component rectangles
    QRect iconRect(const QStyleOptionViewItem& option) const;

signals:
    void geometryBrowserClicked(int modelId);
    void modelViewClicked(int modelId);

private:
    QRect previewRect(const QStyleOptionViewItem& option) const;
    QRect textRect(const QStyleOptionViewItem& option) const;

};

#endif // MODELCARDDELEGATE_H
