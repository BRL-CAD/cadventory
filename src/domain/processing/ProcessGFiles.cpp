#include "ProcessGFiles.h"
#include <brlcad/ged.h>
#include <brlcad/bu/uuid.h>
#include <QDebug>
#include <iostream>
#include <QProcess>
#include <QSettings>
#include <QFile>
#include <QDir>
#include "config.h"
#include <string>
#include <cstring>    // for strlen
#include <algorithm>
#include <filesystem>

// Helper function to truncate a path if it exceeds maxLen (default 50 characters).
// It keeps the first part and the last part of the path and places "..." in between.
static std::string truncatePath(const std::string& path, size_t maxLen = 20) {
    if (path.length() <= maxLen) {
        return path;
    }
    // Calculate how many characters to show from the front and back.
    size_t frontLen = (maxLen - 3) / 2;
    size_t backLen = maxLen - 3 - frontLen;
    return path.substr(0, frontLen) + "..." + path.substr(path.length() - backLen);
}

ProcessGFiles::ProcessGFiles(Model* model)
    : model(model)
{
}

std::optional<ModelData> ProcessGFiles::processGFile(const ModelData& modelData)
{
    // Ensure we have a file path
    if (modelData.file_path.empty()) {
        qDebug() << "[ProcessGFiles::processGFile] No file path provided. Aborting.";
        return std::nullopt;
    }

    // Attempt to open the BRL-CAD database
    const char* db_filename = modelData.file_path.c_str();
    std::unique_ptr<struct ged, decltype(&ged_close)> gedp(
        ged_open("db", db_filename, 0),
        ged_close
    );
    if (!gedp) {
        qDebug() << "[ProcessGFiles::processGFile] Unable to open BRL-CAD database at path:"
            << QString::fromStdString(modelData.file_path);
        return std::nullopt;
    }

    // Create a working copy of the modelData
    ModelData updatedModelData = modelData;

    // Extract title
    extractTitle(updatedModelData, gedp.get());
    qDebug() << "[ProcessGFiles::processGFile] Title extracted:" << QString::fromStdString(updatedModelData.title);

    // Extract object data
    extractObjects(updatedModelData, gedp.get());
    std::vector<ObjectData> allObjects = model->getObjectsForModel(updatedModelData.id);
    if (allObjects.empty()) {
        qDebug() << "[ProcessGFiles::processGFile] No objects found for model ID:" << updatedModelData.id;
        return std::nullopt;
    }

    // Attempt to determine which object is our primary
    std::string primaryObject;
    std::vector<ObjectData> selectedObjects = model->getSelectedObjectsForModel(updatedModelData.id);
    if (!selectedObjects.empty()) {
        // use first selected object
        primaryObject = selectedObjects.front().name;
    } else {
        // sanity: extractObjects() should already have handled finding the selected object or
        //          pattern matching common 'all' objects
        primaryObject = allObjects.front().name;

        // if we don't have a selected object, go ahead and add this one
        model->updateObjectSelection(allObjects.front().object_id, true);
    }
    /*** Queue separate job for thumbnail generation - inidcated with 'getAllNeededDirectives()'
    qDebug() << "[ProcessGFiles::processGFile] Using \'" << primaryObject << "\' for thumbnail.";

    // Attempt to generate our thumbnail
    if (!generateThumbnail(updatedModelData, objectNameForThumbnail)) {
        qDebug() << "[ProcessGFiles::processGFile] Thumbnail generation failed for model ID:" << updatedModelData.id;
        // Not a hard fail, move on
    }
    ***/
    // Generate a UUID with this .g + primaryObject
    updatedModelData.is_processed_dir = generateUUID(gedp.get(), primaryObject);

    // Create or update our model
    updatedModelData.is_processed = true;
    auto existing = model->getModelById(updatedModelData.id);
    bool success = false;
    if (existing.has_value()) {
        success = model->updateModel(updatedModelData.id, updatedModelData);
    } else {
        success = model->insertModel(updatedModelData);
    }
    if (!success) {
        qDebug() << "[ProcessGFiles::processGFile] Failed to process in database for model ID:" << updatedModelData.id;
        return std::nullopt;
    }

    return updatedModelData;
}

