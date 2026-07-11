#include "LibraryWindow.h"
#include "IndexingWorker.h"
#include "ProcessGFiles.h"
#include "MainWindow.h"
#include "GeometryBrowserDialog.h"
#include "AuditViewerDialog.h"
#include "IntegrityViewerDialog.h"
#include "ModelView.h"
#include "Logger.h"
// #include "AdvancedOptionsDialog.h"
#include "ReportGenerationWindow.h"
#include "ReportGeneratorWorker.h"
#include "FileSystemFilterProxyModel.h"
#include "FileSystemModelWithCheckboxes.h"

#include <QThread>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListView>
#include <QPushButton>
#include <QComboBox>
#include <QMenuBar>
#include <QLineEdit>
#include <QLabel>
#include <QFileSystemWatcher>
#include <QStandardItem>
#include <QIcon>
#include <QTimer>
#include <QDebug>

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <set>

#include "CADventory.h"
#include "AIModelTagging.h"

namespace fs = std::filesystem;

LibraryWindow::LibraryWindow(QWidget* parent)
    : QWidget(parent),
    library(nullptr),
    mainWindow(nullptr),
    model(nullptr),
    uiModel(nullptr),
    availableModelsProxyModel(new ModelFilterProxyModel(this)),
    modelCardDelegate(new ModelCardDelegate(this)),
    explorerModel(new QStandardItemModel(this)),
    jobSvc(nullptr)
{
    ui.setupUi(this);
}

LibraryWindow::~LibraryWindow() {
    LOG_DEBUG << "LibraryWindow destructor called" << LOG_ENDL;

    if (jobSvc)
        jobSvc->stop();
}

void LibraryWindow::loadFromLibrary(Library* _library) {
    library = _library;
    ui.currentLibrary->setText(library->name());

    // Load models from the library
    model = library->model;
    model->refreshModelData();

    if (uiModel) {
        availableModelsProxyModel->setSourceModel(nullptr);
        delete uiModel;
    }
    uiModel = new QtModelsListModel(library->fullPath, this);

    // Set the source model for proxy model
    availableModelsProxyModel->setSourceModel(uiModel);

    // Now that library is set, set up models and views
    setupModelsAndViews();
    setupExplorerView();

    // Set up connections
    setupConnections();

    // setup and starts our job worker for this library
    setupLibraryWorker();
}

void LibraryWindow::setupLibraryWorker() {
    // get our job service
    jobSvc = dynamic_cast<QtJobServiceBase*>(CADventory::instance()->getJobService());
    if (!jobSvc)
        return;

    // sanity: make sure we stop before modifying paths
    if (jobSvc->state() != JobServiceState::Stopped)
        jobSvc->stop();

    // setup paths for this library
    // TODO: we probably want some utility class and/or job service itself
    //       to manage all these hidden paths
    std::filesystem::path root = library->fullPath;
    jobSvc->setRootPath(root.string());

    // setup connections
    const auto ct = static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection);
    connect(jobSvc, &QtJobServiceBase::directiveFinished, this, &LibraryWindow::onModelProcessed, ct);
    connect(jobSvc, &QtJobServiceBase::statsUpdated, this, &LibraryWindow::onProgressUpdated, ct);
    connect(jobSvc, &QtJobServiceBase::refreshSuggested, this, &LibraryWindow::onRefreshRequested, ct);
    // TODO: service infinitely loops, so we never emit 'finished' unless stop() is called
    connect(jobSvc, &QtJobServiceBase::finished, this, &LibraryWindow::onIndexingComplete, ct);

    // good to go
    jobSvc->start();
}

void LibraryWindow::startIndexing() {
    /*
    if (indexingThread && indexingThread->isRunning()) {
        if (indexingWorker) {
            indexingWorker->requestReindex();
        }
        return;
    }

    // Create the indexing worker and thread
    indexingThread = new QThread(this);
    indexingWorker = new IndexingWorker(library);

    indexingWorker->moveToThread(indexingThread);

    // Connect signals and slots
    connect(indexingThread, &QThread::started, indexingWorker, &IndexingWorker::process);
    connect(indexingWorker, &IndexingWorker::modelProcessed, this, &LibraryWindow::onModelProcessed);
    connect(indexingWorker, &IndexingWorker::progressUpdated, this, &LibraryWindow::onProgressUpdated);
    connect(indexingWorker, &IndexingWorker::finished, this, &LibraryWindow::onIndexingComplete);
    connect(indexingWorker, &IndexingWorker::finished, indexingThread, &QThread::quit);
    connect(indexingThread, &QThread::finished, indexingWorker, &QObject::deleteLater);
    connect(indexingThread, &QThread::finished, indexingThread, &QObject::deleteLater);

    // Start the indexing thread
    indexingThread->start();
    */
}

