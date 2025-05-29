#include "ProcessGFiles.h"
#include <brlcad/ged.h>
#include <QDebug>
#include <iostream>
#include <QProcess>
#include <QSettings>
#include <QFile>
#include <QDir>
#include "config.h"
#include <string>
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

void ProcessGFiles::processGFile(const ModelData& modelData)
{
    // Use truncated path for debug output only.
    std::string displayPath = truncatePath(modelData.file_path);
    qDebug() << "[ProcessGFiles::processGFile] Processing model with ID:" << modelData.id
        << "and file path:" << QString::fromStdString(displayPath);

    // Ensure file path is not empty
    if (modelData.file_path.empty()) {
        qDebug() << "[ProcessGFiles::processGFile] No file path provided. Aborting.";
        return;
    }

    // Open the BRL-CAD database using the full path.
    const char* db_filename = modelData.file_path.c_str();
    struct ged* gedp = ged_open("db", db_filename, 0);
    if (gedp == GED_NULL) {
        qDebug() << "[ProcessGFiles::processGFile] Error: Unable to open BRL-CAD database at path:"
            << QString::fromStdString(displayPath);
        return;
    }

    // Make a copy of modelData to modify
    ModelData updatedModelData = modelData;
    updatedModelData.is_processed = true;

    extractTitle(updatedModelData, gedp);
    qDebug() << "[ProcessGFiles::processGFile] Title extracted:" << QString::fromStdString(updatedModelData.title);

    // Update the model in the database with the extracted title.
    if (!model->updateModel(updatedModelData.id, updatedModelData)) {
        qDebug() << "[ProcessGFiles::processGFile] Error: Could not update model in database for ID:"
            << updatedModelData.id;
        ged_close(gedp);
        return;
    }
    extractObjects(updatedModelData, gedp);

    std::vector<ObjectData> allObjects = model->getObjectsForModel(updatedModelData.id);
    if (allObjects.empty()) {
        qDebug() << "[ProcessGFiles::processGFile] No objects found for model ID:" << updatedModelData.id
            << ". Skipping thumbnail generation.";
        ged_close(gedp);
        return;
    }

    qDebug() << "[ProcessGFiles::processGFile] Attempting thumbnail generation for model ID:" << updatedModelData.id;

    std::vector<ObjectData> selectedObjects = model->getSelectedObjectsForModel(updatedModelData.id);

    std::string objectNameForThumbnail;
    if (!selectedObjects.empty()) {
        const ObjectData& selectedObject = selectedObjects.front();
        objectNameForThumbnail = selectedObject.name;
        qDebug() << "[ProcessGFiles::processGFile] Found selected object. Using object named:"
            << QString::fromStdString(objectNameForThumbnail) << "for thumbnail.";
    }
    else {
        if (!allObjects.empty()) {
            objectNameForThumbnail = "all";
            qDebug() << "[ProcessGFiles::processGFile] No specific object selected. Defaulting to 'all'.";
        }
        else {
            qDebug() << "[ProcessGFiles::processGFile] No objects available for thumbnail generation.";
            ged_close(gedp);
            return;
        }
    }

    bool thumbnailGenerated = generateThumbnail(updatedModelData, objectNameForThumbnail);

    if (!thumbnailGenerated) {
        qDebug() << "[ProcessGFiles::processGFile] Thumbnail generation failed for model ID:" << updatedModelData.id;
    }
    else {
        qDebug() << "[ProcessGFiles::processGFile] Thumbnail generated successfully for model ID:" << updatedModelData.id
            << ". Updating model data in the database.";
        if (!model->updateModel(updatedModelData.id, updatedModelData)) {
            qDebug() << "[ProcessGFiles::processGFile] Error updating model thumbnail in database for ID:"
                << updatedModelData.id;
        }
        else {
            qDebug() << "[ProcessGFiles::processGFile] Successfully updated model thumbnail for model ID:" << updatedModelData.id;
        }
    }

    ged_close(gedp);
    // Use the truncated version for the final debug output.
    qDebug() << "[ProcessGFiles::processGFile] Completed processing for path:" << QString::fromStdString(truncatePath(updatedModelData.file_path));
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
    std::string gistCommand;
    std::string errorMessage;

    // Use truncated paths for debug display.
    qDebug() << "[ProcessGFiles::generateGistReport] Started for inputFilePath:" << QString::fromStdString(truncatePath(inputFilePath))
        << ", outputFilePath:" << QString::fromStdString(truncatePath(outputFilePath))
        << ", primary_obj:" << QString::fromStdString(primary_obj)
        << ", label:" << QString::fromStdString(label);

    QFileInfo inputFile(QString::fromStdString(inputFilePath));
    if (!inputFile.exists()) {
        errorMessage = "Input file does not exist: " + inputFilePath;
        qDebug() << "[ProcessGFiles::generateGistReport]" << QString::fromStdString(errorMessage);
        return { false, errorMessage, "" };
    }

    QString gistExecutable = QStringLiteral(GIST_EXECUTABLE_PATH);
    QStringList arguments;

    arguments << QString::fromStdString(inputFilePath)
        << "-o" << QString::fromStdString(outputFilePath) << "-Z";


    gistCommand = gistExecutable.toStdString() + " " + arguments.join(" ").toStdString();
    qDebug() << "[ProcessGFiles::generateGistReport] Running gist command:" << QString::fromStdString(gistCommand);

    QSettings settings;
    int timeLimitMs = settings.value("gistReportTimer", 10000).toInt() * 1000;

    QProcess process;
    process.setProgram(gistExecutable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);

    process.start();
    if (!process.waitForStarted()) {
        errorMessage = "Failed to start the gist process for command: " + gistCommand;
        qDebug() << "[ProcessGFiles::generateGistReport]" << QString::fromStdString(errorMessage);
        return { false, errorMessage, gistCommand };
    }

    bool finishedInTime = process.waitForFinished(timeLimitMs);

    if (!finishedInTime) {
        // The process did not finish in the allotted time
        errorMessage = "Gist command timed out after " + std::to_string(timeLimitMs / 1000) + " seconds.";
        qDebug() << "[ProcessGFiles::generateGistReport]" << QString::fromStdString(errorMessage);
        process.kill();
        process.waitForFinished();
        return { false, errorMessage, gistCommand };
    }

    int exitCode = process.exitCode();
    if (exitCode != 0) {
        errorMessage = "The gist process finished with a non-zero exit code: " + std::to_string(exitCode);
        std::string processOutput = process.readAllStandardOutput().toStdString();
        qDebug() << "[ProcessGFiles::generateGistReport]" << QString::fromStdString(errorMessage);
        qDebug() << "[ProcessGFiles::generateGistReport] Process output:" << QString::fromStdString(processOutput);
        return { false, processOutput, gistCommand };
    }

    // Check if the output file was generated
    QFileInfo outputFile(QString::fromStdString(outputFilePath));
    if (!outputFile.exists() || outputFile.size() == 0) {
        errorMessage = "Output file not generated or empty at path: " + outputFilePath;
        qDebug() << "[ProcessGFiles::generateGistReport]" << QString::fromStdString(errorMessage);
        return { false, errorMessage, gistCommand };
    }

    qDebug() << "[ProcessGFiles::generateGistReport] Gist report generated successfully for file:"
        << QString::fromStdString(truncatePath(inputFilePath))
        << "Output path:" << QString::fromStdString(truncatePath(outputFilePath));

    return { true, "", gistCommand };
}
