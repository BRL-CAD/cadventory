#pragma once

#include "HiddenDir.h"
#include "ModelTypes.h"

#include <QDialog>

#include <vector>

class QLabel;
class QTableWidget;

class IntegrityViewerDialog : public QDialog {
public:
    IntegrityViewerDialog(const HiddenDir& paths, std::vector<ModelData> models,
                          QWidget* parent = nullptr);

private:
    void reload();

    HiddenDir m_paths;
    std::vector<ModelData> m_models;
    QLabel* m_status = nullptr;
    QTableWidget* m_table = nullptr;
};
