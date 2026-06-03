#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QDateTime>
#include <QStringList>
#include <QVariant>

#include "QtModelsListModel.h"
#include "ModelsManager.h"
#include "ModelTypes.h"

#if CADVENTORY_WITH_GUI
#include <QPixmap>
#endif

// ---------- Helpers ----------
static QString freshTempLib(const char* tag) {
    const QString base = QDir::tempPath();
    const QString name = QString("%1_%2_%3")
        .arg("QtModelsListModel")
        .arg(tag)
        .arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(base + "/" + name);
    return base + "/" + name;
}

static ModelData makeModel(QString fp, QString sn) {
    ModelData m{};
    m.file_path = fp.toStdString();
    m.short_name = sn.toStdString();
    m.library_name = "Lib";
    // rest left default
    return m;
}

// Tiny valid 1x1 PNG (opaque white)
static const unsigned char kPng1x1[] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,
    0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
    0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xde,0x00,0x00,0x00,0x0a,0x49,0x44,0x41,
    0x54,0x08,0xd7,0x63,0xf8,0xff,0xff,0x3f,
    0x00,0x05,0xfe,0x02,0xfe,0xa7,0x5d,0xe6,
    0x8d,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,
    0x44,0xae,0x42,0x60,0x82
};

// ---------- Test Suite ----------
class QtModelsListModelTests : public QObject {
    Q_OBJECT
private slots:
    void roles_and_rowCount_and_data_basic() {
        const QString lib = freshTempLib("basic");
        QtModelsListModel svc(lib.toStdString());

        // Seed via separate manager that points to the same library path:
        ModelsManager mm(lib.toStdString());
        QVERIFY(mm.resetDatabase());
        auto a = mm.insertModel(makeModel("a.g", "a"));
        auto b = mm.insertModel(makeModel("b.g", "b"));
        QVERIFY(a.has_value());
        QVERIFY(b.has_value());

        // refresh service to pick up new rows
        QSignalSpy resetSpy(&svc, &QAbstractItemModel::modelReset);
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.rowCount(), 2);

        // role names exist
        const auto roles = svc.roleNames();
        QVERIFY(roles.contains(QtModelsListModel::IdRole));
        QVERIFY(roles.contains(QtModelsListModel::ShortNameRole));
        QVERIFY(roles.contains(QtModelsListModel::TagsRole));
        QVERIFY(roles.contains(QtModelsListModel::LongNameRole));
        QVERIFY(roles.contains(QtModelsListModel::ModelersRole));
        QVERIFY(roles.contains(QtModelsListModel::ModelTypeRole));
        QVERIFY(roles.contains(QtModelsListModel::AliasesRole));
        QVERIFY(roles.contains(QtModelsListModel::SuitabilityRole));
        QVERIFY(roles.contains(QtModelsListModel::ClassificationRole));
        QVERIFY(roles.contains(QtModelsListModel::OwnerOrgRole));
        QVERIFY(roles.contains(QtModelsListModel::SourceOrgRole));
        QVERIFY(roles.contains(QtModelsListModel::CreatedAtFsRole));
        QVERIFY(roles.contains(QtModelsListModel::ModifiedAtFsRole));

        // check a row
        QModelIndex i0 = svc.index(0, 0);
        QVERIFY(i0.isValid());
        const auto short0 = svc.data(i0, QtModelsListModel::ShortNameRole).toString();
        const auto path0  = svc.data(i0, QtModelsListModel::FilePathRole).toString();
        QVERIFY(short0 == "a" || short0 == "b");
        if (short0 == "a")  QCOMPARE(path0, QString("a.g"));
        if (short0 == "b")  QCOMPARE(path0, QString("b.g"));

