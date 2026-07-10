#include "AuditViewerDialog.h"

#include "AuditLog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

AuditViewerDialog::AuditViewerDialog(const std::string& auditRoot, QWidget* parent)
    : QDialog(parent),
      m_auditRoot(auditRoot) {
    setWindowTitle(tr("Audit Log"));
    resize(1000, 600);

    auto* layout = new QVBoxLayout(this);
    m_status = new QLabel(this);
    layout->addWidget(m_status);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Timestamp"), tr("Actor"), tr("Model"),
                                        tr("Property"), tr("Before"), tr("After"),
                                        tr("File Path")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setWordWrap(true);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    layout->addWidget(m_table);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* refresh = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    connect(refresh, &QPushButton::clicked, this, [this] { reload(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    reload();
}

void AuditViewerDialog::reload() {
    const AuditReadResult result = AuditLog(m_auditRoot).readEvents();
    m_table->setRowCount(static_cast<int>(result.events.size()));
    for (int row = 0; row < static_cast<int>(result.events.size()); ++row) {
        const AuditEvent& event = result.events[static_cast<std::size_t>(row)];
        const QString values[] = {event.timestamp, event.actor,
                                  QStringLiteral("%1 (%2)").arg(event.shortName).arg(event.modelId),
                                  event.property, event.before, event.after, event.filePath};
        for (int column = 0; column < 7; ++column)
            m_table->setItem(row, column, new QTableWidgetItem(values[column]));
    }
    m_table->resizeRowsToContents();

    QString status = tr("%1 audit event(s)").arg(result.events.size());
    if (result.invalidEvents > 0)
        status += tr("; %1 invalid file(s) ignored").arg(result.invalidEvents);
    m_status->setText(status);
}
