// Catch2 is used for writing and running unit tests
#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include "AuditLog.h"
#include "Model.h"
#include "LibraryBackup.h"
#include <filesystem>
#include <memory>

#include "ModelTestFixture.h"

const std::string TEST_NAME = "cadventory_ModelTest";

// Tests for initializing the Model and performing basic CRUD operations
TEST_CASE("Model Initialization and CRUD Operations", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Test if the database and its supporting directories are created successfully
    SECTION("Database Initialization") {
        REQUIRE(std::filesystem::exists(fixture.model->getHiddenPaths().modelDb()));
        REQUIRE(std::filesystem::exists(fixture.tempDir / ".cadventory" / "metadata.db"));
    }

    // Test inserting, retrieving, and updating a model
    SECTION("Insert, Retrieve, and Update Model") {
        ModelData newModel = {0, "TestModel", "./path/to/file", "{}", "Test Title", {}, "Author", "/file/path", "Library", true, false, false, {}};
        REQUIRE(fixture.model->insertModel(newModel) == true);

        auto fetchedModel = fixture.model->getModelByFilePath(newModel.file_path);
        REQUIRE(fetchedModel.short_name == "TestModel");

        fetchedModel.short_name = "UpdatedModel";
        REQUIRE(fixture.model->updateModel(fetchedModel.id, fetchedModel) == true);

        auto updatedModel = fixture.model->getModelById(fetchedModel.id);
        REQUIRE(updatedModel.has_value());
        REQUIRE(updatedModel.value().short_name == "UpdatedModel");
    }

    // Test deleting a model and verifying its deletion
    SECTION("Delete Model and Verify Deletion") {
        ModelData delModel = {0, "DeleteModel", "./delete/path", "{}", "Delete Title", {}, "Author", "/delete/file/path", "Library", false, false, false, {}};
        REQUIRE(fixture.model->insertModel(delModel) == true);

        auto fetchedModel = fixture.model->getModelByFilePath(delModel.file_path);
        REQUIRE(fixture.model->deleteModel(fetchedModel.id) == true);
        REQUIRE_FALSE(fixture.model->modelExists(fetchedModel.id));
    }
}

TEST_CASE("LibraryBackup captures CADventory-managed state", "[Backup]") {
    ModelTestFixture fixture(TEST_NAME);
    ModelData modelData = {0, "BackupModel", "./model.g", "{}", "Original", {}, "Author",
                           "/models/backup.g", "Library", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(modelData));
    const int modelId = fixture.model->getModelByFilePath(modelData.file_path).id;
    REQUIRE(fixture.model->setPropertyForModel(modelId, "long_name", "Backed Up Name"));

    const fs::path managedData = fs::path(fixture.model->getHiddenPaths().dataDir()) / "cache.txt";
    std::filesystem::create_directories(managedData.parent_path());
    std::ofstream(managedData) << "managed output";

    const fs::path geometry = fixture.tempDir / "source-model.g";
    std::ofstream(geometry) << "not managed by CADventory";

    const auto backup = LibraryBackup::create(fixture.model->getHiddenPaths(),
                                               fixture.tempDir / "backups");
    REQUIRE(backup.success);
    REQUIRE(std::filesystem::exists(backup.directory / ".cadventory" / "metadata.db"));
    REQUIRE(std::filesystem::exists(backup.directory / ".cadventory" / "jobs" / "jobs.db"));
    REQUIRE(std::filesystem::exists(backup.directory / ".cadventory" / "data" / "cache.txt"));
    REQUIRE(std::filesystem::exists(backup.directory / ".cadventory" / "audit"));
    REQUIRE_FALSE(std::filesystem::exists(backup.directory / "source-model.g"));

    Model restored(backup.directory.string());
    const auto restoredModel = restored.getModelByFilePath(modelData.file_path);
    REQUIRE(restoredModel.id > 0);
    REQUIRE(restoredModel.long_name == "Backed Up Name");
}