std::vector<std::string> ProcessGFiles::getAllNeededDirectives() {
    return {/*"dummy",*/ "thumb"};
}

std::string ProcessGFiles::generateUUID(struct ged *gedp, const std::string &primaryObj) {
    if (!gedp || !gedp->dbip) 
        return "";

    // build namespace using primaryObj
    const uint8_t arbitrary[16] = {0x4a, 0x3e, 0x13, 0x3f, 0x1a, 0xfc, 0x4d, 0x6c, 0x9a, 0xdd, 0x82, 0x9b, 0x7b, 0xb6, 0xc6, 0xc1};
    uint8_t ns_uuid[16];
    bu_uuid_create(ns_uuid, 
                   primaryObj.size(), reinterpret_cast<const uint8_t*>(primaryObj.data()),
                   arbitrary);

    // gather all db directory's
    std::vector<directory*> dirs;
    struct directory *dp = RT_DIR_NULL;
    FOR_ALL_DIRECTORY_START(dp, gedp->dbip) {
        dirs.push_back(dp);
    } FOR_ALL_DIRECTORY_END;
    // sort on d_namep for repeatable ordering
    std::sort(dirs.begin(), dirs.end(),
        // O(n log n)
        [](const directory* a, const directory* b) {
            const char *an = (a && a->d_namep) ? a->d_namep : "";
            const char *bn = (b && b->d_namep) ? b->d_namep : "";
            return std::strcmp(an, bn) < 0;
        });

    // hash all our objects together
    struct bu_data_hash_state *hs = bu_data_hash_create();
    if (!hs) 
        return ""; // allocation failure?
    for (auto *d : dirs) {
        const char *name = (d && d->d_namep) ? d->d_namep : "";
        bu_data_hash_update(hs, name, std::strlen(name));

        // External object bytes
        struct bu_external ext = BU_EXTERNAL_INIT_ZERO;
        if (d && db_get_external(&ext, d, gedp->dbip) == 0) {
            if (ext.ext_buf && ext.ext_nbytes > 0) {
                bu_data_hash_update(hs, ext.ext_buf, ext.ext_nbytes);
            }
            bu_free_external(&ext);
        } // else silently skip errors
    }
    unsigned long long digest = bu_data_hash_val(hs);
    bu_data_hash_destroy(hs);

    // finally create the uuid
    uint8_t uuid[16];
    uint8_t digest_buf[8];
    for (int i = 0; i < 8; ++i) {
        // pack into 8 bytes big endian
        digest_buf[7 - i] = static_cast<uint8_t>(digest >> (i * 8));
    }
    bu_uuid_create(uuid, sizeof(digest_buf), digest_buf, ns_uuid);

    // return human-readable string
    uint8_t uuid_str[37] = {0};
    bu_uuid_encode(uuid, uuid_str);
    return std::string(reinterpret_cast<char*>(uuid_str));
}

void ProcessGFiles::extractTitle(ModelData& modelData, struct ged* gedp)
{
    if (gedp && gedp->dbip && gedp->dbip->dbi_title) {
        std::string title(gedp->dbip->dbi_title);
        modelData.title = title;
        qDebug() << "[ProcessGFiles::extractTitle] Database title found:" << QString::fromStdString(title);
    }
    else {
        modelData.title = "(Untitled)";
        qDebug() << "[ProcessGFiles::extractTitle] No title found in database. Using '(Untitled)'";
    }
}

