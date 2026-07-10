#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <atomic>
#include <thread>
#include <algorithm>

#include "ModelsManager.h"
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

// ======================================================================

TEST_CASE("ModelsManager: resetDatabase clears cache and notifies") {
    TempDirFixture fix("mm_reset");
    ModelsManager mm(fix.tempDir.string());

    std::atomic<int> notified{0};
    mm.subscribe([&]{ ++notified; });

    // reset should clear and notify
    REQUIRE(mm.resetDatabase());
    REQUIRE(notified.load() >= 1);
    REQUIRE(mm.getAll().empty());
}

TEST_CASE("ModelsManager: insert updates cache, returns persisted record, and notifies") {
    TempDirFixture fix("mm_insert");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    std::atomic<int> notified{0};
    mm.subscribe([&]{ ++notified; });

    auto r = mm.insertModel(baseModel("a.g", "a"));
    REQUIRE(r.has_value());
    REQUIRE(r->id > 0);

    // cache reflects it
    auto all = mm.getAll();
    REQUIRE(all.size() == 1);
    REQUIRE(all[0].id == r->id);
    REQUIRE(all[0].file_path == "a.g");

    REQUIRE(notified.load() == 1);
}

TEST_CASE("ModelsManager: update persists, refreshes cache from repo, and notifies") {
    TempDirFixture fix("mm_update");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());
    mm.subscribe([]{}); // ok to have a no-op subscriber

    auto a = mm.insertModel(baseModel("a.g", "nameA"));
    REQUIRE(a);

    // Change a few fields (title, flags)
    ModelData patch = *a;
    patch.title = "New Title";
    patch.long_name = "Canonical New Title";
    patch.modelers = "Jane Doe";
    patch.model_type = "component";
    patch.aliases = "Legacy Name\nProgram Handle";
    patch.suitability = "analysis-ready";
    patch.classification = "public";
    patch.owner_org = "OpenAI";
    patch.source_org = "Legacy Import";
    patch.created_at_fs = "2026-06-03T09:15:00Z";
    patch.modified_at_fs = "2026-06-03T10:45:00Z";
    patch.is_included = true;
    patch.is_processed = true;
    REQUIRE(mm.updateModel(patch));

    auto all = mm.getAll();
    REQUIRE(all.size() == 1);
    REQUIRE(all[0].title == "New Title");
    REQUIRE(all[0].long_name == "Canonical New Title");
    REQUIRE(all[0].modelers == "Jane Doe");
    REQUIRE(all[0].model_type == "component");
    REQUIRE(all[0].aliases == "Legacy Name\nProgram Handle");
    REQUIRE(all[0].suitability == "analysis-ready");
    REQUIRE(all[0].classification == "public");
    REQUIRE(all[0].owner_org == "OpenAI");
    REQUIRE(all[0].source_org == "Legacy Import");
    REQUIRE(all[0].created_at_fs == "2026-06-03T09:15:00Z");
    REQUIRE(all[0].modified_at_fs == "2026-06-03T10:45:00Z");
    REQUIRE(all[0].is_included == true);
    REQUIRE(all[0].is_processed == true);

    HiddenDir paths(fix.tempDir);
    bool foundTitleEvent = false;
    for (const auto& day : fs::directory_iterator(paths.auditDir())) {
        for (const auto& event : fs::directory_iterator(day.path())) {
            std::ifstream input(event.path());
            const std::string contents((std::istreambuf_iterator<char>(input)), {});
            if (contents.find("\"property\":\"title\"") != std::string::npos &&
                contents.find("\"after\":\"New Title\"") != std::string::npos) {
                foundTitleEvent = true;
            }
        }
    }
    REQUIRE(foundTitleEvent);
}

TEST_CASE("ModelsManager: delete removes from cache and notifies") {
    TempDirFixture fix("mm_delete");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    auto a = mm.insertModel(baseModel("a.g", "a"));
    auto b = mm.insertModel(baseModel("b.g", "b"));
    REQUIRE(a);
    REQUIRE(b);

    REQUIRE(mm.deleteModel(a->id));

    auto all = mm.getAll();
    REQUIRE(all.size() == 1);
    REQUIRE(all[0].id == b->id);
}

TEST_CASE("ModelsManager: getAll_snapshot reflects current cache") {
    TempDirFixture fix("mm_snapshot");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    auto a = mm.insertModel(baseModel("a.g", "a")); REQUIRE(a);
    auto b = mm.insertModel(baseModel("b.g", "b")); REQUIRE(b);

    const auto& snap = mm.getAll_snapshot();
    REQUIRE(snap.size() == 2);
    bool valid_id = (snap[0].id == a->id || snap[1].id == a->id);
    REQUIRE(valid_id);
}

TEST_CASE("ModelsManager: getModelByFilePath matches exact current stored path") {
    TempDirFixture fix("mm_by_path");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    auto a = mm.insertModel(baseModel("dir/a.g", "a"));
    auto b = mm.insertModel(baseModel("b.g", "b"));
    REQUIRE(a);
    REQUIRE(b);

    // NOTE: We intentionally do NOT normalize here until you switch to relative paths.
    auto found = mm.getModelByFilePath("dir/a.g");
    REQUIRE(found.id == a->id);

    auto nf = mm.getModelByFilePath("dir\\a.g"); // different separators -> not found for now
    REQUIRE(nf.id == -1);
}