// Tests for verifying data roles and utility functions
TEST_CASE("Model Data Roles and Utility Functions", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    ModelData testModel = {0, "RoleTest", "./path/to/file", "{}", "Role Title", {}, "Author", "/file/path", "Library", true, false, false, {}};
    REQUIRE(fixture.model->insertModel(testModel) == true);

    QModelIndex index = fixture.model->index(0, 0);
    REQUIRE(index.isValid());

    // Verify role names mapping
    SECTION("Role Mapping") {
        auto roles = fixture.model->roleNames();
        REQUIRE(roles[Model::IdRole] == "id");
        REQUIRE(roles[Model::ShortNameRole] == "short_name");
    }

    // Test the data retrieval for specific roles
    SECTION("Verify Data for Roles") {
        REQUIRE(fixture.model->data(index, Model::ShortNameRole).toString().toStdString() == "RoleTest");
        REQUIRE(fixture.model->data(index, Model::TitleRole).toString().toStdString() == "Role Title");
    }
}

// Tests for managing objects associated with models
TEST_CASE("Object Management and Transactions", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    ModelData testModel = {0, "ObjectTest", "./path/to/file", "{}", "Object Title", {}, "Author", "/file/path", "Library", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(testModel) == true);

    auto fetchedModel = fixture.model->getModelByFilePath(testModel.file_path);
    REQUIRE(fetchedModel.id > 0);

    // Test inserting and retrieving objects
    SECTION("Insert and Retrieve Objects") {
        ObjectData obj1 = {0, fetchedModel.id, "Object1", -1, false};
        int parentId = fixture.model->insertObject(obj1);
        REQUIRE(parentId != -1);

        ObjectData obj2 = {0, fetchedModel.id, "Object2", parentId, true};
        REQUIRE(fixture.model->insertObject(obj2) != -1);

        auto objects = fixture.model->getObjectsForModel(fetchedModel.id);
        REQUIRE(objects.size() == 2);
        REQUIRE(objects[0].name == "Object1");
    }

    // Test updating and deleting objects
    SECTION("Update and Delete Objects") {
        ObjectData obj = {0, fetchedModel.id, "ToUpdate", -1, false};
        int objId = fixture.model->insertObject(obj);
        REQUIRE(objId != -1);

        obj.name = "UpdatedObject";
        REQUIRE(fixture.model->updateObject(obj) == true);

        // Fetch the object again to validate the update
        auto updatedObj = fixture.model->getObjectById(objId);
        REQUIRE(updatedObj.object_id == objId);  // Ensure correct object is fetched
        REQUIRE(updatedObj.name == "ToUpdate");  // Validate the updated name

        // Delete objects for the model and verify
        REQUIRE(fixture.model->deleteObjectsForModel(fetchedModel.id) == true);
        REQUIRE(fixture.model->getObjectsForModel(fetchedModel.id).empty());
    }

    // Test transaction handling for object updates
    SECTION("Transaction Handling") {
        fixture.model->beginTransaction();
        REQUIRE(fixture.model->updateObjectSelection(0, true) == true);
        fixture.model->commitTransaction();

        auto selectedObjects = fixture.model->getSelectedObjectsForModel(fetchedModel.id);
        REQUIRE(selectedObjects.empty());
    }
}


// Test cases for advanced model features like handling tags
TEST_CASE("Advanced Model Features", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Verify adding and retrieving tags for a model
    SECTION("Handle Tags") {
        ModelData tagModel = {0, "TagModel", "./path", "{}", "Tag Title", {}, "Author", "/file/path", "Library", false, false, false, {}};
        REQUIRE(fixture.model->insertModel(tagModel) == true);

        auto modelId = fixture.model->getModelByFilePath(tagModel.file_path).id;
        REQUIRE(fixture.model->addTagToModel(modelId, "Tag1") == true);
        REQUIRE(fixture.model->addTagToModel(modelId, "Tag2") == true);

        auto tags = fixture.model->getTagsForModel(modelId);
        REQUIRE(tags.size() == 2); // Ensure two tags are added
    }
}

// Test cases for hashing functionality in the Model class
TEST_CASE("Model: Hashing Functionality", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Test hashing a valid file
    SECTION("Hashing a Valid File") {
        // Create dummy files for hashing tests
        std::filesystem::path validFilePath = fixture.tempDir / "valid_file.txt";
        std::ofstream validFile(validFilePath);
        validFile << "Test content for hashing.";
        validFile.close();

        int hashValue = fixture.model->hashModel(validFilePath.string());
        REQUIRE(hashValue != 0); // Ensure a valid hash is produced
    }

    // Test hashing an empty file
    SECTION("Hashing an Empty File") {        
        std::filesystem::path emptyFilePath = fixture.tempDir / "empty_file.txt";
        std::ofstream emptyFile(emptyFilePath);
        emptyFile.close();

        int hashValue = fixture.model->hashModel(emptyFilePath.string());
        REQUIRE(hashValue != 0); // Hashing should still produce a valid value
    }

    // Test hashing a nonexistent file
    SECTION("Hashing a Nonexistent File") {
        std::filesystem::path invalidFilePath = fixture.tempDir / "nonexistent.txt";

        int hashValue = fixture.model->hashModel(invalidFilePath.string());
        REQUIRE(hashValue == 0); // Nonexistent file should return a hash of 0
    }
}