        // tags is QStringList
        const QVariant tagsV = svc.data(i0, QtModelsListModel::TagsRole);
        QVERIFY(tagsV.canConvert<QStringList>());
        QCOMPARE(tagsV.toStringList().size(), 0);
    }

    void setData_toggles_flags_and_emits_reset_via_manager_notify() {
        const QString lib = freshTempLib("setData");
        QtModelsListModel svc(lib.toStdString());

        // Seed one row
        ModelsManager mm(lib.toStdString());
        QVERIFY(mm.resetDatabase());
        auto rec = mm.insertModel(makeModel("a.g", "a"));
        QVERIFY(rec.has_value());

        QSignalSpy resetSpy(&svc, &QAbstractItemModel::modelReset);
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.rowCount(), 1);

        QModelIndex i0 = svc.index(0,0);
        QVERIFY(i0.isValid());

        // Flip included -> should succeed, and service should reset via subscription
        QVERIFY(svc.setData(i0, true, QtModelsListModel::IsIncludedRole));
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.data(i0, QtModelsListModel::IsIncludedRole).toBool(), true);

        // Flip selected
        QVERIFY(svc.setData(i0, true, QtModelsListModel::IsSelectedRole));
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.data(i0, QtModelsListModel::IsSelectedRole).toBool(), true);

        // Flip processed
        QVERIFY(svc.setData(i0, true, QtModelsListModel::IsProcessedRole));
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.data(i0, QtModelsListModel::IsProcessedRole).toBool(), true);
    }

    void selectAllIncluded_affects_only_included_rows() {
        const QString lib = freshTempLib("selectAll");
        QtModelsListModel svc(lib.toStdString());

        // Seed two rows
        ModelsManager mm(lib.toStdString());
        QVERIFY(mm.resetDatabase());
        auto a = mm.insertModel(makeModel("a.g", "a"));
        auto b = mm.insertModel(makeModel("b.g", "b"));
        QVERIFY(a.has_value());
        QVERIFY(b.has_value());

        QSignalSpy resetSpy(&svc, &QAbstractItemModel::modelReset);
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.rowCount(), 2);

        // Mark only the first row included
        QModelIndex i0 = svc.index(0,0);
        QModelIndex i1 = svc.index(1,0);
        QVERIFY(i0.isValid());
        QVERIFY(i1.isValid());

        // In case ordering is different, find the 'a' row by role
        int aRow = -1, bRow = -1;
        for (int r = 0; r < svc.rowCount(); ++r) {
            const auto sn = svc.data(svc.index(r,0), QtModelsListModel::ShortNameRole).toString();
            if (sn == "a") aRow = r;
            if (sn == "b") bRow = r;
        }
        QVERIFY(aRow >= 0 && bRow >= 0);

        QVERIFY(svc.setData(svc.index(aRow,0), true, QtModelsListModel::IsIncludedRole));
        QVERIFY(resetSpy.wait(500));

        // Bulk select included
        svc.selectAllIncluded(true);
        QVERIFY(resetSpy.wait(500));

        // Verify: only included rows become selected
        const bool aSelected = svc.data(svc.index(aRow,0), QtModelsListModel::IsSelectedRole).toBool();
        const bool bSelected = svc.data(svc.index(bRow,0), QtModelsListModel::IsSelectedRole).toBool();
        const bool aIncluded = svc.data(svc.index(aRow,0), QtModelsListModel::IsIncludedRole).toBool();
        const bool bIncluded = svc.data(svc.index(bRow,0), QtModelsListModel::IsIncludedRole).toBool();

        QVERIFY(aIncluded);
        QCOMPARE(aSelected, true);
        QCOMPARE(bIncluded, false);
        // b's selection should remain at default false
        QCOMPARE(bSelected, false);
    }

    void tags_role_reflects_repo_changes_after_refresh() {
        const QString lib = freshTempLib("tags");
        QtModelsListModel svc(lib.toStdString());

        // Seed & tag via external manager
        ModelsManager mm(lib.toStdString());
        QVERIFY(mm.resetDatabase());
        auto r = mm.insertModel(makeModel("x.g", "x"));
        QVERIFY(r.has_value());
        QVERIFY(mm.addTagToModel(r->id, "steel"));
        QVERIFY(mm.addTagToModel(r->id, "aluminum"));

        QSignalSpy resetSpy(&svc, &QAbstractItemModel::modelReset);
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.rowCount(), 1);

        // Verify tags via role
        QModelIndex i0 = svc.index(0,0);
        QStringList tags = svc.data(i0, QtModelsListModel::TagsRole).toStringList();
        QCOMPARE(tags.size(), 2);
        QVERIFY(tags.contains("steel"));
        QVERIFY(tags.contains("aluminum"));

        // Remove one, refresh, re-check
        QVERIFY(mm.removeTagFromModel(r->id, "steel"));
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        tags = svc.data(i0, QtModelsListModel::TagsRole).toStringList();
        QCOMPARE(tags.size(), 1);
        QCOMPARE(tags[0], QString("aluminum"));
    }

    void thumbnail_role_decodes_png_conditionally() {
        const QString lib = freshTempLib("thumb");
        QtModelsListModel svc(lib.toStdString());

        // Insert a record with a small valid PNG blob
        ModelsManager mm(lib.toStdString());
        QVERIFY(mm.resetDatabase());
        ModelData m = makeModel("t.g", "t");
        m.thumbnail.assign(kPng1x1, kPng1x1 + sizeof(kPng1x1));
        m.long_name = "Thumbnail Model";
        m.modelers = "Alice; Bob";
        m.model_type = "assembly";
        m.aliases = "Model T\nLegacy T";
        m.suitability = "reference";
        m.classification = "public";
        m.owner_org = "OpenAI";
        m.source_org = "BRL-CAD";
        m.created_at_fs = "2026-06-03T08:00:00Z";
        m.modified_at_fs = "2026-06-03T11:30:00Z";
        auto r = mm.insertModel(m);
        QVERIFY(r.has_value());

        QSignalSpy resetSpy(&svc, &QAbstractItemModel::modelReset);
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.rowCount(), 1);

        QModelIndex i0 = svc.index(0,0);
        const QVariant v = svc.data(i0, QtModelsListModel::ThumbnailRole);
