#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#include "SQLModelRepository.h"
#include "SQLiteDB.h"
#include "ModelTypes.h"
#include "fixtures/TempDirFixture.h"

namespace fs = std::filesystem;

// ----- Helpers -----
static ModelData baseModel(std::string fp, std::string sn = "part") {
    ModelData m{};
    m.short_name   = std::move(sn);
    m.primary_file = "";
    m.override_info= "";
    m.title        = "";
    m.thumbnail    = {};
    m.author       = "";
    m.file_path    = std::move(fp);
    m.library_name = "Lib";
    m.is_selected  = false;
    m.is_processed = false;
    m.is_included  = false;
    return m;
}

static std::string dbpath(const TempDirFixture& fix) {
    return (fix.tempDir / "repo.db").string();
}

// ========== Tests ==========

TEST_CASE("Schema reset and basic insert succeeds") {
    TempDirFixture fix("repo_schema");
    SQLModelRepository repo(dbpath(fix));
    REQUIRE(repo.reset());

    auto a = repo.insertModel(baseModel("a.g", "foo"));
    REQUIRE(a.has_value());
    REQUIRE(a->id > 0);
    REQUIRE(a->file_path == "a.g"); // normalized generic
    REQUIRE(a->short_name == "foo");
}

TEST_CASE("Insert enforces unique file_path, and unique short_name is auto-resolved") {
    TempDirFixture fix("repo_insert_unique");
    SQLModelRepository repo(dbpath(fix));

    auto a = repo.insertModel(baseModel("a.g", "name"));
    REQUIRE(a);

    // duplicate filepath rejected
    auto dupfp = repo.insertModel(baseModel("a.g", "name2"));
    REQUIRE_FALSE(dupfp.has_value());

    // duplicate short_name -> allocator should suffix
    auto b = repo.insertModel(baseModel("b.g", "name"));
    REQUIRE(b);
    REQUIRE(b->short_name != a->short_name);
    REQUIRE(b->short_name.rfind("name_", 0) == 0); // starts with "name_"
}

TEST_CASE("getAllModels / getModelById / getModelByFilePath (with path normalization)") {
    TempDirFixture fix("repo_reads");
    SQLModelRepository repo(dbpath(fix));

    auto a = repo.insertModel(baseModel("dir/a.g", "a")); REQUIRE(a);
    auto b = repo.insertModel(baseModel("dir/b.g", "b")); REQUIRE(b);

    // all (expected stable order by id if you added ORDER BY id)
    auto all = repo.getAllModels();
    REQUIRE(all.size() == 2);
    REQUIRE(all[0].id == a->id);
    REQUIRE(all[1].id == b->id);

    // by id
    auto x = repo.getModelById(b->id);
    REQUIRE(x);
    REQUIRE(x->short_name == "b");

    // by normalized path: query with backslashes should still match
    auto y = repo.getModelByFilePath("dir/a.g");
    REQUIRE(y);
    REQUIRE(y->id == a->id);
}

TEST_CASE("Included and NotProcessed filters, plus isFileIncluded") {
    TempDirFixture fix("repo_included");
    SQLModelRepository repo(dbpath(fix));

    auto a = repo.insertModel(baseModel("a.g", "a")); REQUIRE(a);
    auto b = repo.insertModel(baseModel("b.g", "b")); REQUIRE(b);

    REQUIRE_FALSE(repo.isFileIncluded("a.g"));
    REQUIRE(repo.setModelIncluded(a->id, true));
    REQUIRE(repo.setModelIncluded(b->id, true));

    auto inc = repo.getIncludedModels();
    REQUIRE(inc.size() == 2);

    auto incNP = repo.getIncludedNotProcessedModels();
    REQUIRE(incNP.size() == 2);

    REQUIRE(repo.setModelProcessed(a->id, true));
    auto incNP2 = repo.getIncludedNotProcessedModels();
    REQUIRE(incNP2.size() == 1);
    REQUIRE(incNP2[0].id == b->id);
}