// Test cases for printing model details to output
TEST_CASE("Model: Print Functionality", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Ensure the printModel function runs without crashing
    SECTION("Print Model") {
        ModelData testModel = {1, "TestModel", "./primary/file", "{}", "Title", {}, "Author", "/path", "Library", true, false, false, {}};
        REQUIRE_NOTHROW(fixture.model->printModel(testModel));
    }
}

// Test cases for refreshing data and checking roles
TEST_CASE("Model: Refresh Data and Roles", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Verify that refreshing model data does not throw errors
    SECTION("Refresh Model Data") {
        REQUIRE_NOTHROW(fixture.model->refreshModelData());
    }

    // Ensure role names map to expected values
    SECTION("Role Names") {
        auto roles = fixture.model->roleNames();
        REQUIRE(roles[Model::IdRole] == "id");
        REQUIRE(roles[Model::ShortNameRole] == "short_name");
    }
}

// Test cases for setting data and checking item flags
TEST_CASE("Model: Set Data and Flags", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    ModelData testModel = {0, "SelectableModel", "./file", "{}", "Title", {}, "Author", "/path", "Library", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(testModel));

    QModelIndex index = fixture.model->index(0, 0);
    REQUIRE(index.isValid()); // Ensure the index is valid

    // Verify setting data for the IsSelectedRole role
    SECTION("Set Data for IsSelectedRole") {
        REQUIRE(fixture.model->setData(index, true, Model::IsSelectedRole) == true);
        REQUIRE(fixture.model->data(index, Model::IsSelectedRole).toBool() == true);
    }

    // Verify setting data for the IsIncludedRole role
    SECTION("Set Data for IsIncludedRole") {
        REQUIRE(fixture.model->setData(index, true, Model::IsIncludedRole) == true);
        REQUIRE(fixture.model->data(index, Model::IsIncludedRole).toBool() == true);
    }

    // Test invalid index handling
    SECTION("Invalid Index Handling") {
        QModelIndex invalidIndex;
        REQUIRE(fixture.model->setData(invalidIndex, true, Model::IsSelectedRole) == false);
    }

    // Verify item flags for valid and invalid indices
    SECTION("Item Flags") {
        auto flags = fixture.model->flags(index);
        REQUIRE(flags == (Qt::ItemIsEnabled | Qt::ItemIsSelectable));

        QModelIndex invalidIndex;
        REQUIRE(fixture.model->flags(invalidIndex) == Qt::NoItemFlags);
    }
}

// Test case for retrieving models marked as "selected"
TEST_CASE("Model: Get Selected Models", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Insert sample models into the database
    ModelData model1 = {0, "Model1", "./file1", "{}", "Title1", {}, "Author1", "/path1", "Library1", true, false, false, {}};
    ModelData model2 = {0, "Model2", "./file2", "{}", "Title2", {}, "Author2", "/path2", "Library2", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(model1)); // Ensure model1 is inserted successfully
    REQUIRE(fixture.model->insertModel(model2)); // Ensure model2 is inserted successfully

    // Verify that only the "selected" model is retrieved
    SECTION("Retrieve Only Selected Models") {
        auto selectedModels = fixture.model->getSelectedModels();
        REQUIRE(selectedModels.size() == 1); // Only one model should be selected
        REQUIRE(selectedModels[0].short_name == "Model1"); // Verify the selected model
    }
}