void LibraryWindow::processNextFile() {
    if (currentFileIndex >= static_cast<int>(filesToTag.size())) {
        ui.statusLabel->setText("Tagging complete!");
        ui.generateAllTagsButton->setEnabled(true);

        ui.generateAllTagsButton->show();
        ui.generateAllTagsButton->setEnabled(true);

        ui.pauseButton->hide();
        ui.cancelButton->hide();

	currentFileIndex = -1;
	canceled = false;
	paused = false;
        return;
    }

    QString filepath = QString::fromStdString(filesToTag[currentFileIndex]);
    LOG_DEBUG << "Processing file:" << filepath << LOG_ENDL;

    // get our tagger, start generating
    AIModelTagging* tagger = CADventory::instance()->getTagger();
    tagger->generateTags(filepath);
}

void LibraryWindow::onTagsGeneratedFromBatch(const QStringList& tags) {
    int fileIndex = currentFileIndex;
    if (fileIndex < 0 || fileIndex >= (int)filesToTag.size()) {
        return;
    }
    std::string filepath = filesToTag[fileIndex];

    ModelData data = model->getModelByFilePath(filepath);
    int modelId = data.id;
    if (modelId != -1) {
        std::vector<std::string> existingTags = model->getTagsForModel(modelId);
        std::set<std::string> existing(existingTags.begin(), existingTags.end());
        for (const QString& qtTag : tags) {
            std::string tag = qtTag.toStdString();
            if (existing.find(tag) == existing.end()) {
                model->addTagToModel(modelId, tag);
            }
        }

        model->refreshModelData();
        if (uiModel)
            uiModel->refresh();
        availableModelsProxyModel->invalidate();
    }

    int progress = ui.progressBar->value() + 1;
    ui.progressBar->setValue(progress);
    ui.statusLabel->setText(QString("Processed %1/%2")
        .arg(progress)
        .arg(ui.progressBar->maximum()));

    currentFileIndex++;

    QTimer::singleShot(200, this, &LibraryWindow::processNextFile);
}

void LibraryWindow::onTagGenerationFailed(const QString& reason) {
    if (canceled || paused || currentFileIndex < 0)
        return;

    ui.statusLabel->setText(tr("Skipped %1: %2")
                                .arg(currentFileIndex + 1)
                                .arg(reason));
    ++currentFileIndex;
    QTimer::singleShot(200, this, &LibraryWindow::processNextFile);
}

void LibraryWindow::onResumeTagGenerationClicked() {
    paused = false;

	processNextFile();

    ui.resumeButton->hide();
    ui.pauseButton->show();
    ui.cancelButton->show();

    ui.statusLabel->setText("Resuming tagging...");
}

void LibraryWindow::onPauseTagGenerationClicked() {
    paused = true;
    
    // get tagger
    AIModelTagging* tagger = CADventory::instance()->getTagger();

    // TODO: this is a little misleading (button click is 'pause', but we're canceling the process)
    tagger->cancel();

    ui.pauseButton->hide();
    ui.resumeButton->show();
    ui.cancelButton->show();
    ui.statusLabel->setText("Tag generation paused.");
}

void LibraryWindow::onCancelTagGenerationClicked() {
    canceled = true;

    // get tagger
    AIModelTagging* tagger = CADventory::instance()->getTagger();

    // kill any QProcess that is running
    tagger->cancel();

    // ui update
    ui.pauseButton->hide();
    ui.cancelButton->hide();
    ui.generateAllTagsButton->show();
    ui.generateAllTagsButton->setEnabled(true);
    ui.statusLabel->setText("Tag generation canceled.");
} 