void ProcessGFiles::extractObjects(ModelData& modelData, struct ged* gedp)
{
    qDebug() << "[ProcessGFiles::extractObjects] Started for model ID:" << modelData.id;

    if (!gedp || !gedp->dbip) {
        std::cerr << "[ProcessGFiles::extractObjects] Invalid ged pointer." << std::endl;
        qDebug() << "[ProcessGFiles::extractObjects] Invalid ged pointer. Cannot process objects.";
        return;
    }

    // Initialize the directory pointer to list top-level objects
    struct directory** dir = nullptr;
    qDebug() << "[ProcessGFiles::extractObjects] Listing top-level objects from the database.";
    size_t dir_count = db_ls(gedp->dbip, DB_LS_TOPS, nullptr, &dir);
    if (dir_count == 0) {
        std::cerr << "[ProcessGFiles::extractObjects] No objects found in database." << std::endl;
        qDebug() << "[ProcessGFiles::extractObjects] No objects found in database for model ID:" << modelData.id;
        return;
    }

    qDebug() << "[ProcessGFiles::extractObjects] Number of top-level objects found:" << dir_count;

    std::vector<std::string> tops_elements;
    for (size_t i = 0; i < dir_count; ++i) {
        tops_elements.push_back(dir[i]->d_namep);
    }

    std::string model_short_name = modelData.short_name;
    std::vector<std::string> objects_to_try = {
        "all", "all.g", model_short_name,
        model_short_name + ".g", model_short_name + ".c"
    };
    std::string selected_object_name;

    // Check if any objects_to_try are in tops_elements
    for (const auto& obj_name : objects_to_try) {
        if (std::find(tops_elements.begin(), tops_elements.end(), obj_name) != tops_elements.end()) {
            selected_object_name = obj_name;
            break;
        }
    }

    // If no match, select the first top-level object
    if (selected_object_name.empty() && !tops_elements.empty()) {
        selected_object_name = tops_elements.front();
    }

    qDebug() << "[ProcessGFiles::extractObjects] Selected object for thumbnail:" << QString::fromStdString(selected_object_name);

    // Iterate over the directory entries for top-level objects
    for (size_t i = 0; i < dir_count; ++i) {
        std::string object_name(dir[i]->d_namep);
        qDebug() << "[ProcessGFiles::extractObjects] Found top-level object name:" << QString::fromStdString(object_name);

        // Create ObjectData for the top-level object
        ObjectData topLevelObjData;

        topLevelObjData.model_id = modelData.id;
        topLevelObjData.name = object_name;
        topLevelObjData.parent_object_id = -1; // -1 indicates no parent
        topLevelObjData.is_selected = (object_name == selected_object_name);

        qDebug() << "[ProcessGFiles::extractObjects] Inserting top-level object - "
            << "Model ID:" << topLevelObjData.model_id
            << ", Name:" << QString::fromStdString(topLevelObjData.name)
            << ", Parent Object ID:" << topLevelObjData.parent_object_id
            << ", is_selected:" << topLevelObjData.is_selected;

        int insertedTopLevelObjectId = model->insertObject(topLevelObjData);
        if (insertedTopLevelObjectId == -1) {
            qDebug() << "[ProcessGFiles::extractObjects] Failed to insert top-level object:"
                << QString::fromStdString(topLevelObjData.name)
                << "for model ID:" << topLevelObjData.model_id;
            continue;
        }
        else {
            topLevelObjData.object_id = insertedTopLevelObjectId;
            qDebug() << "[ProcessGFiles::extractObjects] Successfully inserted top-level object:"
                << QString::fromStdString(topLevelObjData.name)
                << "with ID:" << insertedTopLevelObjectId << "for model ID:" << topLevelObjData.model_id;
        }

        // If this top-level object is a combination, retrieve and insert its children
        if (dir[i]->d_flags & RT_DIR_COMB) {
            qDebug() << "[ProcessGFiles::extractObjects] Object" << QString::fromStdString(object_name) << "is a combination. Retrieving children.";
            insertChildObjects(modelData, gedp, topLevelObjData, selected_object_name);
        }
        else {
            qDebug() << "[ProcessGFiles::extractObjects] Object" << QString::fromStdString(object_name) << "is a primitive. No child objects to insert.";
        }
    }

    // Free the directory list for top-level objects
    bu_free(dir, "free directory list");
    qDebug() << "[ProcessGFiles::extractObjects] Completed for model ID:" << modelData.id;
}

