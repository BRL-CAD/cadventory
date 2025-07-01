#ifndef GEOMETRYBROWSERDIALOG_H
#define GEOMETRYBROWSERDIALOG_H

#include <QDialog>
#include <QTreeWidget>
#include <map>
#include "Model.h"

class GeometryBrowserDialog : public QDialog {
    Q_OBJECT

public:
    GeometryBrowserDialog(int modelId, Model* model, QWidget* parent = nullptr);
    QTreeWidget* getTreeWidget() const { return treeWidget; }

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);

private:
    void loadObjects();
    void populateTreeWidget();
    void uncheckAllExcept(QTreeWidgetItem* exceptItem);
    void uncheckItemRecursively(QTreeWidgetItem* item, QTreeWidgetItem* exceptItem);

    int modelId;
    Model* model;
    QTreeWidget* treeWidget;

    // map object names to their IDs
    std::map<std::string, int> objectNameToIdMap;

    // map parent object names to their children's names
    std::map<std::string, std::vector<std::string>> geometryData;

    bool isUpdatingCheckState = false;
};

#endif // GEOMETRYBROWSERDIALOG_H
