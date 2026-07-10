#pragma once

#include <QDialog>

#include <string>

class QLabel;
class QTableWidget;

class AuditViewerDialog : public QDialog {
public:
    explicit AuditViewerDialog(const std::string& auditRoot, QWidget* parent = nullptr);

private:
    void reload();

    std::string m_auditRoot;
    QLabel* m_status = nullptr;
    QTableWidget* m_table = nullptr;
};
