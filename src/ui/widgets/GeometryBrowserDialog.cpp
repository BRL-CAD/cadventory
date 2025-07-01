#include "GeometryBrowserDialog.h"
#include "Model.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QStyle>
#include <QTreeWidgetItem>
#include <QDebug>
#include <iostream>
#include <array>
#include <regex>
#include <sstream>

GeometryBrowserDialog::GeometryBrowserDialog(int modelId, Model* model, QWidget* parent)
    : QDialog(parent), modelId(modelId), model(model) {
    setWindowTitle("Geometry Browser");

    // load objects for the given model
    loadObjects();

    // create UI components
    treeWidget = new QTreeWidget();
    treeWidget->setHeaderLabel("Objects");
    treeWidget->header()->setSectionResizeMode(QHeaderView::Stretch);
    treeWidget->setExpandsOnDoubleClick(false);
    treeWidget->setAnimated(true);

    populateTreeWidget();

    // layout
    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->addWidget(treeWidget);

    setLayout(mainLayout);

    // connections
    connect(treeWidget, &QTreeWidget::itemChanged, this, &GeometryBrowserDialog::onItemChanged);
}

void GeometryBrowserDialog::loadObjects() {
    // Get the objects from the model
    std::vector<ObjectData> objects = model->getObjectsForModel(modelId);

    // Build the geometryData map and objectNameToIdMap
    for (const auto& obj : objects) {
        objectNameToIdMap[obj.name] = obj.object_id;

        if (obj.parent_object_id == -1) {
            geometryData[obj.name] = std::vector<std::string>();
        } else {
            std::string parentName;
            for (const auto& potentialParent : objects) {
                if (potentialParent.object_id == obj.parent_object_id) {
                    parentName = potentialParent.name;
                    break;
                }
            }
            if (!parentName.empty()) {
                geometryData[parentName].push_back(obj.name);
            } else {
                // handle missing parent
                geometryData[obj.name] = std::vector<std::string>();
            }
        }
    }
}

void GeometryBrowserDialog::populateTreeWidget() {
    QIcon folderClosedIcon = QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon);
    QIcon fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    // map object names to their selection state
    std::map<std::string, bool> objectSelectionMap;
    for (const auto& [name, id] : objectNameToIdMap) {
        ObjectData objData = model->getObjectById(id);
        objectSelectionMap[name] = objData.is_selected;
    }

    // create a mapping from object name to QTreeWidgetItem
    std::map<std::string, QTreeWidgetItem*> itemMap;

    // create all items
    for (const auto& [name, _] : objectNameToIdMap) {
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, QString::fromStdString(name));
        item->setCheckState(0, objectSelectionMap[name] ? Qt::Checked : Qt::Unchecked);

        itemMap[name] = item;
    }

    // set up hierarchy
    for (const auto& [parentName, childrenNames] : geometryData) {
        QTreeWidgetItem* parentItem = itemMap[parentName];
        for (const auto& childName : childrenNames) {
            QTreeWidgetItem* childItem = itemMap[childName];
            parentItem->addChild(childItem);
        }
    }

    // add top-level items to the tree widget
    for (const auto& [name, item] : itemMap) {
        if (geometryData.find(name) != geometryData.end()) {
            if (!item->parent()) {
                treeWidget->addTopLevelItem(item);
            }
            item->setIcon(0, folderClosedIcon);
        } else {
            if (!item->parent()) {
                treeWidget->addTopLevelItem(item);
            }
            item->setIcon(0, fileIcon);
        }
    }

    // expand all items
    treeWidget->expandAll();
}

void GeometryBrowserDialog::onItemChanged(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);

    // prevent infinite recursion
    if (isUpdatingCheckState)
        return;

    QString itemName = item->text(0);
    std::string objectName = itemName.toStdString();

    if (objectNameToIdMap.find(objectName) != objectNameToIdMap.end()) {
        int objectId = objectNameToIdMap[objectName];
        bool isSelected = (item->checkState(0) == Qt::Checked);

        // begin updating check states
        isUpdatingCheckState = true;

        if (isSelected) {
            uncheckAllExcept(item);

            // update the selection state in the model
            model->setObjectData(objectId, true, Model::IsSelectedRole);
        } else {
            // If the item is unchecked, update the model
            model->setObjectData(objectId, false, Model::IsSelectedRole);
        }

        isUpdatingCheckState = false;
    }
}

void GeometryBrowserDialog::uncheckAllExcept(QTreeWidgetItem* exceptItem) {
    // get all top-level items
    int topLevelItemCount = treeWidget->topLevelItemCount();
    for (int i = 0; i < topLevelItemCount; ++i) {
        QTreeWidgetItem* topItem = treeWidget->topLevelItem(i);
        uncheckItemRecursively(topItem, exceptItem);
    }
}

void GeometryBrowserDialog::uncheckItemRecursively(QTreeWidgetItem* item, QTreeWidgetItem* exceptItem) {
    if (item != exceptItem && item->checkState(0) == Qt::Checked) {
        item->setCheckState(0, Qt::Unchecked);

        // update the model's selection state
        QString itemName = item->text(0);
        std::string objectName = itemName.toStdString();
        if (objectNameToIdMap.find(objectName) != objectNameToIdMap.end()) {
            int objectId = objectNameToIdMap[objectName];
            model->setObjectData(objectId, false, Model::IsSelectedRole);
        }
    }

    // recurse for child items
    int childCount = item->childCount();
    for (int i = 0; i < childCount; ++i) {
        QTreeWidgetItem* childItem = item->child(i);
        uncheckItemRecursively(childItem, exceptItem);
    }
}