void LibraryWindow::onGenerateAllTagsClicked() {
    // generates all tags for all models

    // get tagger
    AIModelTagging* tagger = CADventory::instance()->getTagger();

    // check the tagger was started successfully
    if (!tagger->taggingEnabled()) {
        const QString reason = tagger->taggingStatus();
        QMessageBox::critical(this, tr("AI Tagging Unavailable"),
                              reason.isEmpty() ? tr("Unable to use the AI tagger.") : reason);
        return;
    }

    // get all the paths
    std::vector<std::string> relativePaths = library->getModels();
    if (relativePaths.empty()) {
	QMessageBox::information(this, "No Models", "No models found in the library.");
        return;
    }
    // build full paths
    filesToTag.clear();
    for (const auto& rel : relativePaths) {
        filesToTag.push_back(library->fullPath + "/" + rel);
    }

    // ui updates
    ui.generateAllTagsButton->setEnabled(false);
    ui.generateAllTagsButton->hide();
    ui.pauseButton->show();
    ui.cancelButton->show();
    ui.progressBar->setMaximum(static_cast<int>(relativePaths.size()));
    ui.progressBar->setValue(0);
    ui.statusLabel->setText("Generating tags...");
    ui.generateAllTagsButton->setEnabled(false);

    // debug print
    LOG_DEBUG << "Generating tags for the following models:" << LOG_ENDL;
    for (const std::string& rel : relativePaths) {
        LOG_DEBUG << QString::fromStdString(rel) << LOG_ENDL;
    }

    // reset index?
    if (canceled || currentFileIndex <= 0) {
        currentFileIndex = 0;
    }

    // only setup one connection
    if (!taggerConnected) {
        connect(tagger, &AIModelTagging::tagsReady, this, &LibraryWindow::onTagsGeneratedFromBatch);
        connect(tagger, &AIModelTagging::taggingFailed, this, &LibraryWindow::onTagGenerationFailed);
        taggerConnected = true;
    }

    processNextFile();
}

void LibraryWindow::setMainWindow(MainWindow* mainWindow) {
    this->mainWindow = mainWindow;
    reload = new QAction(tr("&Reload"), this);

    this->mainWindow->editMenu->addAction(reload);
    connect(reload, &QAction::triggered, this, &LibraryWindow::reloadLibrary);
}

void LibraryWindow::setupModelsAndViews() {
    // Configure available models view
    ui.availableModelsView->setModel(availableModelsProxyModel);
    ui.availableModelsView->setItemDelegate(modelCardDelegate);
    ui.availableModelsView->setViewMode(QListView::ListMode);
    ui.availableModelsView->setFlow(QListView::TopToBottom);
    ui.availableModelsView->setWrapping(false);
    ui.availableModelsView->setResizeMode(QListView::Adjust);
    ui.availableModelsView->setSpacing(0);
    ui.availableModelsView->setUniformItemSizes(true);
    ui.availableModelsView->setSelectionMode(QAbstractItemView::NoSelection);
    ui.availableModelsView->setSelectionBehavior(QAbstractItemView::SelectRows);

    QSize itemSize = modelCardDelegate->sizeHint(QStyleOptionViewItem(), QModelIndex());
    ui.availableModelsView->setGridSize(QSize(0, itemSize.height()));

    // Setup file system model with checkboxes
    QString libraryPath = QString::fromStdString(library->fullPath);
    LOG_DEBUG << "Library Path in setupModelsAndViews:" << libraryPath << LOG_ENDL;

    // Create the FileSystemModelWithCheckboxes
    fileSystemModel = new FileSystemModelWithCheckboxes(libraryPath, this);

    // Create and set up the proxy model to filter .g files
    fileSystemProxyModel = new FileSystemFilterProxyModel(libraryPath, this);
    fileSystemProxyModel->setSourceModel(fileSystemModel);
    fileSystemProxyModel->setRecursiveFilteringEnabled(true);

    // Set the model to the tree view
    ui.fileSystemTreeView->setModel(fileSystemProxyModel);

    // Set the root index of the view to the mapped root index
    QModelIndex rootIndex = fileSystemModel->index(libraryPath);
    QModelIndex proxyRootIndex = fileSystemProxyModel->mapFromSource(rootIndex);
    ui.fileSystemTreeView->setRootIndex(proxyRootIndex);
    LOG_DEBUG << "Set root index of fileSystemTreeView to proxyRootIndex." << LOG_ENDL;

    // Hide columns other than the name
    for (int i = 1; i < fileSystemModel->columnCount(); ++i) {
        ui.fileSystemTreeView->hideColumn(i);
    }

    // Set uniform row heights for consistent appearance
    ui.fileSystemTreeView->setUniformRowHeights(true);

    // Set icon size (if desired)
    ui.fileSystemTreeView->setIconSize(QSize(24, 24));

    // Expand Filesystem
    expandFilesystemToDepth(m_fsTargetDepth);
    connect(ui.expandOneLevelButton, &QPushButton::clicked, this, &LibraryWindow::onExpandOneLevelClicked);
    connect(ui.collapseAllButton, &QPushButton::clicked, this, &LibraryWindow::onCollapseAllClicked);

    // Connect signals
    connect(fileSystemModel, &QFileSystemModel::directoryLoaded, this, &LibraryWindow::onDirectoryLoaded);
    connect(fileSystemModel, &FileSystemModelWithCheckboxes::inclusionChanged, this, &LibraryWindow::onInclusionChanged);
}