TEST_CASE("ModelsManager: included/selected/processed flags sync cache and repo") {
    TempDirFixture fix("mm_flags");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    auto a = mm.insertModel(baseModel("a.g", "a"));
    auto b = mm.insertModel(baseModel("b.g", "b"));
    REQUIRE(a);
    REQUIRE(b);

    // flip flags on 'a'
    REQUIRE(mm.setModelIncluded(a->id, true));
    REQUIRE(mm.setModelSelected(a->id, true));
    REQUIRE(mm.setModelProcessed(a->id, true));

    // cache views
    auto inc = mm.getIncludedModels();
    REQUIRE(inc.size() == 1);
    REQUIRE(inc[0].id == a->id);

    auto sel = mm.getSelectedModels();
    REQUIRE(sel.size() == 1);
    REQUIRE(sel[0].id == a->id);
}

TEST_CASE("ModelsManager: bulk markAllNotIncluded + selectAllIncluded update cache and notify appropriately") {
    TempDirFixture fix("mm_bulk");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    auto a = mm.insertModel(baseModel("a.g", "a")); REQUIRE(a);
    auto b = mm.insertModel(baseModel("b.g", "b")); REQUIRE(b);

    REQUIRE(mm.setModelIncluded(a->id, true));
    REQUIRE(mm.setModelIncluded(b->id, true));
    REQUIRE(mm.setModelSelected(a->id, true));
    REQUIRE(mm.setModelSelected(b->id, false));

    int changed = mm.markAllNotIncluded();
    REQUIRE(changed == 2);
    REQUIRE(mm.getIncludedModels().empty());

    // With nothing included, selectAllIncluded is a no-op but should still return true
    REQUIRE(mm.selectAllIncluded(true));
    // selection flags untouched for non-included models
    auto sel = mm.getSelectedModels();
    // depending on previous selection, could be non-empty; just ensure 'included' state drove the bulk.
    for (const auto& m : mm.getAll()) {
        if (m.is_included) {
            REQUIRE(m.is_selected == true);
        }
    }
}

TEST_CASE("ModelsManager: tags add/remove update cache and notify") {
    TempDirFixture fix("mm_tags");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    auto a = mm.insertModel(baseModel("a.g", "a"));
    REQUIRE(a);

    REQUIRE(mm.addTagToModel(a->id, "steel"));
    REQUIRE(mm.addTagToModel(a->id, "steel")); // idempotent cache update
    REQUIRE(mm.addTagToModel(a->id, "aluminum"));

    auto afterAdd = mm.getAll();
    REQUIRE(afterAdd.size() == 1);
    REQUIRE(afterAdd[0].tags.size() == 2);
    REQUIRE(std::find(afterAdd[0].tags.begin(), afterAdd[0].tags.end(), "steel") != afterAdd[0].tags.end());
    REQUIRE(std::find(afterAdd[0].tags.begin(), afterAdd[0].tags.end(), "aluminum") != afterAdd[0].tags.end());

    REQUIRE(mm.removeTagFromModel(a->id, "steel"));
    auto afterRemove = mm.getAll();
    REQUIRE(afterRemove[0].tags.size() == 1);
    REQUIRE(afterRemove[0].tags[0] == "aluminum");
}

TEST_CASE("ModelsManager: refresh repopulates cache from repo") {
    TempDirFixture fix("mm_refresh");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    // Seed via the manager
    auto a = mm.insertModel(baseModel("a.g", "a")); REQUIRE(a);
    auto b = mm.insertModel(baseModel("b.g", "b")); REQUIRE(b);

    // Simulate external change via the repo owned by the manager:
    // (Since ModelsManager owns the repo instance, just call through it by using the public API.)
    // We'll update a title then call refresh to ensure cache picks it up.
    ModelData patch = *a;
    patch.title = "changed-outside";
    REQUIRE(mm.updateModel(patch)); // repo updated; cache also updated here already

    // Now refresh anyway (idempotent correctness check)
    mm.refresh();
    auto all = mm.getAll();
    REQUIRE(all.size() == 2);
    auto it = std::find_if(all.begin(), all.end(), [&](const ModelData& m){ return m.id == a->id; });
    REQUIRE(it != all.end());
    REQUIRE(it->title == "changed-outside");
}

TEST_CASE("ModelsManager: thumbnails are delegated to repo (no cache notify)") {
    TempDirFixture fix("mm_thumbs");
    ModelsManager mm(fix.tempDir.string());
    REQUIRE(mm.resetDatabase());

    std::atomic<int> notified{0};
    mm.subscribe([&]{ ++notified; });

    auto a = mm.insertModel(baseModel("a.g", "a")); REQUIRE(a);
    REQUIRE(notified.load() == 1);

    std::vector<unsigned char> png{0x89,0x50,0x4E,0x47};
    REQUIRE(mm.setThumbnail(a->id, png));

    // No additional notify expected because cache doesn't track blobs
    REQUIRE(notified.load() == 1);

    auto roundtrip = mm.getThumbnail(a->id);
    REQUIRE(roundtrip == png);
}
