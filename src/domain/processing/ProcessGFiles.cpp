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
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

#include "sha1.h"

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
    //updatedModelData.is_processed_dir = generateUUID(gedp.get(), primaryObject);
    // set processed dir
    updatedModelData.is_processed_dir = generateProcessDir(modelData.file_path, primaryObject);

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

// helper function - create a sha1 hash of the file contents (this should be the same
// as doing a 'sha1sum' on the command-line
static inline std::string sha1Digest(const std::filesystem::path& file) {
    SHA1_CTX ctx;
    SHA1Init(&ctx);

    // hash file contents
    std::array<char, 1 << 12> buf;
    std::ifstream in(file, std::ios::binary);
    while (in.read(buf.data(), buf.size()) || in.gcount())
        SHA1Update(&ctx,
                   reinterpret_cast<const unsigned char*>(buf.data()),
                   static_cast<uint32_t>(in.gcount()));

    unsigned char raw[20];
    SHA1Final(raw, &ctx);

    // make a human-readable string
    std::ostringstream out;
    for (auto b : raw)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);

    return out.str();
}

std::string ProcessGFiles::generateProcessDir(const std::string& filename, const std::string& objName) {
    using std::filesystem::path;

    const std::string hash = sha1Digest(filename);
    const path dir = path(hash.substr(0,2)) / hash / objName;       // .../aa/hash/objName

    return dir.generic_string();
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

struct safeRtInternal {
    // RAII rt_db_internal
    rt_db_internal intern{};
    bool ok{false};
    safeRtInternal(struct directory* dp, struct db_i* dbip) {
        ok = (rt_db_get_internal(&intern, dp, dbip, nullptr, &rt_uniresource) >= 0);
    }
    ~safeRtInternal() {
        if (ok) rt_db_free_internal(&intern);
    }
};

// edge de-dup (parent_dp*, child_dp*)
using Edge = std::pair<const directory*, const directory*>;
struct EdgeHash {
    size_t operator()(const Edge& e) const noexcept {
        auto h1 = std::hash<const void*>{}(e.first);
        auto h2 = std::hash<const void*>{}(e.second);

        // hash_combine
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
// convenience defines
struct QNode { int parentId; const directory* parentDp; int depth; };
using EdgeSet       = std::unordered_set<Edge, EdgeHash>;
using ChildSet      = std::unordered_set<const directory*>;
using ChildrenCache = std::unordered_map<const directory*, ChildSet>;
using DpByName      = std::unordered_map<std::string, const directory*>;

// context passed to the BFS walker
struct ProcessGFiles::WalkCtx {
    Model*          model;
    ged*            gedp;
    int             modelId;
    const DpByName& dpByName;
    ChildrenCache&  childrenCache;
    EdgeSet&        seenEdges;
    int             maxDepthEdges;
    std::string     selectedName;
};

void ProcessGFiles::extractObjects(ModelData& modelData, struct ged* gedp) {
    qDebug() << "[ProcessGFiles::extractObjects] Started for model ID:" << modelData.id;

    if (!gedp || !gedp->dbip) {
        std::cerr << "[ProcessGFiles::extractObjects] Invalid ged pointer." << std::endl;
        qDebug() << "[ProcessGFiles::extractObjects] Invalid ged pointer. Cannot process objects.";
        return;
    }

    // depth limit in edges from a 'tops' object
    QSettings settings;
    int maxDepthEdges = std::max(settings.value("objects/maxDepthEdges", 1).toInt(), 0);

    // build name->dp, and collect tops & combs
    DpByName dpByName;
    std::vector<const directory*> tops;
    std::vector<struct directory*> combs;

    // check for a common-pattern 'tops' component
    const std::string model_short_name = modelData.short_name;
    std::unordered_set<std::string> objects_to_try = {
        "all", "all.g",
        model_short_name, model_short_name + ".g", model_short_name + ".c"
    };
    std::string selected_object_name;

    // collect directories
    struct directory* dp = nullptr;
    FOR_ALL_DIRECTORY_START(dp, gedp->dbip) {
        if (!dp || !dp->d_namep || dp->d_flags & RT_DIR_HIDDEN)
            continue;
        std::string name(dp->d_namep);

        // map name to dp
        dpByName.emplace(name, dp);

        // keep track of all combs
        if (dp->d_flags & RT_DIR_COMB)
            combs.push_back(dp);

        // no refs means we're a 'tops' object
        if (dp->d_nref == 0) {
            tops.push_back(dp);

            if (selected_object_name.empty() && 
                (objects_to_try.find(dp->d_namep) != objects_to_try.end())) {
                // keep track of a common top object for default 'selection'
                selected_object_name = std::string(dp->d_namep);
            }
        }
    } FOR_ALL_DIRECTORY_END

    // make sure we got something
    if (dpByName.empty() || tops.empty()) {
        qDebug() << "[ProcessGFiles::extractObjects] No objects found in database for model ID:" << modelData.id;
        return;
    }

    // we didn't get lucky with common object_name, use first 'tops'
    if (selected_object_name.empty() && !tops.empty())
        selected_object_name = std::string(tops.front()->d_namep);

    // cache what we can
    ChildrenCache childrenCache;    // unpack each comb once -> set of children
    EdgeSet seenEdges;              // edges de-duped (parent_dp*, child_dp*)

    // walker context
    WalkCtx ctx{ model, gedp, modelData.id, dpByName, childrenCache, seenEdges, 
                 maxDepthEdges, selected_object_name };

    // for each top, insert and BFS to depth
    for (const directory* topDp : tops) {
        // Create ObjectData for the top-level object
        ObjectData topLevelObjData;
        topLevelObjData.model_id = modelData.id;
        topLevelObjData.name = topDp->d_namep;
        topLevelObjData.parent_object_id = -1;  // -1 indicates no parent
        topLevelObjData.is_selected = (topLevelObjData.name == selected_object_name);

        const int topId = model->insertObject(topLevelObjData);
        if (topId == -1)
            continue;

        insertChildObjects(topId, topDp, ctx);
    }

    qDebug() << "[ProcessGFiles::extractObjects] Done. Depth edges =" << maxDepthEdges;
}

void db_tree_list_comb_children(const union tree* tree,
                                const DpByName& dpByName,
                                ChildSet& children) {
    if (!tree) return;
    RT_CK_TREE(tree);

    switch (tree->tr_op) {
        case OP_UNION:
        case OP_INTERSECT:
        case OP_SUBTRACT:
        case OP_XOR:
            db_tree_list_comb_children(tree->tr_b.tb_left,  dpByName, children);
            db_tree_list_comb_children(tree->tr_b.tb_right, dpByName, children);
            break;
        case OP_DB_LEAF:
            if (tree->tr_l.tl_name) {
                auto it = dpByName.find(std::string(tree->tr_l.tl_name));
                if (it != dpByName.end()) {
                    const directory* cdp = it->second;
                    if (cdp && !(cdp->d_flags & RT_DIR_HIDDEN)) {
                        children.insert(cdp);
                    }
                }
            }
            break;
        default:
            break;
    }
}

void ProcessGFiles::insertChildObjects(int parentId, const directory* parentDp, const WalkCtx& ctx) {
    // if not a comb or depth limit is 0, we're done
    if (!(parentDp->d_flags & RT_DIR_COMB) || ctx.maxDepthEdges == 0)
        return;

    // use queue for BFS
    std::queue<QNode> queue;
    queue.push(QNode{parentId, parentDp, 0});

    while (!queue.empty()) {
        auto [parentId, parentDp, depth] = queue.front();
        queue.pop();
        if (depth >= ctx.maxDepthEdges)
            continue;

        // fetch or build children set for this comb
        auto ccit = ctx.childrenCache.find(parentDp);
        if (ccit == ctx.childrenCache.end()) {
            ChildSet children;
            safeRtInternal guard(const_cast<directory*>(parentDp), ctx.gedp->dbip);
            if (guard.ok) {
                auto* comb = static_cast<rt_comb_internal*>(guard.intern.idb_ptr);
                if (comb && comb->tree) {
                    db_tree_list_comb_children(comb->tree, ctx.dpByName, children);
                }
            }

            ccit = ctx.childrenCache.emplace(parentDp, std::move(children)).first;
        }
        const ChildSet& children = ccit->second;

        for (const directory* childDp : children) {
            Edge edge{parentDp, childDp};
            if (!ctx.seenEdges.insert(edge).second)
                continue; // already processed

            // Create ObjectData for child
            ObjectData childObjData;
            childObjData.model_id = ctx.modelId;
            childObjData.name = childDp->d_namep;
            childObjData.parent_object_id = parentId;
            childObjData.is_selected = (childObjData.name == ctx.selectedName);

            const int childId = model->insertObject(childObjData);
            if (childId == -1)
                continue;

            // continue BFS if child is a comb and we haven't hit depth limit
            if ((childDp->d_flags & RT_DIR_COMB) && (depth + 1 < ctx.maxDepthEdges)) {
                queue.push(QNode{childId, childDp, depth + 1});
            }
        }
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