void LibraryWindow::setupExplorerView() {
    // Configure explorer models view
    ui.explorerModelsView->setModel(explorerModel);
    ui.explorerModelsView->setViewMode(QListView::ListMode);
    ui.explorerModelsView->setFlow(QListView::TopToBottom);
    ui.explorerModelsView->setWrapping(false);
    ui.explorerModelsView->setResizeMode(QListView::Adjust);
    ui.explorerModelsView->setSpacing(2);
    ui.explorerModelsView->setUniformItemSizes(true);
    ui.explorerModelsView->setSelectionMode(QAbstractItemView::NoSelection);
    ui.explorerModelsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // Disable the default selection highlighting
    ui.explorerModelsView->setStyleSheet("QListView::item:selected { background-color: transparent; }");
    
    // Set icon size
    ui.explorerModelsView->setIconSize(QSize(16, 16));
    
    // Populate the explorer model with all models in the library
    populateExplorerModel();
}

void LibraryWindow::populateExplorerModel() {
    // Clear the model and the stored items
    explorerModel->clear();
    allExplorerItems.clear();
    
    // Set up headers
    explorerModel->setHorizontalHeaderLabels(QStringList() << "Models");
    
    // Get all models from the library
    if (!uiModel)
        return;

    for (int i = 0; i < uiModel->rowCount(); ++i) {
        QModelIndex index = uiModel->index(i, 0);
        
        // show all included models
        if (uiModel->data(index, QtModelsListModel::IsIncludedRole).toBool()) {
            
            // Get model data
            int modelId = uiModel->data(index, QtModelsListModel::IdRole).toInt();
            QString shortName = uiModel->data(index, QtModelsListModel::ShortNameRole).toString();
            QString title = uiModel->data(index, QtModelsListModel::TitleRole).toString();
            bool isSelected = uiModel->data(index, QtModelsListModel::IsSelectedRole).toBool();
            
            // Remove file extension from shortName if present
            int dotIndex = shortName.lastIndexOf('.');
            if (dotIndex > 0) {
                shortName = shortName.left(dotIndex);
            }
            
            // Create item
            QStandardItem* item = new QStandardItem(shortName);
            item->setData(modelId, Qt::UserRole); // Store model ID
            item->setData(title, Qt::UserRole + 1); // Store title as tooltip
            
            // Set tooltip with title
            item->setToolTip(title);
            
            // Set background color if selected
            if (isSelected) {
                QColor selectedColor = QColor(180, 180, 180); // Darker gray
                item->setBackground(selectedColor);
            } else {
                // Ensure unselected items have transparent background
                item->setBackground(Qt::transparent);
            }
            
            // Add to model
            explorerModel->appendRow(item);
            
            // Store the item for filtering
            allExplorerItems.append(item);
        }
    }
}

void LibraryWindow::onExplorerModelClicked(const QModelIndex& index) {
    if (!uiModel)
        return;

    // Get the model ID from the item data
    int modelId = explorerModel->data(index, Qt::UserRole).toInt();
    LOG_DEBUG << "Explorer model clicked:" << modelId << LOG_ENDL;
    
    // Find the corresponding model in the UI-facing model
    for (int i = 0; i < uiModel->rowCount(); ++i) {
        QModelIndex modelIndex = uiModel->index(i, 0);
        if (uiModel->data(modelIndex, QtModelsListModel::IdRole).toInt() == modelId) {
            // Toggle selection state
            bool isSelected = uiModel->data(modelIndex, QtModelsListModel::IsSelectedRole).toBool();
            bool newSelectionState = !isSelected;
            uiModel->setData(modelIndex, newSelectionState, QtModelsListModel::IsSelectedRole);
            
            // Update the available models view to reflect the selection change
            QModelIndex proxyIndex = availableModelsProxyModel->mapFromSource(modelIndex);
            if (proxyIndex.isValid()) {
                availableModelsProxyModel->dataChanged(proxyIndex, proxyIndex, {QtModelsListModel::IsSelectedRole});
            }
            
            // Update the explorer view to highlight the selected item
            QStandardItem* item = explorerModel->itemFromIndex(index);
            if (item) {
                // Set the background color based on selection state
                if (newSelectionState) {
                    // Selected
                    QColor selectedColor = QColor(180, 180, 180); // Darker gray
                    item->setBackground(selectedColor);
                } else {
                    // Deselected
                    item->setBackground(Qt::transparent);
                }
                
                // Update the view to reflect the change
                explorerModel->dataChanged(index, index, {Qt::BackgroundRole});
            }
            break;
        }
    }
}