TEST_CASE("Bulk ops: markAllNotIncluded returns affected rows; selectAllIncluded toggles selection") {
    TempDirFixture fix("repo_bulk");
    SQLModelRepository repo(dbpath(fix));

    auto a = repo.insertModel(baseModel("a.g")); REQUIRE(a);
    auto b = repo.insertModel(baseModel("b.g")); REQUIRE(b);
    REQUIRE(repo.setModelIncluded(a->id, true));
    REQUIRE(repo.setModelIncluded(b->id, true));

    int changed = repo.markAllNotIncluded();
    REQUIRE(changed == 2);

    // Now selecting included should affect none
    REQUIRE(repo.selectAllIncluded(true));
    auto inc = repo.getIncludedModels();
    REQUIRE(inc.empty());
}

TEST_CASE("updateModel: preserves file_path and short_name when omitted, enforces uniqueness when changed") {
    TempDirFixture fix("repo_update");
    SQLModelRepository repo(dbpath(fix));

    auto a = repo.insertModel(baseModel("a.g", "nameA")); REQUIRE(a);
    auto b = repo.insertModel(baseModel("b.g", "nameB")); REQUIRE(b);

    // Prepare patch with empty file_path and short_name => preserve both
    ModelData patch = *a;
    patch.file_path.clear();     // should keep "a.g"
    patch.short_name.clear();    // should keep "nameA"
    patch.title = "New Title";
    REQUIRE(repo.updateModel(a->id, patch));

    auto a1 = repo.getModelById(a->id); REQUIRE(a1);
    REQUIRE(a1->file_path == "a.g");
    REQUIRE(a1->short_name == "nameA");
    REQUIRE(a1->title == "New Title");

    // Changing to an existing file_path should fail
    patch = *a1;
    patch.file_path = "b.g"; // already in use
    REQUIRE_FALSE(repo.updateModel(a->id, patch));

    // Change to a fresh filepath succeeds
    patch = *a1;
    patch.file_path = "c.g";
    REQUIRE(repo.updateModel(a->id, patch));
    auto a2 = repo.getModelById(a->id); REQUIRE(a2);
    REQUIRE(a2->file_path == "c.g");

    // Change short_name to an occupied one -> allocator should suffix
    patch = *a2;
    patch.short_name = "nameB";
    REQUIRE(repo.updateModel(a->id, patch));
    auto a3 = repo.getModelById(a->id); REQUIRE(a3);
    REQUIRE(a3->short_name != "nameB");  // should have been uniquified
    REQUIRE(a3->short_name.rfind("nameB_", 0) == 0);
}

TEST_CASE("Canonical metadata fields persist while keeping title and author compatibility") {
    TempDirFixture fix("repo_metadata_fields");
    SQLModelRepository repo(dbpath(fix));

    ModelData seed = baseModel("metadata.g", "metadata");
    seed.long_name = "Canonical Long Name";
    seed.modelers = "Ada Lovelace; Grace Hopper";
    seed.model_type = "assembly";
    seed.owner_org = "OpenAI";
    seed.source_org = "BRL-CAD";

    auto inserted = repo.insertModel(seed);
    REQUIRE(inserted);
    REQUIRE(inserted->title == "Canonical Long Name");
    REQUIRE(inserted->author == "Ada Lovelace; Grace Hopper");

    auto roundTrip = repo.getModelById(inserted->id);
    REQUIRE(roundTrip);
    REQUIRE(roundTrip->long_name == "Canonical Long Name");
    REQUIRE(roundTrip->modelers == "Ada Lovelace; Grace Hopper");
    REQUIRE(roundTrip->model_type == "assembly");
    REQUIRE(roundTrip->owner_org == "OpenAI");
    REQUIRE(roundTrip->source_org == "BRL-CAD");
    REQUIRE(roundTrip->title == "Canonical Long Name");
    REQUIRE(roundTrip->author == "Ada Lovelace; Grace Hopper");
}