// Test case for updating the parent ID of an object
TEST_CASE("Model: Update Object Parent ID", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // create a model in db
    ModelData md{0, "Model", "./pf", "{}", "Title", {}, "Author", "/path/A", "Library", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(md));
    int modelId = fixture.model->getModelByFilePath(md.file_path).id;
    REQUIRE(modelId > 0);

    // create a 'parent' object
    int parentId = fixture.model->insertObject({0, modelId, "Parent", -1, false});
    REQUIRE(parentId != -1);

    // create a 'child' object (no parent yet)
    int childId = fixture.model->insertObject({0, modelId, "Child", -1, false});
    REQUIRE(childId != -1);

    SECTION("Update Parent ID Successfully") {
        REQUIRE(fixture.model->updateObjectParentId(childId, parentId) == true);

        auto updated = fixture.model->getObjectById(childId);
        REQUIRE(updated.object_id == childId);
        REQUIRE(updated.parent_object_id == parentId);      // Confirm the parent ID update
    }
}

// Test case for deleting and recreating database tables
TEST_CASE("Model: Delete Tables", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Verify that tables can be deleted and recreated without errors
    SECTION("Successfully Delete and Recreate Tables") {
        REQUIRE(fixture.model->deleteTables() == true); // Tables deleted successfully
        REQUIRE_NOTHROW(fixture.model->resetDatabase()); // Tables recreated without exceptions
    }
}

// Test case for retrieving all tags in the database
TEST_CASE("Model: Get All Tags", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // create a model
    ModelData md1{0, "ModelA", "./pfA", "{}", "Title", {}, "Author", "/path/A", "Library", false, false, false, {}};
    ModelData md2{0, "ModelB", "./pfB", "{}", "Title", {}, "Author", "/path/B", "Library", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(md1));
    REQUIRE(fixture.model->insertModel(md2));
    int modelId1 = fixture.model->getModelByFilePath(md1.file_path).id;
    int modelId2 = fixture.model->getModelByFilePath(md2.file_path).id;
    REQUIRE(modelId1 > 0);
    REQUIRE(modelId2 > 0);

    // Insert sample tags into the database
    REQUIRE(fixture.model->addTagToModel(modelId1, "Tag1") == true); // Add "Tag1" to model1
    REQUIRE(fixture.model->addTagToModel(modelId2, "Tag2") == true); // Add "Tag2" to model2

    // Verify that all tags are retrieved correctly
    SECTION("Retrieve All Tags") {
        auto tags = fixture.model->getAllTags();
        REQUIRE(tags.size() == 2); // Ensure two tags are retrieved
        REQUIRE(std::find(tags.begin(), tags.end(), "Tag1") != tags.end()); // Check for "Tag1"
        REQUIRE(std::find(tags.begin(), tags.end(), "Tag2") != tags.end()); // Check for "Tag2"
    }
}

// Test case for removing tags from a model
TEST_CASE("Model: Remove Tags from Model", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Insert a sample model and add tags to it
    ModelData modelData = {0, "ModelWithTags", "./file", "{}", "Title", {}, "Author", "/path", "Library", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(modelData)); // Insert model

    auto modelId = fixture.model->getModelByFilePath(modelData.file_path).id;
    REQUIRE(fixture.model->addTagToModel(modelId, "Tag1") == true); // Add "Tag1"
    REQUIRE(fixture.model->addTagToModel(modelId, "Tag2") == true); // Add "Tag2"

    // Verify that a specific tag can be removed
    SECTION("Remove a Specific Tag") {
        REQUIRE(fixture.model->removeTagFromModel(modelId, "Tag1") == true); // Remove "Tag1"

        auto tags = fixture.model->getTagsForModel(modelId);
        REQUIRE(tags.size() == 1); // Only one tag should remain
        REQUIRE(tags[0] == "Tag2"); // Verify remaining tag
    }

    // Verify that all tags can be removed
    SECTION("Remove All Tags") {
        REQUIRE(fixture.model->removeAllTagsFromModel(modelId) == true); // Remove all tags

        auto tags = fixture.model->getTagsForModel(modelId);
        REQUIRE(tags.empty()); // Ensure no tags remain
    }
}