void LibraryWindow::onExplorerModelDoubleClicked(const QModelIndex& index) {
    // Just call the click handler to select the model
    onExplorerModelClicked(index);
}

void LibraryWindow::setupConnections() {

    ui.pauseButton->hide();
    ui.cancelButton->hide();
    ui.resumeButton->hide();

    // Connect search input
    connect(ui.searchLineEdit, &QLineEdit::textChanged,
            this, &LibraryWindow::onSearchTextChanged);
    connect(ui.searchFieldComboBox, &QComboBox::currentTextChanged,
            this, &LibraryWindow::onSearchFieldChanged);

    // Connect clicks on available models
    connect(ui.availableModelsView, &QListView::clicked,
            this, &LibraryWindow::onAvailableModelClicked);

    // Connect Generate Report button
    connect(ui.generateReportButton, &QPushButton::clicked,
            this, &LibraryWindow::onGenerateReportButtonClicked);
    connect(ui.auditLogButton, &QPushButton::clicked,
            this, &LibraryWindow::onAuditLogButtonClicked);
    connect(ui.integrityButton, &QPushButton::clicked,
            this, &LibraryWindow::onIntegrityButtonClicked);

    // Connect geometry browser clicked signal
    connect(modelCardDelegate, &ModelCardDelegate::geometryBrowserClicked,
            this, &LibraryWindow::onGeometryBrowserClicked);

    connect(modelCardDelegate, &ModelCardDelegate::modelViewClicked,
            this, &LibraryWindow::onModelViewClicked);
            
    // Connect explorer view signals
    connect(ui.explorerModelsView, &QListView::clicked,
            this, &LibraryWindow::onExplorerModelClicked);
    connect(ui.explorerModelsView, &QListView::doubleClicked,
            this, &LibraryWindow::onExplorerModelDoubleClicked);

	// Connect generate all tags button
	connect(ui.generateAllTagsButton, &QPushButton::clicked,
		this, &LibraryWindow::onGenerateAllTagsClicked);

	// Connect pause/cancel/resume tag generation buttons
    connect(ui.pauseButton, &QPushButton::clicked,
        this, &LibraryWindow::onPauseTagGenerationClicked);
    connect(ui.cancelButton, &QPushButton::clicked,
        this, &LibraryWindow::onCancelTagGenerationClicked);
    connect(ui.resumeButton, &QPushButton::clicked,
        this, &LibraryWindow::onResumeTagGenerationClicked);

    ui.searchFieldComboBox->clear();
	ui.searchFieldComboBox->addItem("Short Name", QtModelsListModel::ShortNameRole);
    ui.searchFieldComboBox->addItem("Tags", QtModelsListModel::TagsRole);
}

void LibraryWindow::onAuditLogButtonClicked() {
    if (!model)
        return;

    AuditViewerDialog dialog(model->getHiddenPaths().auditDir(), this);
    dialog.exec();
}

void LibraryWindow::onIntegrityButtonClicked() {
    if (!model)
        return;

    IntegrityViewerDialog dialog(model->getHiddenPaths(), model->getAll(), this);
    dialog.exec();
}

void LibraryWindow::onSearchTextChanged(const QString& text) {
    int role = ui.searchFieldComboBox->currentData().toInt();
    availableModelsProxyModel->setFilterRole(role);
    availableModelsProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    availableModelsProxyModel->setFilterFixedString(text);
}

void LibraryWindow::onSearchFieldChanged(const QString& field) {
    Q_UNUSED(field);
    // Update filter role based on selected field
    int role = ui.searchFieldComboBox->currentData().toInt();
    availableModelsProxyModel->setFilterRole(role);
    // Re-apply filter
    availableModelsProxyModel->invalidate();
}