void ProcessGFiles::insertChildObjects(ModelData& modelData, struct ged* gedp, const ObjectData& parentObjData, const std::string& selected_object_name)
{
    qDebug() << "[ProcessGFiles::insertChildObjects] Started for parent object ID:" << parentObjData.object_id << "Name:" << QString::fromStdString(parentObjData.name);

    struct directory* parent_dir = db_lookup(gedp->dbip, parentObjData.name.c_str(), LOOKUP_QUIET);
    if (!parent_dir) {
        qDebug() << "[ProcessGFiles::insertChildObjects] Parent object" << QString::fromStdString(parentObjData.name) << "not found in database.";
        return;
    }

    if (!(parent_dir->d_flags & RT_DIR_COMB)) {
        qDebug() << "[ProcessGFiles::insertChildObjects] Parent object" << QString::fromStdString(parentObjData.name) << "is not a combination. No children to insert.";
        return;
    }

    struct rt_db_internal intern;
    struct rt_comb_internal* comb;
    if (rt_db_get_internal(&intern, parent_dir, gedp->dbip, nullptr, &rt_uniresource) < 0) {
        qDebug() << "[ProcessGFiles::insertChildObjects] Error retrieving internal representation for object" << QString::fromStdString(parentObjData.name);
        return;
    }

    comb = static_cast<struct rt_comb_internal*>(intern.idb_ptr);

    if (!comb->tree) {
        qDebug() << "[ProcessGFiles::insertChildObjects] Combination" << QString::fromStdString(parentObjData.name) << "has no children.";
        rt_db_free_internal(&intern);
        return;
    }

    // Retrieve child objects
    std::vector<std::string> children;
    db_tree_list_comb_children(comb->tree, children);

    qDebug() << "[ProcessGFiles::insertChildObjects] Number of children found for object" << QString::fromStdString(parentObjData.name) << ":" << children.size();

    // Insert each child object into the database
    for (const auto& child_name : children) {
        qDebug() << "[ProcessGFiles::insertChildObjects] Processing child object name:" << QString::fromStdString(child_name);

        // Lookup the child's directory entry
        struct directory* child_dir = db_lookup(gedp->dbip, child_name.c_str(), LOOKUP_QUIET);
        if (!child_dir) {
            qDebug() << "[ProcessGFiles::insertChildObjects] Child object" << QString::fromStdString(child_name) << "not found in database.";
            continue;
        }

        // Create ObjectData for the child
        ObjectData childObjData;
        childObjData.model_id = modelData.id;
        childObjData.name = child_name;
        childObjData.parent_object_id = parentObjData.object_id; // The parent's object ID
        childObjData.is_selected = (child_name == selected_object_name);

        qDebug() << "[ProcessGFiles::insertChildObjects] Inserting child object -"
            << "Model ID:" << childObjData.model_id
            << ", Name:" << QString::fromStdString(childObjData.name)
            << ", Parent Object ID:" << childObjData.parent_object_id
            << ", is_selected:" << childObjData.is_selected;

        int childInsertedObjectId = model->insertObject(childObjData);
        if (childInsertedObjectId == -1) {
            qDebug() << "[ProcessGFiles::insertChildObjects] Failed to insert child object:"
                << QString::fromStdString(childObjData.name)
                << "for parent object ID:" << parentObjData.object_id << "model ID:" << childObjData.model_id;
        }
        else {
            childObjData.object_id = childInsertedObjectId;
            qDebug() << "[ProcessGFiles::insertChildObjects] Successfully inserted child object:"
                << QString::fromStdString(childObjData.name)
                << "with ID:" << childInsertedObjectId << "for parent object ID:" << parentObjData.object_id << "model ID:" << childObjData.model_id;
        }
    }

    rt_db_free_internal(&intern);

    qDebug() << "[ProcessGFiles::insertChildObjects] Completed for parent object ID:" << parentObjData.object_id << "Name:" << QString::fromStdString(parentObjData.name);
}

