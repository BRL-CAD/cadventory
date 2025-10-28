#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QDateTime>
#include <QStringList>
#include <QVariant>

#include "QtModelsService.h"
#include "ModelsManager.h"
#include "ModelTypes.h"

#if CADVENTORY_WITH_GUI
#include <QPixmap>
#endif

// ---------- Helpers ----------
static QString freshTempLib(const char* tag) {
    const QString base = QDir::tempPath();
    const QString name = QString("%1_%2_%3")
        .arg("QtModelsService")
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
class QtModelsServiceTests : public QObject {
    Q_OBJECT
private slots:
    void roles_and_rowCount_and_data_basic() {
        const QString lib = freshTempLib("basic");
        QtModelsService svc(lib.toStdString());

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
        QVERIFY(roles.contains(QtModelsService::IdRole));
        QVERIFY(roles.contains(QtModelsService::ShortNameRole));
        QVERIFY(roles.contains(QtModelsService::TagsRole));

        // check a row
        QModelIndex i0 = svc.index(0, 0);
        QVERIFY(i0.isValid());
        const auto short0 = svc.data(i0, QtModelsService::ShortNameRole).toString();
        const auto path0  = svc.data(i0, QtModelsService::FilePathRole).toString();
        QVERIFY(short0 == "a" || short0 == "b");
        if (short0 == "a")  QCOMPARE(path0, QString("a.g"));
        if (short0 == "b")  QCOMPARE(path0, QString("b.g"));

        // tags is QStringList
        const QVariant tagsV = svc.data(i0, QtModelsService::TagsRole);
        QVERIFY(tagsV.canConvert<QStringList>());
        QCOMPARE(tagsV.toStringList().size(), 0);
    }

    void setData_toggles_flags_and_emits_reset_via_manager_notify() {
        const QString lib = freshTempLib("setData");
        QtModelsService svc(lib.toStdString());

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
        QVERIFY(svc.setData(i0, true, QtModelsService::IsIncludedRole));
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.data(i0, QtModelsService::IsIncludedRole).toBool(), true);

        // Flip selected
        QVERIFY(svc.setData(i0, true, QtModelsService::IsSelectedRole));
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.data(i0, QtModelsService::IsSelectedRole).toBool(), true);

        // Flip processed
        QVERIFY(svc.setData(i0, true, QtModelsService::IsProcessedRole));
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.data(i0, QtModelsService::IsProcessedRole).toBool(), true);
    }

    void selectAllIncluded_affects_only_included_rows() {
        const QString lib = freshTempLib("selectAll");
        QtModelsService svc(lib.toStdString());

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
            const auto sn = svc.data(svc.index(r,0), QtModelsService::ShortNameRole).toString();
            if (sn == "a") aRow = r;
            if (sn == "b") bRow = r;
        }
        QVERIFY(aRow >= 0 && bRow >= 0);

        QVERIFY(svc.setData(svc.index(aRow,0), true, QtModelsService::IsIncludedRole));
        QVERIFY(resetSpy.wait(500));

        // Bulk select included
        svc.selectAllIncluded(true);
        QVERIFY(resetSpy.wait(500));

        // Verify: only included rows become selected
        const bool aSelected = svc.data(svc.index(aRow,0), QtModelsService::IsSelectedRole).toBool();
        const bool bSelected = svc.data(svc.index(bRow,0), QtModelsService::IsSelectedRole).toBool();
        const bool aIncluded = svc.data(svc.index(aRow,0), QtModelsService::IsIncludedRole).toBool();
        const bool bIncluded = svc.data(svc.index(bRow,0), QtModelsService::IsIncludedRole).toBool();

        QVERIFY(aIncluded);
        QCOMPARE(aSelected, true);
        QCOMPARE(bIncluded, false);
        // b's selection should remain at default false
        QCOMPARE(bSelected, false);
    }

    void tags_role_reflects_repo_changes_after_refresh() {
        const QString lib = freshTempLib("tags");
        QtModelsService svc(lib.toStdString());

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
        QStringList tags = svc.data(i0, QtModelsService::TagsRole).toStringList();
        QCOMPARE(tags.size(), 2);
        QVERIFY(tags.contains("steel"));
        QVERIFY(tags.contains("aluminum"));

        // Remove one, refresh, re-check
        QVERIFY(mm.removeTagFromModel(r->id, "steel"));
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        tags = svc.data(i0, QtModelsService::TagsRole).toStringList();
        QCOMPARE(tags.size(), 1);
        QCOMPARE(tags[0], QString("aluminum"));
    }

    void thumbnail_role_decodes_png_conditionally() {
        const QString lib = freshTempLib("thumb");
        QtModelsService svc(lib.toStdString());

        // Insert a record with a small valid PNG blob
        ModelsManager mm(lib.toStdString());
        QVERIFY(mm.resetDatabase());
        ModelData m = makeModel("t.g", "t");
        m.thumbnail.assign(kPng1x1, kPng1x1 + sizeof(kPng1x1));
        auto r = mm.insertModel(m);
        QVERIFY(r.has_value());

        QSignalSpy resetSpy(&svc, &QAbstractItemModel::modelReset);
        svc.refresh();
        QVERIFY(resetSpy.wait(500));
        QCOMPARE(svc.rowCount(), 1);

        QModelIndex i0 = svc.index(0,0);
        const QVariant v = svc.data(i0, QtModelsService::ThumbnailRole);
#if CADVENTORY_WITH_GUI
        // With GUI, we expect a QPixmap QVariant (may be null pixmap if decode fails, but with the 1x1 it should succeed)
        QVERIFY(v.canConvert<QPixmap>());
#else
        // Headless builds return invalid/empty QVariant for ThumbnailRole
        QVERIFY(!v.isValid() || v.isNull());
#endif
    }
};

QTEST_MAIN(QtModelsServiceTests)
#include "QtModelsServiceTest.moc"