void LibraryWindow::onAvailableModelClicked(const QModelIndex& index) {
    if (!uiModel)
        return;

    // Toggle selection state
    QModelIndex sourceIndex = availableModelsProxyModel->mapToSource(index);
    bool isSelected = uiModel->data(sourceIndex, QtModelsListModel::IsSelectedRole).toBool();
    bool newSelectionState = !isSelected;
    uiModel->setData(sourceIndex, newSelectionState, QtModelsListModel::IsSelectedRole);

    // Update the view to reflect the selection change
    availableModelsProxyModel->dataChanged(index, index, {QtModelsListModel::IsSelectedRole});
    
    // Get the model ID
    int modelId = uiModel->data(sourceIndex, QtModelsListModel::IdRole).toInt();
    
    // Update the explorer view to highlight the selected item
    for (int i = 0; i < explorerModel->rowCount(); ++i) {
        QModelIndex explorerIndex = explorerModel->index(i, 0);
        if (explorerModel->data(explorerIndex, Qt::UserRole).toInt() == modelId) {
            QStandardItem* item = explorerModel->itemFromIndex(explorerIndex);
            if (item) {
                // Set the background color based on selection state
                if (newSelectionState) {
                    // Selected
                    QColor selectedColor = QColor(180, 180, 180); // Darker gray
                    item->setBackground(selectedColor);
                } else {
                    // Deselected
                    item->setBackground(Qt::transparent);
                }
                
                // Update the view to reflect the change
                explorerModel->dataChanged(explorerIndex, explorerIndex, {Qt::BackgroundRole});
            }
            break;
        }
    }
}