void db_tree_list_comb_children(const union tree* tree, std::vector<std::string>& children) {
    if (!tree) return;

    switch (tree->tr_op) {
    case OP_UNION:
    case OP_INTERSECT:
    case OP_SUBTRACT:
    case OP_XOR:
        db_tree_list_comb_children(tree->tr_b.tb_left, children);
        db_tree_list_comb_children(tree->tr_b.tb_right, children);
        break;
    case OP_DB_LEAF:
        if (tree->tr_l.tl_name) {
            children.push_back(tree->tr_l.tl_name);
        }
        break;
    default:
        break;
    }
}

bool ProcessGFiles::generateThumbnail(ModelData& modelData, const std::string& selected_object_name)
{
    qDebug() << "[ProcessGFiles::generateThumbnail] Started for model ID:" << modelData.id
        << "with selected object:" << QString::fromStdString(selected_object_name);

    QSettings settings;
    int timeLimitMs = settings.value("previewTimer", 30).toInt() * 1000;

    if (selected_object_name.empty()) {
        qDebug() << "[ProcessGFiles::generateThumbnail] No valid object selected for raytrace in file:"
            << QString::fromStdString(truncatePath(modelData.file_path));
        return false;
    }

    if (modelData.file_path.empty()) {
        qDebug() << "[ProcessGFiles::generateThumbnail] No file path available in modelData for generating thumbnail.";
        return false;
    }

    QString previewsFolder = QString::fromStdString(model->getHiddenDirectoryPath() + "/previews");
    QString modelShortName = QString::fromStdString(std::filesystem::path(modelData.file_path).stem().string());
    QString pngFilePath = previewsFolder + "/" + modelShortName + ".png";
    QDir().mkpath(QFileInfo(pngFilePath).absolutePath());

    // Use the RT_EXECUTABLE_PATH from configuration
    QString rtExecutable = QStringLiteral(RT_EXECUTABLE_PATH);

    // Build the arguments list for rt.exe
    QStringList arguments;
    arguments << "-s512"
        << "-o" << pngFilePath
        << QString::fromStdString(modelData.file_path)
        << QString::fromStdString(selected_object_name);

    qDebug() << "[ProcessGFiles::generateThumbnail] Running command:" << rtExecutable << arguments;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
    // On Windows, execute the executable directly.
    process.setProgram(rtExecutable);
    process.setArguments(arguments);
#else
    // On Unix-like systems, if needed, you can execute via the shell.
    QString rtCommand = rtExecutable + " " + arguments.join(" ");
    process.setProgram("/bin/sh");
    process.setArguments({ "-c", rtCommand });
#endif

    process.start();
    if (!process.waitForStarted()) {
        qDebug() << "[ProcessGFiles::generateThumbnail] Failed to start the process for command:" << rtExecutable << arguments;
        return false;
    }

    bool finishedInTime = process.waitForFinished(timeLimitMs);
    if (!finishedInTime) {
        qDebug() << "[ProcessGFiles::generateThumbnail] Command timed out after" << timeLimitMs / 1000 << "seconds.";
        process.kill();
        process.waitForFinished();
        return false;
    }

    int exitCode = process.exitCode();
    if (exitCode != 0) {
        qDebug() << "[ProcessGFiles::generateThumbnail] The process finished with a non-zero exit code:" << exitCode;
        qDebug() << "[ProcessGFiles::generateThumbnail] Error output:" << process.readAllStandardOutput();
        return false;
    }

    if (!QFile::exists(pngFilePath) || QFileInfo(pngFilePath).size() == 0) {
        qDebug() << "[ProcessGFiles::generateThumbnail] Generated thumbnail file is empty or missing at path:" << pngFilePath;
        return false;
    }

    QFile thumbnailFile(pngFilePath);
    if (!thumbnailFile.open(QIODevice::ReadOnly)) {
        qDebug() << "[ProcessGFiles::generateThumbnail] Failed to open thumbnail file at:" << pngFilePath;
        return false;
    }

    QByteArray thumbnailData = thumbnailFile.readAll();
    thumbnailFile.close();

    if (thumbnailData.isEmpty()) {
        qDebug() << "[ProcessGFiles::generateThumbnail] Generated thumbnail data is empty.";
        return false;
    }

    modelData.thumbnail.assign(thumbnailData.begin(), thumbnailData.end());

    if (!QFile::remove(pngFilePath)) {
        qDebug() << "[ProcessGFiles::generateThumbnail] Could not remove PNG file at" << pngFilePath;
    }

    qDebug() << "[ProcessGFiles::generateThumbnail] Thumbnail generated and stored for model with ID:" << modelData.id
        << "and object:" << QString::fromStdString(selected_object_name);

    return true;
}

