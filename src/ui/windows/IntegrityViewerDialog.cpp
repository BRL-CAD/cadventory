#include "IntegrityViewerDialog.h"

#include "LibraryIntegrity.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString issueLabel(IntegrityIssueType type) {
    switch (type) {
    case IntegrityIssueType::MissingFile:
        return QObject::tr("Missing");
    case IntegrityIssueType::ModifiedFile:
        return QObject::tr("Modified since indexing");
    case IntegrityIssueType::UnreadableFile:
        return QObject::tr("Could not read");
    }
    return {};
}

}

IntegrityViewerDialog::IntegrityViewerDialog(const HiddenDir& paths, std::vector<ModelData> models,
                                             QWidget* parent)
    : QDialog(parent),
      m_paths(paths),
      m_models(std::move(models)) {
    setWindowTitle(tr("Library Integrity"));
    resize(900, 500);

    auto* layout = new QVBoxLayout(this);
    m_status = new QLabel(this);
    layout->addWidget(m_status);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("Issue"), tr("Model"), tr("Recorded Modified"),
                                        tr("Observed Modified"), tr("File Path")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(m_table);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    connect(refresh, &QPushButton::clicked, this, [this] { reload(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    reload();
}

void IntegrityViewerDialog::reload() {
    const IntegrityReport report = LibraryIntegrity::inspect(m_paths, m_models);
    m_table->setRowCount(static_cast<int>(report.issues.size()));
    for (int row = 0; row < static_cast<int>(report.issues.size()); ++row) {
        const IntegrityIssue& issue = report.issues[static_cast<std::size_t>(row)];
        const QString values[] = {issueLabel(issue.type),
                                  QStringLiteral("%1 (%2)").arg(QString::fromStdString(issue.shortName)).arg(issue.modelId),
                                  QString::fromStdString(issue.recordedModifiedAt),
                                  QString::fromStdString(issue.observedModifiedAt),
                                  QString::fromStdString(issue.filePath)};
        for (int column = 0; column < 5; ++column)
            m_table->setItem(row, column, new QTableWidgetItem(values[column]));
    }

    m_status->setText(tr("%1 model(s) checked; %2 issue(s) found")
                          .arg(report.checkedModels)
                          .arg(report.issues.size()));
}