#if CADVENTORY_WITH_GUI
        // With GUI, we expect a QPixmap QVariant (may be null pixmap if decode fails, but with the 1x1 it should succeed)
        QVERIFY(v.canConvert<QPixmap>());
#else
        // Headless builds return invalid/empty QVariant for ThumbnailRole
        QVERIFY(!v.isValid() || v.isNull());
#endif

        QCOMPARE(svc.data(i0, QtModelsListModel::LongNameRole).toString(), QString("Thumbnail Model"));
        QCOMPARE(svc.data(i0, QtModelsListModel::ModelersRole).toString(), QString("Alice; Bob"));
        QCOMPARE(svc.data(i0, QtModelsListModel::ModelTypeRole).toString(), QString("assembly"));
        QCOMPARE(svc.data(i0, QtModelsListModel::AliasesRole).toString(), QString("Model T\nLegacy T"));
        QCOMPARE(svc.data(i0, QtModelsListModel::SuitabilityRole).toString(), QString("reference"));
        QCOMPARE(svc.data(i0, QtModelsListModel::ClassificationRole).toString(), QString("public"));
        QCOMPARE(svc.data(i0, QtModelsListModel::OwnerOrgRole).toString(), QString("OpenAI"));
        QCOMPARE(svc.data(i0, QtModelsListModel::SourceOrgRole).toString(), QString("BRL-CAD"));
        QCOMPARE(svc.data(i0, QtModelsListModel::CreatedAtFsRole).toString(), QString("2026-06-03T08:00:00Z"));
        QCOMPARE(svc.data(i0, QtModelsListModel::ModifiedAtFsRole).toString(), QString("2026-06-03T11:30:00Z"));
    }
};

QTEST_MAIN(QtModelsListModelTests)
#include "QtModelsListModelTest.moc"
