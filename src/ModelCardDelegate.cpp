#include "ModelCardDelegate.h"
#include "Model.h"
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QStyle>

#include <iostream>

ModelCardDelegate::ModelCardDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
    // No need to load settings icon; we'll use a red rectangle
}

void ModelCardDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    painter->save();

    // Retrieve data
    QPixmap thumbnail = index.data(Model::ThumbnailRole).value<QPixmap>();
    QString shortName = index.data(Model::ShortNameRole).toString();
    QString title = index.data(Model::TitleRole).toString();
    QString author = index.data(Model::AuthorRole).toString();
    bool isSelected = index.data(Model::IsSelectedRole).toBool();

    // Draw background
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    } else if (isSelected) {
        // Use a dark gray background for selected items
        QColor selectedColor = QColor(220, 220, 220);
        painter->fillRect(option.rect, selectedColor);
    } else {
        // For unselected items, use a slightly transparent background
        QColor unselectedColor = option.palette.window().color();
        unselectedColor.setAlpha(200); // 80% opacity
        painter->fillRect(option.rect, unselectedColor);
    }

    // Calculate rectangles
    QRect previewR = previewRect(option);
    QRect textR = textRect(option);
    QRect iconR = iconRect(option);


    // draw thumbnail or placeholder
    if (!thumbnail.isNull()) {
        painter->drawPixmap(previewR, thumbnail.scaled(previewR.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } else {
        // placeholder if no thumbnail
        painter->fillRect(previewR, Qt::lightGray);
        painter->drawText(previewR, Qt::AlignCenter, "No Image");
    }

    // text
    painter->setPen(option.palette.text().color());
    QFont boldFont = option.font;
    boldFont.setBold(true);
    painter->setFont(boldFont);
    painter->drawText(textR, Qt::AlignLeft | Qt::AlignTop, shortName);

    painter->setFont(option.font);
    painter->drawText(textR.adjusted(0, 20, 0, 0), Qt::AlignLeft | Qt::AlignTop, title);
    painter->drawText(textR.adjusted(0, 40, 0, 0), Qt::AlignLeft | Qt::AlignTop, author);

    // QIcon icon = QApplication::style()->standardIcon(QStyle::SP_DialogOpenButton);
    

    /*icon.paint(painter, iconR, Qt::AlignCenter, QIcon::Normal, QIcon::On);
    painter->restore();*/
    QIcon magnifyIcon = QIcon::fromTheme("edit-find");
    if (magnifyIcon.isNull()) {
        // Fallback: use a standard Qt icon that is adaptive
        magnifyIcon = QApplication::style()->standardIcon(QStyle::SP_FileDialogContentsView);
    }

    // Draw the adaptive magnifying glass icon in the icon rectangle.
    magnifyIcon.paint(painter, iconRect(option), Qt::AlignCenter, QIcon::Normal, QIcon::On);
}

QSize ModelCardDelegate::sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(300, 100);
}

bool ModelCardDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                    const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint pos = mouseEvent->pos();

        if (iconRect(option).contains(pos)) {
            int modelId = index.data(Model::IdRole).toInt();
            emit modelViewClicked(modelId);

            // accept the event and prevent further processing
            event->accept();
            return true;
        }
    }

    // For other areas, allow default processing
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}


// Helper methods to calculate component rectangles
QRect ModelCardDelegate::previewRect(const QStyleOptionViewItem& option) const {
    int margin = 10;
    int imageSize = option.rect.height() - 2 * margin;
    return QRect(option.rect.left() + margin, option.rect.top() + margin, imageSize, imageSize);
}

QRect ModelCardDelegate::textRect(const QStyleOptionViewItem& option) const {
    int margin = 10;
    int imageSize = option.rect.height() - 2 * margin;
    int textX = option.rect.left() + margin + imageSize + margin;
    int textWidth = option.rect.width() - imageSize - 4 * margin - 24;
    return QRect(textX, option.rect.top() + margin, textWidth, option.rect.height() - 2 * margin);
}

QRect ModelCardDelegate::iconRect(const QStyleOptionViewItem& option) const {
    int margin = 10;
    int iconSize = 24;
    int x = option.rect.right() - margin - iconSize;
    int y = option.rect.top() + margin;
    return QRect(x, y, iconSize, iconSize);
}