std::tuple<bool, std::string, std::string> ProcessGFiles::generateGistReport(const std::string& inputFilePath, const std::string& outputFilePath, const std::string& primary_obj, const std::string& label)
{
    // helper lambda for logging and returning a failed run
    auto returnFail = [inputFilePath, outputFilePath, primary_obj, label](const std::string err, const std::string cmd) -> std::tuple<bool, std::string, std::string> {
        // use truncated path for debug display?
        qDebug() << "[ProcessGFiles::generateGistReport] Started for inputFilePath:" << QString::fromStdString(truncatePath(inputFilePath))
                 << ", outputFilePath:" << QString::fromStdString(truncatePath(outputFilePath))
                 << ", primary_obj:" << QString::fromStdString(primary_obj)
                 << ", label:" << QString::fromStdString(label);

        if (!cmd.empty())
            qDebug() << "[ProcessGFiles::generateGistReport] Run gist command: " << QString::fromStdString(cmd);

        qWarning() << "[ProcessGFiles::generateGistReport]" << QString:: fromStdString(err);
        return { false, err, cmd };
    };

    // check for gist executable
    QString gistExecutable = QStringLiteral(GIST_EXECUTABLE_PATH);
    if (gistExecutable.isEmpty())
        return returnFail("Cannot find gist executable", "");

    // check inputFile
    QFileInfo inputFile(QString::fromStdString(inputFilePath));
    if (!inputFile.exists())
        return returnFail("Input file does not exist: " + inputFilePath, "");

    // build up arguments list
    // TODO: the ProcessGFiles class has access to the model - extract everything we can instead of expecting the caller to re-access to pass
    QStringList arguments;
    arguments << QString::fromStdString(inputFilePath)
              << "-o" << QString::fromStdString(outputFilePath);
    // re-use previous renders if found
    arguments << "-Z";
    // supplied primary 'top' object
    if (!primary_obj.empty())
        arguments << "-t" << QString::fromStdString(primary_obj);
    // supplied classification label
    if (!label.empty())
        arguments << "-c" << QString::fromStdString(label);

    // string version of command to be run (only used for debugging - QProcess has the 'real' command)
    std::string gistCommand = gistExecutable.toStdString() + " " + arguments.join(" ").toStdString();

    // build our process to be run
    QProcess process;
    process.setProgram(gistExecutable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);    // merge stdout and stderr log

    // misc process settings
    QSettings settings;
    int timeoutMs = settings.value("gistReportTimeoutSec", 120).toInt() * 1000; // 120 sec timeout default

    // start
    process.start();
    if (!process.waitForStarted())
        return returnFail("Failed to start the gist process", gistCommand);
    
    // check timeout
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        return returnFail("Gist command timed out after " + std::to_string(timeoutMs / 1000) + " seconds", gistCommand);
    }

    // check exit code
    if (process.exitStatus() != QProcess::NormalExit) {
        std::string error = "Gist exited with exit code: " + std::to_string(process.exitCode());
        error += "\nProcess output: \'" + process.readAll().toStdString() + "\'";
        return returnFail(error, gistCommand);
    }

    // check output file
    QFileInfo outputFile(QString::fromStdString(outputFilePath));
    if (!outputFile.exists() || outputFile.size() == 0)
        return returnFail("Output file not generated or empty at path: " + outputFilePath, gistCommand);


    // everything's good
    return { true, "", gistCommand };
}