void LibraryWindow::onGenerateReportButtonClicked() {
    model->refreshModelData();
    bool have_selected = !model->getSelectedModels().empty();
    if (!have_selected) {
        // if we don't have any currently selected, ask to select all
        const auto choice = QMessageBox::question(
            this,
            "Report",
            "No models are currently selected.\n\n"
            "Select all included models and continue?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );
        if (choice != QMessageBox::Yes)
            return;

        if (uiModel)
            uiModel->selectAllIncluded(true);
        model->refreshModelData();
        have_selected = !model->getSelectedModels().empty();
    }

    // if we still dont have any selected; bail
    if (!have_selected) {
        QMessageBox::information(this, "Report", "No models selected for the report.");
        return;
    }

    ReportGenerationWindow* window = new ReportGenerationWindow(nullptr, model, library);
    window->show();
}

void LibraryWindow::onSettingsClicked(int modelId) {
    // Handle settings button click
    LOG_DEBUG << "Settings button clicked for model ID:" << modelId << LOG_ENDL;
    // Implement settings dialog or other actions here
}

void LibraryWindow::onRefreshRequested() {
    model->refreshModelData();
    if (uiModel)
        uiModel->refresh();
    availableModelsProxyModel->invalidate();

    // Update explorer model
    populateExplorerModel();
}

void LibraryWindow::onModelProcessed(const QString& directive, const QString& id, bool success) {
    Q_UNUSED(directive); Q_UNUSED(id);

    if (!success)
        return;
    model->refreshModelData();
    if (uiModel)
        uiModel->refresh();
    availableModelsProxyModel->invalidate();
    
    // Update explorer model
    populateExplorerModel();
}

void LibraryWindow::on_backButton_clicked() {
    LOG_DEBUG << "Back button clicked" << LOG_ENDL;

    if (jobSvc)
        jobSvc->stop();

    // Hide the LibraryWindow
    this->hide();
    LOG_DEBUG << "LibraryWindow hidden" << LOG_ENDL;

    // Show the MainWindow
    if (mainWindow) {
        this->mainWindow->editMenu->removeAction(reload);
        disconnect(reload, nullptr, nullptr, nullptr);
        this->mainWindow->returnCentralWidget();
        this->mainWindow->updateStatusLabel("Select or add a new library"); // reset status message
        LOG_DEBUG << "MainWindow shown" << LOG_ENDL;
    } else {
        LOG_DEBUG << "mainWindow is null" << LOG_ENDL;
    }
}

void LibraryWindow::reloadLibrary() {
    std::string path = library->fullPath; //+ "/.cadventory/metadata.db";
    fs::path filePath(path);

    LOG_DEBUG << QString::fromStdString(filePath.string()) << LOG_ENDL;

    // Check if the file exists
    if (fs::exists(filePath)) {

        LOG_DEBUG << "reloadLibrary is called" << LOG_ENDL;
        try {

                // Reload the library
                model->resetDatabase();
                model->refreshModelData();
                if (uiModel)
                    uiModel->refresh();
                availableModelsProxyModel->invalidate();
                fileSystemModel->refresh(); // Custom method to refresh the model
                this->loadFromLibrary(library);

        } catch (const fs::filesystem_error& e) {
            LOG_ERR << "Error: " << e.what() << LOG_ENDL;
        }
    } else {
        LOG_WARN << "'.cadventory' does not exist." << LOG_ENDL;
    }
}

void LibraryWindow::onModelViewClicked(int modelId) {
    LOG_DEBUG << "Model view clicked for model ID:" << modelId << LOG_ENDL;
    ModelView* modelView = new ModelView(modelId, model, this);

    connect(modelView, &ModelView::tagsUpdated, this, [this]() {
        LOG_DEBUG << "Tags updated - refreshing proxy model" << LOG_ENDL;
        model->refreshModelData(); 
        if (uiModel)
            uiModel->refresh();
        availableModelsProxyModel->invalidate();
        });

    modelView->exec();
}

void LibraryWindow::onGeometryBrowserClicked(int modelId) {
    LOG_DEBUG << "Geometry browser clicked for model ID:" << modelId << LOG_ENDL;

    GeometryBrowserDialog* dialog = new GeometryBrowserDialog(modelId, model, this);
    dialog->exec();
}

void LibraryWindow::onProgressUpdated(const JobServiceStats& st) {
    if (st.jobsNew > 0) {
        ui.progressBar->setVisible(true);
        ui.progressBar->setRange(0, 0);     // marquee

        ui.statusLabel->setText(QString("  (%1) remain")
                                         .arg(st.jobsNew));

        return;
    }

    ui.statusLabel->setText("Idle.");
    ui.progressBar->setVisible(false);
}

void LibraryWindow::onInclusionChanged(const QModelIndex& index, bool /*included*/) {
    Q_UNUSED(index);
    model->refreshModelData();
    if (uiModel)
        uiModel->refresh();
    availableModelsProxyModel->invalidate();

    startIndexing();
    
    // Update explorer model
    populateExplorerModel();
}

void LibraryWindow::onIndexingComplete() {
    LOG_DEBUG << "Indexing complete" << LOG_ENDL;

    // Refresh model data
    model->refreshModelData();
    if (uiModel)
        uiModel->refresh();
    availableModelsProxyModel->invalidate();
    
    // Update explorer model
    populateExplorerModel();

    // Update filesystem view checkboxes
    fileSystemModel->dataChanged(fileSystemModel->index(0, 0),
                                 fileSystemModel->index(fileSystemModel->rowCount() - 1, 0),
                                 {Qt::CheckStateRole});
}

void LibraryWindow::onDirectoryLoaded(const QString& path) {
    fileSystemProxyModel->invalidate();
    ui.statusLabel->setText(tr("Loaded %1").arg(path));
    ui.fileSystemTreeView->viewport()->update();
}

void LibraryWindow::expandFilesystemToDepth(int depth) {
    auto *treeview = ui.fileSystemTreeView;
    const QModelIndex rootIdx = treeview->rootIndex();
    if (!rootIdx.isValid())
        return;

    treeview->setUpdatesEnabled(false);
    if (depth < 0) {
        treeview->expandRecursively(rootIdx);           // full expand
    } else {
        treeview->expandRecursively(rootIdx, depth);    // bounded depth
    }
    treeview->setUpdatesEnabled(true);
}

void LibraryWindow::onExpandOneLevelClicked() {
    if (m_fsTargetDepth < 0) {
        // already expaned to 'all'
        expandFilesystemToDepth(-1);
        return;
    }

    ++m_fsTargetDepth;                 // move to next depth
    expandFilesystemToDepth(m_fsTargetDepth);
}

void LibraryWindow::onCollapseAllClicked() {
    auto *treeview = ui.fileSystemTreeView;

    treeview->setUpdatesEnabled(false);
    treeview->collapseAll();                      // purely visual; dont re-walk
    treeview->expand(treeview->rootIndex());      // keep root visible
    treeview->setUpdatesEnabled(true);
}