TEST_CASE("Thumbnail set/get roundtrip and clearing") {
    TempDirFixture fix("repo_thumb");
    SQLModelRepository repo(dbpath(fix));

    auto a = repo.insertModel(baseModel("a.g")); REQUIRE(a);

    std::vector<unsigned char> png{0x89,0x50,0x4E,0x47};
    REQUIRE(repo.setModelThumbnail(a->id, png));
    auto got = repo.getThumbnail(a->id);
    REQUIRE(got == png);

    // clear
    REQUIRE(repo.setModelThumbnail(a->id, {}));
    auto got2 = repo.getThumbnail(a->id);
    REQUIRE(got2.empty());
}

TEST_CASE("Tags: add (idempotent), list ordered by name, remove") {
    TempDirFixture fix("repo_tags");
    SQLModelRepository repo(dbpath(fix));

    auto a = repo.insertModel(baseModel("a.g")); REQUIRE(a);

    REQUIRE(repo.addTagToModel(a->id, "steel"));
    REQUIRE(repo.addTagToModel(a->id, "aluminum"));
    REQUIRE(repo.addTagToModel(a->id, "steel")); // idempotent link

    auto tags = repo.getTagsForModel(a->id);
    REQUIRE(tags.size() == 2);
    // If you added ORDER BY name in getTagsForModel(), the order is alphabetical:
    REQUIRE(tags[0] == "aluminum");
    REQUIRE(tags[1] == "steel");

    REQUIRE(repo.removeTagFromModel(a->id, "steel"));
    auto tags2 = repo.getTagsForModel(a->id);
    REQUIRE(tags2.size() == 1);
    REQUIRE(tags2[0] == "aluminum");
}

TEST_CASE("deleteModel cascades to objects and model_tags") {
    TempDirFixture fix("repo_delete");
    const auto path = dbpath(fix);
    SQLModelRepository repo(path);

    auto a = repo.insertModel(baseModel("a.g")); REQUIRE(a);

    // Create one object row directly and one tag link to exercise cascades
    {
        SQLiteDB raw(path);
        REQUIRE(raw.exec("INSERT INTO objects(model_id, name) VALUES(?1, ?2);",
                         [&](sqlite3_stmt* st){
                             sqlite3_bind_int(st, 1, a->id);
                             sqlite3_bind_text(st, 2, "obj", -1, SQLITE_TRANSIENT);
                         }));
    }
    REQUIRE(repo.addTagToModel(a->id, "steel"));

    REQUIRE(repo.deleteModel(a->id));
    REQUIRE_FALSE(repo.getModelById(a->id).has_value());

    // objects/model_tags rows should be gone due to cascades
    {
        SQLiteDB raw(path);
        int cntObj=0, cntLink=0;
        raw.query("SELECT COUNT(*) FROM objects;", nullptr, [&](sqlite3_stmt* st){ cntObj = sqlite3_column_int(st,0); return true; });
        raw.query("SELECT COUNT(*) FROM model_tags;", nullptr, [&](sqlite3_stmt* st){ cntLink = sqlite3_column_int(st,0); return true; });
        REQUIRE(cntObj == 0);
        REQUIRE(cntLink == 0);
    }
}

TEST_CASE("makeUniqueShortName() behavior: dense and gapped suffixes") {
    TempDirFixture fix("repo_shortname");
    const auto path = dbpath(fix);
    SQLModelRepository repo(path);

    // Pre-seed names: base, base_1..base_5, and a gap at 2 (we'll create out-of-order)
    // We'll insert rows directly to control names.
    auto ins = [&](const std::string& sn){
        ModelData m = baseModel(sn + ".g", sn);
        auto r = repo.insertModel(m);
        REQUIRE(r); // keep simple for this test
        return *r;
    };

    auto a0 = ins("base");    // base
    auto a1 = ins("base_1");  // base_1
    auto a3 = ins("base_3");  // base_3
    auto a4 = ins("base_4");  // base_4
    auto a5 = ins("base_5");  // base_5

    // Now asking for short_name "base" should yield the smallest available -> base_2
    auto next = repo.insertModel(baseModel("newfile.g", "base"));
    REQUIRE(next);
    REQUIRE(next->short_name == "base_2");

    // After base_2 is taken, asking again yields base_6
    auto next2 = repo.insertModel(baseModel("newfile2.g", "base"));
    REQUIRE(next2);
    REQUIRE(next2->short_name == "base_6");
}