// Test case for getting and setting model properties
TEST_CASE("Model: Get and Set Properties", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Insert a sample model into the database
    ModelData modelData = {0, "PropertyModel", "./file", "{}", "Title", {}, "Author", "/path", "Library", false, false, false, {}};
    modelData.long_name = "Long Title";
    modelData.modelers = "Modeler Team";
    modelData.model_type = "Vehicle";
    modelData.aliases = "Legacy Title\nArchive Handle";
    modelData.suitability = "Reference";
    modelData.classification = "Unclassified";
    modelData.owner_org = "OpenAI";
    modelData.source_org = "BRL-CAD";
    modelData.created_at_fs = "2026-06-03T07:00:00Z";
    modelData.modified_at_fs = "2026-06-03T08:30:00Z";
    REQUIRE(fixture.model->insertModel(modelData)); // Ensure model is inserted successfully

    auto modelId = fixture.model->getModelByFilePath(modelData.file_path).id;

    // Verify that properties can be retrieved correctly
    SECTION("Get Properties for Model") {
        auto properties = fixture.model->getPropertiesForModel(modelId);
        REQUIRE(properties["short_name"] == "PropertyModel");
        REQUIRE(properties["long_name"] == "Long Title");
        REQUIRE(properties["modelers"] == "Modeler Team");
        REQUIRE(properties["model_type"] == "Vehicle");
        REQUIRE(properties["aliases"] == "Legacy Title\nArchive Handle");
        REQUIRE(properties["suitability"] == "Reference");
        REQUIRE(properties["classification"] == "Unclassified");
        REQUIRE(properties["owner_org"] == "OpenAI");
        REQUIRE(properties["source_org"] == "BRL-CAD");
        REQUIRE(properties["created_at_fs"] == "2026-06-03T07:00:00Z");
        REQUIRE(properties["modified_at_fs"] == "2026-06-03T08:30:00Z");
    }

    // Verify that a property can be updated successfully
    SECTION("Set a Property for Model") {
        REQUIRE(fixture.model->setPropertyForModel(modelId, "long_name", "New Title") == true);
        REQUIRE(fixture.model->setPropertyForModel(modelId, "modelers", "New Modelers") == true);
        REQUIRE(fixture.model->setPropertyForModel(modelId, "model_type", "Assembly") == true);
        REQUIRE(fixture.model->setPropertyForModel(modelId, "aliases", "Alias One\nAlias Two") == true);
        REQUIRE(fixture.model->setPropertyForModel(modelId, "suitability", "Analysis Ready") == true);
        REQUIRE(fixture.model->setPropertyForModel(modelId, "classification", "Public Release") == true);
        REQUIRE(fixture.model->setPropertyForModel(modelId, "owner_org", "NIST") == true);
        REQUIRE(fixture.model->setPropertyForModel(modelId, "source_org", "Imported Archive") == true);

        auto updatedProperties = fixture.model->getPropertiesForModel(modelId);
        REQUIRE(updatedProperties["long_name"] == "New Title");
        REQUIRE(updatedProperties["modelers"] == "New Modelers");
        REQUIRE(updatedProperties["model_type"] == "Assembly");
        REQUIRE(updatedProperties["aliases"] == "Alias One\nAlias Two");
        REQUIRE(updatedProperties["suitability"] == "Analysis Ready");
        REQUIRE(updatedProperties["classification"] == "Public Release");
        REQUIRE(updatedProperties["owner_org"] == "NIST");
        REQUIRE(updatedProperties["source_org"] == "Imported Archive");

        auto updatedModel = fixture.model->getModelById(modelId);
        REQUIRE(updatedModel.has_value());
        REQUIRE(updatedModel->title == "New Title");
        REQUIRE(updatedModel->author == "New Modelers");
        REQUIRE(updatedModel->aliases == "Alias One\nAlias Two");
        REQUIRE(updatedModel->suitability == "Analysis Ready");
        REQUIRE(updatedModel->classification == "Public Release");
        REQUIRE(updatedModel->owner_org == "NIST");
        REQUIRE(updatedModel->source_org == "Imported Archive");
        REQUIRE(updatedModel->created_at_fs == "2026-06-03T07:00:00Z");
        REQUIRE(updatedModel->modified_at_fs == "2026-06-03T08:30:00Z");

        const auto auditRoot = fixture.model->getHiddenPaths().auditDir();
        REQUIRE(std::filesystem::exists(auditRoot));
        bool foundLongNameEvent = false;
        for (const auto& day : std::filesystem::directory_iterator(auditRoot)) {
            if (!day.is_directory())
                continue;
            for (const auto& event : std::filesystem::directory_iterator(day.path())) {
                if (!event.is_regular_file() || event.path().extension() != ".json")
                    continue;
                std::ifstream input(event.path());
                const std::string contents((std::istreambuf_iterator<char>(input)), {});
                if (contents.find("\"property\":\"long_name\"") != std::string::npos &&
                    contents.find("\"before\":\"Long Title\"") != std::string::npos &&
                    contents.find("\"after\":\"New Title\"") != std::string::npos) {
                    foundLongNameEvent = true;
                }
            }
        }
        REQUIRE(foundLongNameEvent);

        const auto auditEvents = AuditLog(auditRoot).readEvents();
        REQUIRE(auditEvents.invalidEvents == 0);
        REQUIRE_FALSE(auditEvents.events.empty());
        bool foundParsedLongNameEvent = false;
        for (const auto& event : auditEvents.events) {
            if (event.property == "long_name" && event.after == "New Title")
                foundParsedLongNameEvent = true;
        }
        REQUIRE(foundParsedLongNameEvent);
    }

    // Verify that attempting to set an invalid property fails
    SECTION("Fail to Set Invalid Property") {
        REQUIRE(fixture.model->setPropertyForModel(modelId, "invalid_property", "value") == false); // Invalid property
    }
}

// Test case for retrieving models marked as "included"
TEST_CASE("Model: Get Included Models", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Insert sample models into the database
    ModelData model1 = {0, "IncludedModel1", "./file1", "{}", "Title1", {}, "Author1", "/path1", "Library1", false, false, true, {}};
    ModelData model2 = {0, "ExcludedModel", "./file2", "{}", "Title2", {}, "Author2", "/path2", "Library2", false, false, false, {}};
    ModelData model3 = {0, "IncludedModel2", "./file3", "{}", "Title3", {}, "Author3", "/path3", "Library3", false, false, true, {}};
    REQUIRE(fixture.model->insertModel(model1));
    REQUIRE(fixture.model->insertModel(model2));
    REQUIRE(fixture.model->insertModel(model3));

    // Verify that only "included" models are retrieved
    SECTION("Retrieve Included Models") {
        auto includedModels = fixture.model->getIncludedModels();
        REQUIRE(includedModels.size() == 2); // Only two models are marked as "included"

        std::vector<std::string> includedNames;
        for (const auto& modelData : includedModels) {
            includedNames.push_back(modelData.short_name);
        }

        REQUIRE(std::find(includedNames.begin(), includedNames.end(), "IncludedModel1") != includedNames.end());
        REQUIRE(std::find(includedNames.begin(), includedNames.end(), "IncludedModel2") != includedNames.end());
        REQUIRE(std::find(includedNames.begin(), includedNames.end(), "ExcludedModel") == includedNames.end());
    }
}

// Test case for checking if a file is included in the model database
TEST_CASE("Model: Is File Included", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Insert a sample model into the database
    ModelData modelData = {0, "IncludedFileModel", "./file_path", "{}", "Title", {}, "Author", "/path", "Library", false, false, true, {}};
    REQUIRE(fixture.model->insertModel(modelData));

    // Verify that a file is correctly identified as "included"
    SECTION("Check Included File") {
        REQUIRE(fixture.model->isFileIncluded("/path") == true); // File is included
    }

    // Verify that a non-existent file is not identified as "included"
    SECTION("Check Non-Included File") {
        REQUIRE(fixture.model->isFileIncluded("/nonexistent_path") == false); // File is not included
    }
}

// Test case for retrieving models that are included but not processed
TEST_CASE("Model: Get Included Not Processed Models", "[Model]") {
    ModelTestFixture fixture(TEST_NAME);

    // Insert sample models into the database
    ModelData model1 = {0, "IncludedNotProcessed1", "./file1", "{}", "Title1", {}, "Author1", "/path1", "Library1", false, false, true, {}};
    ModelData model2 = {0, "IncludedProcessed", "./file2", "{}", "Title2", {}, "Author2", "/path2", "Library2", false, true, true, {}};
    ModelData model3 = {0, "ExcludedNotProcessed", "./file3", "{}", "Title3", {}, "Author3", "/path3", "Library3", false, false, false, {}};
    REQUIRE(fixture.model->insertModel(model1));
    REQUIRE(fixture.model->insertModel(model2));
    REQUIRE(fixture.model->insertModel(model3));

    // Verify that only models marked as "included" and "not processed" are retrieved
    SECTION("Retrieve Included Not Processed Models") {
        auto notProcessedModels = fixture.model->getIncludedNotProcessedModels();
        REQUIRE(notProcessedModels.size() == 1); // Only one model matches the criteria
        REQUIRE(notProcessedModels[0].short_name == "IncludedNotProcessed1"); // Verify the model
    }
}
