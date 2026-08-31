#include "QtEditorWindow.h"

#include "EditorRuntimeLayer.h"
#include "RhiViewportWindow.h"
#include "panels/QtComponentsPanel.h"
#include "core/Application.h"
#include "core/Logger.h"
#include "renderer/Renderer.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QUrl>

#include <fstream>

namespace Demon {
namespace {

QString qPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

std::filesystem::path fsPath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

QDoubleSpinBox* makeSpin(double value, double minimum = -100000.0, double maximum = 100000.0)
{
    auto* spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setDecimals(4);
    spin->setSingleStep(0.1);
    spin->setValue(value);
    return spin;
}

QWidget* formContainer(QFormLayout** outForm)
{
    auto* content = new QWidget;
    auto* form = new QFormLayout(content);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setContentsMargins(8, 8, 8, 8);
    *outForm = form;
    return content;
}

QString entityName(const std::shared_ptr<Scene>& scene, EntityID id)
{
    if (scene) {
        if (const auto* tag = scene->getComponent<TagComponent>(id))
            return QString::fromStdString(tag->tag);
    }
    return QStringLiteral("Entity %1").arg(id);
}

class EntityTreeWidget final : public QTreeWidget {
public:
    std::function<bool(EntityID, EntityID)> reparentEntity;

protected:
    void dropEvent(QDropEvent* event) override
    {
        QTreeWidgetItem* source = currentItem();
        QTreeWidgetItem* target = itemAt(event->position().toPoint());
        if (!source || !reparentEntity) {
            event->ignore();
            return;
        }
        const EntityID sourceId = source->data(0, Qt::UserRole).toULongLong();
        const EntityID targetId = target ? target->data(0, Qt::UserRole).toULongLong() : NULL_ENTITY;
        if (sourceId != targetId && reparentEntity(sourceId, targetId)) {
            event->acceptProposedAction();
            return;
        }
        event->ignore();
    }
};

} // namespace

QtEditorWindow::QtEditorWindow(std::string projectName,
                               std::filesystem::path projectRoot,
                               std::filesystem::path projectConfig)
    : m_projectName(std::move(projectName)),
      m_projectRoot(std::move(projectRoot)),
      m_projectConfig(std::move(projectConfig))
{
    setWindowTitle(QStringLiteral("DemonEngine - %1").arg(QString::fromStdString(m_projectName)));
    resize(1600, 900);
    setDockNestingEnabled(true);

    m_viewport = new RhiViewportWindow;
    m_viewport->setPickCallback([this](float ndcX, float ndcY) {
        if (!m_runtime)
            return;
        if (m_runtime->beginTransformDrag(ndcX, ndcY)) {
            statusBar()->showMessage(QStringLiteral("Transform gizmo active"));
            return;
        }
        const EntityID picked = m_runtime->pickEntity(ndcX, ndcY);
        selectEntity(picked);
        statusBar()->showMessage(picked == NULL_ENTITY
            ? QStringLiteral("No entity under cursor")
            : QStringLiteral("Selected entity %1").arg(picked), 1800);
    });
    m_viewport->setPointerMoveCallback([this](float ndcX, float ndcY) {
        if (m_runtime && m_runtime->updateTransformDrag(ndcX, ndcY))
            m_sceneDirty = true;
    });
    m_viewport->setPointerReleaseCallback([this](float, float) {
        if (!m_runtime || !m_runtime->isTransformDragging())
            return;
        m_runtime->endTransformDrag();
        rebuildInspector();
        statusBar()->showMessage(QStringLiteral("Transform updated"), 1500);
    });
    m_viewport->setContextMenuCallback([this](const QPoint& globalPosition) {
        showViewportContextMenu(globalPosition);
    });
    m_viewportContainer = QWidget::createWindowContainer(m_viewport, this);
    m_viewportContainer->setMinimumSize(480, 270);
    m_viewportContainer->setFocusPolicy(Qt::StrongFocus);
    m_viewportContainer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_viewportContainer, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        showViewportContextMenu(m_viewportContainer->mapToGlobal(position));
    });
    setCentralWidget(m_viewportContainer);

    buildDocks();
    buildMenus();
    buildToolbar();
    applyTheme();
    statusBar()->showMessage(QStringLiteral("Qt 6.11 editor ready"));

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] { refreshStats(); });
    timer->start(250);

    show();
    m_viewport->winId();
}

QtEditorWindow::~QtEditorWindow()
{
    if (m_loggerSinkId != 0)
        Logger::removeSink(m_loggerSinkId);
}

HWND QtEditorWindow::viewportHandle() const
{
    return reinterpret_cast<HWND>(m_viewport->winId());
}

void QtEditorWindow::bindRuntime(Application* application, EditorRuntimeLayer* runtime)
{
    m_application = application;
    m_runtime = runtime;
    if (m_runtime)
        setScene(m_runtime->scene());

    m_loggerSinkId = Logger::addSink([this](const LogMessage& message) {
        const char* level = "INFO";
        if (message.level == LogLevel::Trace) level = "TRACE";
        if (message.level == LogLevel::Warn) level = "WARN";
        if (message.level == LogLevel::Error) level = "ERROR";
        if (message.level == LogLevel::Fatal) level = "FATAL";
        const QString line = QStringLiteral("[%1] %2")
                                 .arg(QString::fromLatin1(level), QString::fromStdString(message.message));
        QMetaObject::invokeMethod(m_console, [this, line] { m_console->appendPlainText(line); }, Qt::QueuedConnection);
    });
}

void QtEditorWindow::setScene(std::shared_ptr<Scene> scene)
{
    m_scene = std::move(scene);
    m_selectedEntity = NULL_ENTITY;
    if (m_runtime && m_runtime->scene() != m_scene)
        m_runtime->setScene(m_scene);
    rebuildHierarchy();
    rebuildInspector();
    rebuildMaterialEditor();
}

void QtEditorWindow::buildMenus()
{
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    auto* newAction = file->addAction(QStringLiteral("New Scene"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [this] { newScene(); });
    auto* openAction = file->addAction(QStringLiteral("Open Scene..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] { openScene(); });
    auto* saveAction = file->addAction(QStringLiteral("Save Scene"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] { saveScene(false); });
    auto* saveAsAction = file->addAction(QStringLiteral("Save Scene As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this] { saveScene(true); });
    file->addSeparator();
    connect(file->addAction(QStringLiteral("Project Settings...")), &QAction::triggered,
            this, [this] { showProjectSettings(); });
    connect(file->addAction(QStringLiteral("Build Windows Package...")), &QAction::triggered,
            this, [this] { showBuildProgress(); });
    file->addSeparator();
    connect(file->addAction(QStringLiteral("Exit")), &QAction::triggered, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    connect(edit->addAction(QStringLiteral("Create Empty Entity")), &QAction::triggered,
            this, [this] { createEntity(QStringLiteral("Entity")); });
    connect(edit->addAction(QStringLiteral("Delete Selected")), &QAction::triggered,
            this, [this] { deleteSelectedEntity(); });

    auto* view = menuBar()->addMenu(QStringLiteral("&View"));
    for (auto* dock : findChildren<QDockWidget*>())
        view->addAction(dock->toggleViewAction());
}

void QtEditorWindow::buildToolbar()
{
    auto* toolbar = addToolBar(QStringLiteral("Editor"));
    toolbar->setMovable(false);
    connect(toolbar->addAction(QStringLiteral("Play")), &QAction::triggered, this, [this] { toggleRuntime(); });
    connect(toolbar->addAction(QStringLiteral("Pause")), &QAction::triggered, this, [this] { togglePause(); });
    toolbar->addSeparator();
    connect(toolbar->addAction(QStringLiteral("Add Entity")), &QAction::triggered,
            this, [this] { createEntity(QStringLiteral("Entity")); });
    connect(toolbar->addAction(QStringLiteral("Delete")), &QAction::triggered,
            this, [this] { deleteSelectedEntity(); });
    toolbar->addSeparator();

    auto* tools = new QActionGroup(this);
    tools->setExclusive(true);
    auto addTool = [this, toolbar, tools](const QString& text, EditorRuntimeLayer::TransformTool tool,
                                          const QKeySequence& shortcut) {
        QAction* action = toolbar->addAction(text);
        action->setCheckable(true);
        action->setShortcut(shortcut);
        tools->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool] {
            if (m_runtime) m_runtime->setTransformTool(tool);
        });
        return action;
    };
    addTool(QStringLiteral("Move"), EditorRuntimeLayer::TransformTool::Translate, QKeySequence(Qt::Key_W))->setChecked(true);
    addTool(QStringLiteral("Rotate"), EditorRuntimeLayer::TransformTool::Rotate, QKeySequence(Qt::Key_E));
    addTool(QStringLiteral("Scale"), EditorRuntimeLayer::TransformTool::Scale, QKeySequence(Qt::Key_R));
    toolbar->addSeparator();
    auto* grid = toolbar->addAction(QStringLiteral("Grid"));
    grid->setCheckable(true);
    grid->setChecked(true);
    connect(grid, &QAction::toggled, this, [this](bool visible) {
        if (m_runtime) m_runtime->setGridVisible(visible);
    });
    auto* snap = toolbar->addAction(QStringLiteral("Snap"));
    snap->setCheckable(true);
    connect(snap, &QAction::toggled, this, [this](bool enabled) {
        if (m_runtime) m_runtime->setSnapEnabled(enabled);
    });
    connect(toolbar->addAction(QStringLiteral("Frame Selected")), &QAction::triggered, this, [this] {
        if (m_runtime) m_runtime->focusSelected();
    });
}

void QtEditorWindow::buildDocks()
{
    auto makeDock = [this](const QString& title, QWidget* widget, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(title);
        dock->setWidget(widget);
        addDockWidget(area, dock);
        return dock;
    };

    auto* hierarchy = new EntityTreeWidget;
    m_hierarchy = hierarchy;
    m_hierarchy->setHeaderLabel(QStringLiteral("Scene"));
    m_hierarchy->setAlternatingRowColors(true);
    m_hierarchy->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_hierarchy->setDragEnabled(true);
    m_hierarchy->setAcceptDrops(true);
    m_hierarchy->setDropIndicatorShown(true);
    m_hierarchy->setDefaultDropAction(Qt::MoveAction);
    hierarchy->reparentEntity = [this](EntityID child, EntityID parent) {
        if (!m_scene || !m_scene->setParent(child, parent))
            return false;
        m_sceneDirty = true;
        rebuildHierarchy();
        selectEntity(child);
        return true;
    };
    m_hierarchy->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto* addEntityAction = m_hierarchy->addAction(QStringLiteral("Create Empty Entity"));
    connect(addEntityAction, &QAction::triggered, this, [this] { createEntity(QStringLiteral("Entity")); });
    auto* addChildAction = m_hierarchy->addAction(QStringLiteral("Create Child Entity"));
    connect(addChildAction, &QAction::triggered, this, [this] {
        const EntityID parent = m_selectedEntity;
        createEntity(QStringLiteral("Entity"));
        if (m_scene && parent != NULL_ENTITY) {
            m_scene->setParent(m_selectedEntity, parent);
            rebuildHierarchy();
            selectEntity(m_selectedEntity);
        }
    });
    auto* deleteAction = m_hierarchy->addAction(QStringLiteral("Delete"));
    connect(deleteAction, &QAction::triggered, this, [this] { deleteSelectedEntity(); });
    auto* duplicateAction = m_hierarchy->addAction(QStringLiteral("Duplicate"));
    duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, this, [this] {
        if (!m_scene || m_selectedEntity == NULL_ENTITY)
            return;
        Entity duplicate = m_scene->duplicateEntity(m_selectedEntity);
        m_sceneDirty = true;
        rebuildHierarchy();
        selectEntity(duplicate.getID());
    });
    auto* renameAction = m_hierarchy->addAction(QStringLiteral("Rename"));
    renameAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(renameAction, &QAction::triggered, this, [this] {
        if (auto* item = m_hierarchy->currentItem())
            m_hierarchy->editItem(item, 0);
    });
    auto* unparentAction = m_hierarchy->addAction(QStringLiteral("Move to Scene Root"));
    connect(unparentAction, &QAction::triggered, this, [this] {
        if (m_scene && m_selectedEntity != NULL_ENTITY && m_scene->setParent(m_selectedEntity, NULL_ENTITY)) {
            m_sceneDirty = true;
            rebuildHierarchy();
            selectEntity(m_selectedEntity);
        }
    });
    connect(m_hierarchy, &QTreeWidget::itemSelectionChanged, this, [this] {
        QTreeWidgetItem* item = m_hierarchy->currentItem();
        if (!item && !m_hierarchy->selectedItems().isEmpty())
            item = m_hierarchy->selectedItems().constFirst();
        if (item)
            selectEntity(item->data(0, Qt::UserRole).toULongLong());
    });
    connect(m_hierarchy, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (!m_scene || !item || column != 0)
            return;
        const EntityID id = item->data(0, Qt::UserRole).toULongLong();
        if (auto* tag = m_scene->getComponent<TagComponent>(id)) {
            tag->tag = item->text(0).toStdString();
            m_sceneDirty = true;
            if (id == m_selectedEntity)
                rebuildInspector();
        }
    });
    auto* hierarchyDock = makeDock(QStringLiteral("Hierarchy"), m_hierarchy, Qt::LeftDockWidgetArea);

    m_inspector = new QtComponentsPanel;
    m_inspector->setChangedCallback([this] { m_sceneDirty = true; });
    m_inspector->setHierarchyChangedCallback([this] { rebuildHierarchy(); });
    auto* inspectorDock = makeDock(QStringLiteral("Inspector"), m_inspector, Qt::RightDockWidgetArea);

    m_fileModel = new QFileSystemModel(this);
    m_assetsRoot = m_projectRoot.empty() ? std::filesystem::absolute("assets") : m_projectRoot / "assets";
    std::error_code ec;
    std::filesystem::create_directories(m_assetsRoot, ec);
    m_fileModel->setReadOnly(false);
    m_fileModel->setRootPath(qPath(m_assetsRoot));
    m_contentBrowser = new QTreeView;
    m_contentBrowser->setModel(m_fileModel);
    m_contentBrowser->setRootIndex(m_fileModel->index(qPath(m_assetsRoot)));
    m_contentBrowser->setSortingEnabled(true);
    m_contentBrowser->setAlternatingRowColors(true);
    m_contentBrowser->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_contentBrowser->setDragEnabled(true);
    m_contentBrowser->setAcceptDrops(true);
    m_contentBrowser->setDropIndicatorShown(true);
    m_contentBrowser->setDragDropMode(QAbstractItemView::DragDrop);
    m_contentBrowser->setDefaultDropAction(Qt::MoveAction);
    m_contentBrowser->setContextMenuPolicy(Qt::CustomContextMenu);
    m_contentBrowser->header()->setStretchLastSection(false);
    m_contentBrowser->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    connect(m_contentBrowser, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        const QString path = m_fileModel->filePath(index);
        if (!m_fileModel->isDir(index))
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(m_contentBrowser, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        showContentBrowserContextMenu(position);
    });

    auto* newFolderAction = new QAction(QStringLiteral("New Folder"), m_contentBrowser);
    newFolderAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    connect(newFolderAction, &QAction::triggered, this, [this] { createContentFolder(); });
    m_contentBrowser->addAction(newFolderAction);
    auto* renameContentAction = new QAction(QStringLiteral("Rename"), m_contentBrowser);
    renameContentAction->setShortcut(QKeySequence(Qt::Key_F2));
    connect(renameContentAction, &QAction::triggered, this, [this] { renameContentItem(); });
    m_contentBrowser->addAction(renameContentAction);
    auto* deleteContentAction = new QAction(QStringLiteral("Delete"), m_contentBrowser);
    deleteContentAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(deleteContentAction, &QAction::triggered, this, [this] { deleteContentItem(); });
    m_contentBrowser->addAction(deleteContentAction);
    auto* contentDock = makeDock(QStringLiteral("Content Browser"), m_contentBrowser, Qt::BottomDockWidgetArea);

    m_console = new QPlainTextEdit;
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(4000);
    auto* consoleDock = makeDock(QStringLiteral("Console"), m_console, Qt::BottomDockWidgetArea);

    m_stats = new QLabel(QStringLiteral("Waiting for renderer..."));
    m_stats->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_stats->setMargin(8);
    auto* statsDock = makeDock(QStringLiteral("Stats"), m_stats, Qt::RightDockWidgetArea);

    auto* environment = new QWidget;
    auto* environmentLayout = new QVBoxLayout(environment);
    environmentLayout->setContentsMargins(8, 8, 8, 8);
    environmentLayout->setSpacing(8);
    auto* postGroup = new QGroupBox(QStringLiteral("Post Processing"));
    auto* postForm = new QFormLayout(postGroup);
    auto* postEnabled = new QCheckBox(QStringLiteral("Post Processing"));
    auto* taa = new QCheckBox(QStringLiteral("Temporal AA"));
    auto* motionBlur = new QCheckBox(QStringLiteral("Motion Blur"));
    auto* bloom = new QCheckBox(QStringLiteral("Bloom"));
    auto* ssgi = new QCheckBox(QStringLiteral("Screen-Space GI"));
    auto* ssao = new QCheckBox(QStringLiteral("SSAO"));
    postEnabled->setChecked(true);
    taa->setChecked(true);
    motionBlur->setChecked(true);
    bloom->setChecked(true);
    ssgi->setChecked(true);
    ssao->setChecked(true);
    postForm->addRow(postEnabled);
    postForm->addRow(taa);
    postForm->addRow(motionBlur);
    postForm->addRow(bloom);
    postForm->addRow(ssgi);
    postForm->addRow(ssao);

    auto* taaFeedback = makeSpin(0.92, 0.0, 1.0);
    taaFeedback->setSingleStep(0.01);
    postForm->addRow(QStringLiteral("TAA Feedback"), taaFeedback);
    auto* bloomThreshold = makeSpin(1.15, 0.0, 20.0);
    auto* bloomIntensity = makeSpin(0.30, 0.0, 10.0);
    postForm->addRow(QStringLiteral("Bloom Threshold"), bloomThreshold);
    postForm->addRow(QStringLiteral("Bloom Intensity"), bloomIntensity);
    auto* ssgiRadius = makeSpin(2.4, 0.25, 8.0);
    auto* ssgiIntensity = makeSpin(0.85, 0.0, 3.0);
    postForm->addRow(QStringLiteral("SSGI Radius"), ssgiRadius);
    postForm->addRow(QStringLiteral("SSGI Intensity"), ssgiIntensity);
    auto* exposure = makeSpin(1.15, 0.01, 20.0);
    postForm->addRow(QStringLiteral("Exposure"), exposure);
    environmentLayout->addWidget(postGroup);

    auto* atmosphereGroup = new QGroupBox(QStringLiteral("Atmosphere"));
    auto* atmosphereForm = new QFormLayout(atmosphereGroup);
    auto* volumetric = new QCheckBox(QStringLiteral("Volumetric Fog"));
    auto* clouds = new QCheckBox(QStringLiteral("Volumetric Clouds"));
    auto* lensFlare = new QCheckBox(QStringLiteral("Lens Flare"));
    atmosphereForm->addRow(volumetric);
    atmosphereForm->addRow(clouds);
    atmosphereForm->addRow(lensFlare);
    environmentLayout->addWidget(atmosphereGroup);
    environmentLayout->addStretch();
    connect(postEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().setEnabled(enabled);
    });
    connect(taa, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().taa.enabled = enabled;
    });
    connect(motionBlur, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().motionBlur.enabled = enabled;
    });
    connect(bloom, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().bloom.enabled = enabled;
    });
    connect(ssgi, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().ssgi.enabled = enabled;
    });
    connect(ssao, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().ssao.enabled = enabled;
    });
    connect(taaFeedback, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().taa.feedback = static_cast<float>(value);
    });
    connect(bloomThreshold, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().bloom.threshold = static_cast<float>(value);
    });
    connect(bloomIntensity, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().bloom.intensity = static_cast<float>(value);
    });
    connect(ssgiRadius, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().ssgi.radius = static_cast<float>(value);
    });
    connect(ssgiIntensity, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().ssgi.intensity = static_cast<float>(value);
    });
    connect(exposure, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().aces.exposure = static_cast<float>(value);
    });
    connect(volumetric, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().volumetric.enabled = enabled;
    });
    connect(clouds, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().clouds.enabled = enabled;
    });
    connect(lensFlare, &QCheckBox::toggled, this, [this](bool enabled) {
        if (m_application) m_application->getRenderer().getPostProcessing().settings().lensFlare.enabled = enabled;
    });
    auto* environmentDock = makeDock(QStringLiteral("Environment"), environment, Qt::RightDockWidgetArea);

    m_materialEditor = new QScrollArea;
    m_materialEditor->setWidgetResizable(true);
    auto* materialDock = makeDock(QStringLiteral("Material Editor"), m_materialEditor, Qt::RightDockWidgetArea);

    tabifyDockWidget(contentDock, consoleDock);
    tabifyDockWidget(inspectorDock, materialDock);
    tabifyDockWidget(materialDock, environmentDock);
    tabifyDockWidget(environmentDock, statsDock);
    inspectorDock->raise();
    contentDock->raise();
    resizeDocks({hierarchyDock, inspectorDock}, {280, 390}, Qt::Horizontal);
    resizeDocks({contentDock}, {230}, Qt::Vertical);
}

void QtEditorWindow::applyTheme()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QDialog { background: #18191b; color: #e8e8e8; }
        QMenuBar, QMenu, QToolBar, QStatusBar, QDockWidget::title { background: #202225; color: #eeeeee; }
        QDockWidget { color: #eeeeee; }
        QTreeView, QTreeWidget, QPlainTextEdit, QScrollArea, QWidget { background: #1d1f22; color: #e5e5e5; }
        QLineEdit, QDoubleSpinBox { background: #2a2d31; color: #f0f0f0; border: 1px solid #44484e; padding: 3px; }
        QPushButton { background: #30343a; border: 1px solid #4a4f56; padding: 5px 10px; }
        QPushButton:hover { background: #3b4149; }
        QTreeView::item:selected, QTreeWidget::item:selected { background: #3a628f; }
        QFrame#componentCard { background: #24272b; border: 1px solid #353a40; border-radius: 5px; }
        QLabel#componentTitle { color: #f2f4f7; font-size: 13px; font-weight: 600; }
        QToolButton#removeComponent { color: #b9bec6; border: none; padding: 2px 5px; }
        QToolButton#removeComponent:hover { color: #ff8d8d; background: #3b292b; border-radius: 3px; }
        QPushButton#addComponentButton { background: #315f8f; border: 1px solid #477cac; font-weight: 600; padding: 7px; }
        QPushButton#addComponentButton:hover { background: #3b70a6; }
        QLabel#emptyPanelMessage { color: #868c95; padding: 24px; }
        QComboBox, QSpinBox { background: #2a2d31; color: #f0f0f0; border: 1px solid #44484e; padding: 3px; }
        QScrollBar:vertical { background: #1b1d20; width: 10px; }
        QScrollBar::handle:vertical { background: #4a4f57; border-radius: 4px; min-height: 24px; }
    )"));
}

void QtEditorWindow::seedDefaultScene()
{
    if (!m_scene)
        return;
    auto cameraEntity = m_scene->createEntity("Camera");
    auto& cameraTransform = cameraEntity.addComponent<TransformComponent>();
    cameraTransform.translation = {0.0f, 2.5f, 6.0f};
    cameraTransform.rotation = {-18.0f, 0.0f, 0.0f};
    cameraEntity.addComponent<CameraComponent>();

    auto lightEntity = m_scene->createEntity("Directional Light");
    lightEntity.addComponent<TransformComponent>().rotation = {-45.0f, 45.0f, 0.0f};
    auto& light = lightEntity.addComponent<LightComponent>();
    light.type = LightType::Directional;
    light.intensity = 2.0f;

    auto ground = m_scene->createEntity("Ground");
    ground.addComponent<TransformComponent>();
    ground.addComponent<MeshRendererComponent>().meshPath = "builtin:plane";
    auto& groundMaterial = ground.addComponent<MaterialComponent>();
    groundMaterial.albedoColor = {0.28f, 0.30f, 0.34f, 1.0f};
    groundMaterial.roughness = 0.95f;

    auto cube = m_scene->createEntity("Cube");
    cube.addComponent<TransformComponent>().translation = {0.0f, 0.5f, 0.0f};
    cube.addComponent<MeshRendererComponent>().meshPath = "builtin:cube";
    auto& cubeMaterial = cube.addComponent<MaterialComponent>();
    cubeMaterial.albedoColor = {0.75f, 0.18f, 0.18f, 1.0f};
    cubeMaterial.roughness = 0.45f;
}

void QtEditorWindow::rebuildHierarchy()
{
    const QSignalBlocker blocker(m_hierarchy);
    m_hierarchy->clear();
    if (!m_scene)
        return;

    std::function<void(EntityID, QTreeWidgetItem*)> addEntity = [&](EntityID id, QTreeWidgetItem* parent) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_hierarchy);
        item->setText(0, entityName(m_scene, id));
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(id));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (id == m_selectedEntity)
            m_hierarchy->setCurrentItem(item);
        for (EntityID child : m_scene->getChildren(id))
            addEntity(child, item);
    };

    for (EntityID id : m_scene->getEntities()) {
        if (m_scene->getParent(id) == NULL_ENTITY)
            addEntity(id, nullptr);
    }
    m_hierarchy->expandAll();
}

void QtEditorWindow::rebuildInspector()
{
    m_inspector->setScene(m_scene);
    m_inspector->setSelectedEntity(m_selectedEntity);
}

void QtEditorWindow::rebuildMaterialEditor()
{
    QFormLayout* form = nullptr;
    QWidget* content = formContainer(&form);
    m_materialEditor->setWidget(content);
    if (!m_scene || m_selectedEntity == NULL_ENTITY) {
        form->addRow(new QLabel(QStringLiteral("Select an entity with a material")));
        return;
    }

    auto* material = m_scene->getComponent<MaterialComponent>(m_selectedEntity);
    if (!material) {
        auto* add = new QPushButton(QStringLiteral("Add Material Override"));
        connect(add, &QPushButton::clicked, this, [this] {
            m_scene->addComponent(m_selectedEntity, MaterialComponent{});
            m_sceneDirty = true;
            rebuildMaterialEditor();
        });
        form->addRow(add);
        return;
    }

    auto addMaterialSpin = [this, form, material](const QString& label, float& field, double min, double max) {
        float* fieldPtr = &field;
        auto* spin = makeSpin(field, min, max);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, material, fieldPtr](double value) {
            *fieldPtr = static_cast<float>(value);
            material->dirty = true;
            m_sceneDirty = true;
        });
        form->addRow(label, spin);
    };
    addMaterialSpin(QStringLiteral("Roughness"), material->roughness, 0.0, 1.0);
    addMaterialSpin(QStringLiteral("Metallic"), material->metallic, 0.0, 1.0);
    addMaterialSpin(QStringLiteral("AO"), material->ao, 0.0, 1.0);
    addMaterialSpin(QStringLiteral("Emissive"), material->emissiveStrength, 0.0, 100.0);
    auto* albedo = new QWidget;
    auto* albedoLayout = new QHBoxLayout(albedo);
    albedoLayout->setContentsMargins(0, 0, 0, 0);
    float* channels[] = { &material->albedoColor.r, &material->albedoColor.g, &material->albedoColor.b };
    for (float* channel : channels) {
        auto* spin = makeSpin(*channel, 0.0, 1.0);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, material, channel](double value) {
            *channel = static_cast<float>(value);
            material->dirty = true;
            m_sceneDirty = true;
        });
        albedoLayout->addWidget(spin);
    }
    form->addRow(QStringLiteral("Albedo RGB"), albedo);
}

void QtEditorWindow::refreshStats()
{
    if (!m_application)
        return;
    const auto& renderStats = m_application->getRenderer().getStats();
    m_stats->setText(QStringLiteral(
        "Draw calls: %1\nVertices: %2\nIndices: %3\nCulled: %4\nGPU: %5 ms\nViewport: %6 x %7")
        .arg(renderStats.drawCalls)
        .arg(renderStats.vertexCount)
        .arg(renderStats.indexCount)
        .arg(renderStats.culledCount)
        .arg(renderStats.gpuTimeMs, 0, 'f', 2)
        .arg(m_application->getWindow().getWidth())
        .arg(m_application->getWindow().getHeight()));
}

void QtEditorWindow::refreshAll()
{
    rebuildHierarchy();
    rebuildInspector();
    rebuildMaterialEditor();
    refreshStats();
}

void QtEditorWindow::selectEntity(EntityID id)
{
    m_selectedEntity = id;
    if (m_runtime)
        m_runtime->setSelectedEntity(id);
    {
        const QSignalBlocker blocker(m_hierarchy);
        if (id == NULL_ENTITY) {
            m_hierarchy->clearSelection();
            m_hierarchy->setCurrentItem(nullptr);
        } else {
            QTreeWidgetItemIterator iterator(m_hierarchy);
            while (*iterator) {
                if ((*iterator)->data(0, Qt::UserRole).toULongLong() == id) {
                    m_hierarchy->setCurrentItem(*iterator);
                    m_hierarchy->scrollToItem(*iterator);
                    break;
                }
                ++iterator;
            }
        }
    }
    rebuildInspector();
    rebuildMaterialEditor();
}

void QtEditorWindow::showViewportContextMenu(const QPoint& globalPosition)
{
    QMenu menu(this);
    auto* createMenu = menu.addMenu(QStringLiteral("Create"));
    connect(createMenu->addAction(QStringLiteral("Empty Entity")), &QAction::triggered,
            this, [this] { createEntity(QStringLiteral("Entity")); });
    connect(createMenu->addAction(QStringLiteral("Cube")), &QAction::triggered, this, [this] {
        createEntity(QStringLiteral("Cube"));
        m_scene->addComponent(m_selectedEntity, MeshRendererComponent{.meshPath = "builtin:cube"});
        m_scene->addComponent(m_selectedEntity, MaterialComponent{});
        rebuildInspector();
    });
    connect(createMenu->addAction(QStringLiteral("Sphere")), &QAction::triggered, this, [this] {
        createEntity(QStringLiteral("Sphere"));
        m_scene->addComponent(m_selectedEntity, MeshRendererComponent{.meshPath = "builtin:sphere"});
        m_scene->addComponent(m_selectedEntity, MaterialComponent{});
        rebuildInspector();
    });
    connect(createMenu->addAction(QStringLiteral("Directional Light")), &QAction::triggered, this, [this] {
        createEntity(QStringLiteral("Directional Light"));
        auto* light = m_scene->addComponent(m_selectedEntity, LightComponent{});
        light->type = LightType::Directional;
        rebuildInspector();
    });
    connect(createMenu->addAction(QStringLiteral("Point Light")), &QAction::triggered, this, [this] {
        createEntity(QStringLiteral("Point Light"));
        m_scene->addComponent(m_selectedEntity, LightComponent{});
        rebuildInspector();
    });
    connect(createMenu->addAction(QStringLiteral("Camera")), &QAction::triggered, this, [this] {
        createEntity(QStringLiteral("Camera"));
        m_scene->addComponent(m_selectedEntity, CameraComponent{});
        rebuildInspector();
    });

    menu.addSeparator();
    auto* toolMenu = menu.addMenu(QStringLiteral("Transform Tool"));
    auto addTool = [this, toolMenu](const QString& name, EditorRuntimeLayer::TransformTool tool) {
        QAction* action = toolMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(m_runtime && m_runtime->transformTool() == tool);
        connect(action, &QAction::triggered, this, [this, tool] {
            if (m_runtime) m_runtime->setTransformTool(tool);
        });
    };
    addTool(QStringLiteral("Move"), EditorRuntimeLayer::TransformTool::Translate);
    addTool(QStringLiteral("Rotate"), EditorRuntimeLayer::TransformTool::Rotate);
    addTool(QStringLiteral("Scale"), EditorRuntimeLayer::TransformTool::Scale);
    QAction* grid = menu.addAction(QStringLiteral("Show Infinite Grid"));
    grid->setCheckable(true);
    grid->setChecked(m_runtime && m_runtime->isGridVisible());
    connect(grid, &QAction::toggled, this, [this](bool visible) {
        if (m_runtime) m_runtime->setGridVisible(visible);
    });
    QAction* snap = menu.addAction(QStringLiteral("Snap Transform"));
    snap->setCheckable(true);
    snap->setChecked(m_runtime && m_runtime->isSnapEnabled());
    connect(snap, &QAction::toggled, this, [this](bool enabled) {
        if (m_runtime) m_runtime->setSnapEnabled(enabled);
    });

    menu.addSeparator();
    QAction* focus = menu.addAction(QStringLiteral("Frame Selected"));
    focus->setEnabled(m_selectedEntity != NULL_ENTITY);
    connect(focus, &QAction::triggered, this, [this] {
        if (m_runtime) m_runtime->focusSelected();
    });
    QAction* duplicate = menu.addAction(QStringLiteral("Duplicate Selected"));
    duplicate->setEnabled(m_selectedEntity != NULL_ENTITY);
    connect(duplicate, &QAction::triggered, this, [this] {
        if (!m_scene || m_selectedEntity == NULL_ENTITY) return;
        Entity copy = m_scene->duplicateEntity(m_selectedEntity);
        m_sceneDirty = true;
        rebuildHierarchy();
        selectEntity(copy.getID());
    });
    QAction* remove = menu.addAction(QStringLiteral("Delete Selected"));
    remove->setEnabled(m_selectedEntity != NULL_ENTITY);
    connect(remove, &QAction::triggered, this, [this] { deleteSelectedEntity(); });
    menu.exec(globalPosition);
}

std::filesystem::path QtEditorWindow::selectedContentPath() const
{
    if (!m_contentBrowser || !m_fileModel || !m_contentBrowser->currentIndex().isValid())
        return {};
    return fsPath(m_fileModel->filePath(m_contentBrowser->currentIndex()));
}

std::filesystem::path QtEditorWindow::contentBrowserDirectory() const
{
    std::filesystem::path path = selectedContentPath();
    std::error_code ec;
    if (path.empty())
        return m_assetsRoot;
    if (!std::filesystem::is_directory(path, ec))
        path = path.parent_path();
    return path.empty() ? m_assetsRoot : path;
}

void QtEditorWindow::showContentBrowserContextMenu(const QPoint& position)
{
    const QModelIndex index = m_contentBrowser->indexAt(position);
    if (index.isValid())
        m_contentBrowser->setCurrentIndex(index);

    QMenu menu(this);
    auto* createMenu = menu.addMenu(QStringLiteral("Create"));
    connect(createMenu->addAction(QStringLiteral("Folder")), &QAction::triggered,
            this, [this] { createContentFolder(); });
    connect(createMenu->addAction(QStringLiteral("Empty File...")), &QAction::triggered,
            this, [this] { createContentFile("NewFile.txt", ""); });
    connect(createMenu->addAction(QStringLiteral("C++ Script")), &QAction::triggered,
            this, [this] { createContentFile("NewScript.cpp",
                "#include \"core/DemonPCH.h\"\n\nnamespace Demon {\n\n// Script implementation.\n\n} // namespace Demon\n"); });
    connect(createMenu->addAction(QStringLiteral("Material")), &QAction::triggered,
            this, [this] { createContentFile("NewMaterial.material",
                "{\n  \"albedo\": [1.0, 1.0, 1.0, 1.0],\n  \"metallic\": 0.0,\n  \"roughness\": 0.5\n}\n"); });
    connect(createMenu->addAction(QStringLiteral("Shader")), &QAction::triggered,
            this, [this] { createContentFile("NewShader.hlsl",
                "float4 main() : SV_Target0\n{\n    return float4(1.0, 1.0, 1.0, 1.0);\n}\n"); });
    connect(createMenu->addAction(QStringLiteral("Scene")), &QAction::triggered,
            this, [this] {
                const auto directory = contentBrowserDirectory();
                bool accepted = false;
                const QString name = QInputDialog::getText(this, QStringLiteral("Create Scene"),
                    QStringLiteral("File name"), QLineEdit::Normal, QStringLiteral("NewScene.demon"), &accepted).trimmed();
                if (!accepted || name.isEmpty()) return;
                const auto path = directory / fsPath(name);
                if (std::filesystem::exists(path)) {
                    QMessageBox::warning(this, QStringLiteral("Content Browser"), QStringLiteral("That file already exists."));
                    return;
                }
                SceneSerializer serializer(Scene::create(path.stem().string()));
                if (!serializer.serialize(path.string()))
                    QMessageBox::critical(this, QStringLiteral("Content Browser"), QStringLiteral("Could not create the scene file."));
                refreshContentBrowser();
            });

    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Import Files...")), &QAction::triggered,
            this, [this] { importContentFiles(); });
    connect(menu.addAction(QStringLiteral("Refresh")), &QAction::triggered,
            this, [this] { refreshContentBrowser(); });

    const std::filesystem::path selected = selectedContentPath();
    if (!selected.empty()) {
        menu.addSeparator();
        QAction* open = menu.addAction(QStringLiteral("Open"));
        connect(open, &QAction::triggered, this, [selected] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(qPath(selected)));
        });
        QAction* reveal = menu.addAction(QStringLiteral("Reveal in File Explorer"));
        connect(reveal, &QAction::triggered, this, [selected] {
            const auto target = std::filesystem::is_directory(selected) ? selected : selected.parent_path();
            QDesktopServices::openUrl(QUrl::fromLocalFile(qPath(target)));
        });
        connect(menu.addAction(QStringLiteral("Rename")), &QAction::triggered,
                this, [this] { renameContentItem(); });
        connect(menu.addAction(QStringLiteral("Duplicate")), &QAction::triggered,
                this, [this] { duplicateContentItem(); });
        QAction* remove = menu.addAction(QStringLiteral("Delete"));
        remove->setEnabled(selected.lexically_normal() != m_assetsRoot.lexically_normal());
        connect(remove, &QAction::triggered, this, [this] { deleteContentItem(); });
    }
    menu.exec(m_contentBrowser->viewport()->mapToGlobal(position));
}

void QtEditorWindow::createContentFolder()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Create Folder"),
        QStringLiteral("Folder name"), QLineEdit::Normal, QStringLiteral("New Folder"), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;
    const auto path = contentBrowserDirectory() / fsPath(name);
    std::error_code ec;
    if (!std::filesystem::create_directory(path, ec)) {
        QMessageBox::warning(this, QStringLiteral("Content Browser"),
            ec ? QString::fromStdString(ec.message()) : QStringLiteral("That folder already exists."));
        return;
    }
    refreshContentBrowser();
}

void QtEditorWindow::createContentFile(const std::string& suggestedName, const std::string& contents)
{
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Create File"),
        QStringLiteral("File name"), QLineEdit::Normal, QString::fromStdString(suggestedName), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;
    const auto path = contentBrowserDirectory() / fsPath(name);
    if (std::filesystem::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("Content Browser"), QStringLiteral("That file already exists."));
        return;
    }
    std::ofstream output(path, std::ios::binary);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        QMessageBox::critical(this, QStringLiteral("Content Browser"), QStringLiteral("The file could not be created."));
        return;
    }
    refreshContentBrowser();
}

void QtEditorWindow::renameContentItem()
{
    const auto source = selectedContentPath();
    if (source.empty() || source.lexically_normal() == m_assetsRoot.lexically_normal())
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename Asset"),
        QStringLiteral("New name"), QLineEdit::Normal, qPath(source.filename()), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;
    std::error_code ec;
    std::filesystem::rename(source, source.parent_path() / fsPath(name), ec);
    if (ec)
        QMessageBox::critical(this, QStringLiteral("Content Browser"), QString::fromStdString(ec.message()));
    refreshContentBrowser();
}

void QtEditorWindow::duplicateContentItem()
{
    const auto source = selectedContentPath();
    if (source.empty() || source.lexically_normal() == m_assetsRoot.lexically_normal())
        return;
    const bool directory = std::filesystem::is_directory(source);
    const std::string base = source.stem().string() + " Copy";
    const std::string extension = directory ? std::string{} : source.extension().string();
    std::filesystem::path destination = source.parent_path() / (base + extension);
    for (int suffix = 2; std::filesystem::exists(destination); ++suffix)
        destination = source.parent_path() / (base + " " + std::to_string(suffix) + extension);
    std::error_code ec;
    if (directory)
        std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, ec);
    else
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
    if (ec)
        QMessageBox::critical(this, QStringLiteral("Content Browser"), QString::fromStdString(ec.message()));
    refreshContentBrowser();
}

void QtEditorWindow::deleteContentItem()
{
    const auto path = selectedContentPath();
    if (path.empty() || path.lexically_normal() == m_assetsRoot.lexically_normal())
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete Asset"),
            QStringLiteral("Delete '%1'?\nThis cannot be undone.").arg(qPath(path.filename()))) != QMessageBox::Yes)
        return;
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec)
        QMessageBox::critical(this, QStringLiteral("Content Browser"), QString::fromStdString(ec.message()));
    refreshContentBrowser();
}

void QtEditorWindow::importContentFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import Assets"),
        QString(), QStringLiteral("Supported Assets (*.fbx *.obj *.gltf *.glb *.png *.jpg *.jpeg *.tga *.hdr *.exr *.wav *.ogg *.json *.material *.hlsl);;All Files (*)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    const auto directory = contentBrowserDirectory();
    for (const QString& file : files) {
        const std::filesystem::path source = fsPath(file);
        std::filesystem::path destination = directory / source.filename();
        if (std::filesystem::exists(destination)) {
            if (QMessageBox::question(this, QStringLiteral("Import Asset"),
                    QStringLiteral("Replace '%1'?").arg(qPath(destination.filename()))) != QMessageBox::Yes)
                continue;
        }
        std::error_code ec;
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            QMessageBox::warning(this, QStringLiteral("Import Asset"), QString::fromStdString(ec.message()));
    }
    refreshContentBrowser();
}

void QtEditorWindow::refreshContentBrowser()
{
    if (!m_fileModel || !m_contentBrowser)
        return;
    m_fileModel->setRootPath(QString());
    m_fileModel->setRootPath(qPath(m_assetsRoot));
    m_contentBrowser->setRootIndex(m_fileModel->index(qPath(m_assetsRoot)));
}

void QtEditorWindow::newScene()
{
    if (m_sceneDirty && QMessageBox::question(this, QStringLiteral("Unsaved Scene"),
            QStringLiteral("Discard the current unsaved scene?")) != QMessageBox::Yes)
        return;
    auto scene = Scene::create("Untitled Scene");
    setScene(scene);
    seedDefaultScene();
    m_scenePath.clear();
    m_sceneDirty = false;
    refreshAll();
}

void QtEditorWindow::openScene()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Scene"),
        qPath(m_projectRoot), QStringLiteral("Demon Scene (*.demon *.json);;All Files (*)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty())
        return;
    auto scene = Scene::create("Loaded Scene");
    SceneSerializer serializer(scene);
    if (!serializer.deserialize(fsPath(path).string())) {
        QMessageBox::critical(this, QStringLiteral("Open Scene"), QStringLiteral("The scene could not be loaded."));
        return;
    }
    setScene(scene);
    m_scenePath = fsPath(path);
    m_sceneDirty = false;
    statusBar()->showMessage(QStringLiteral("Opened %1").arg(path), 3000);
}

bool QtEditorWindow::saveScene(bool saveAs)
{
    if (!m_scene)
        return false;
    if (saveAs || m_scenePath.empty()) {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Scene"),
            qPath(m_projectRoot / "assets" / "Untitled.demon"),
            QStringLiteral("Demon Scene (*.demon);;JSON (*.json)"), nullptr,
            QFileDialog::DontUseNativeDialog);
        if (path.isEmpty())
            return false;
        m_scenePath = fsPath(path);
    }
    SceneSerializer serializer(m_scene);
    if (!serializer.serialize(m_scenePath.string())) {
        QMessageBox::critical(this, QStringLiteral("Save Scene"), QStringLiteral("The scene could not be saved."));
        return false;
    }
    m_sceneDirty = false;
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(qPath(m_scenePath)), 3000);
    return true;
}

void QtEditorWindow::createEntity(const QString& name)
{
    if (!m_scene)
        return;
    Entity entity = m_scene->createEntity(name.toStdString());
    entity.addComponent<TransformComponent>();
    m_sceneDirty = true;
    rebuildHierarchy();
    selectEntity(entity.getID());
}

void QtEditorWindow::deleteSelectedEntity()
{
    if (!m_scene || m_selectedEntity == NULL_ENTITY)
        return;
    m_scene->destroyEntity(m_selectedEntity);
    m_selectedEntity = NULL_ENTITY;
    if (m_runtime)
        m_runtime->setSelectedEntity(NULL_ENTITY);
    m_sceneDirty = true;
    refreshAll();
}

void QtEditorWindow::toggleRuntime()
{
    if (!m_runtime)
        return;
    if (!m_runtime->isPlaying()) {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Enter Runtime Mode"));
        auto* layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(QStringLiteral("Run a temporary copy of the current scene?\nChanges made in runtime mode are discarded.")));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;

        QProgressDialog progress(QStringLiteral("Preparing runtime scene..."), QString(), 0, 3, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setCancelButton(nullptr);
        progress.setValue(1);
        QApplication::processEvents();
        m_runtime->setPlaying(true);
        setScene(m_runtime->scene());
        progress.setValue(3);
        statusBar()->showMessage(QStringLiteral("Runtime mode"));
    } else {
        m_runtime->setPlaying(false);
        setScene(m_runtime->scene());
        statusBar()->showMessage(QStringLiteral("Edit mode"), 2000);
    }
}

void QtEditorWindow::togglePause()
{
    if (m_runtime && m_runtime->isPlaying())
        m_runtime->setPaused(!m_runtime->isPaused());
}

void QtEditorWindow::showProjectSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Project Settings"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(QString::fromStdString(m_projectName));
    auto* root = new QLineEdit(qPath(m_projectRoot));
    root->setReadOnly(true);
    form->addRow(QStringLiteral("Project Name"), name);
    form->addRow(QStringLiteral("Project Root"), root);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        m_projectName = name->text().toStdString();
        setWindowTitle(QStringLiteral("DemonEngine - %1").arg(name->text()));
    }
}

void QtEditorWindow::showBuildProgress()
{
    const QString output = QFileDialog::getExistingDirectory(this, QStringLiteral("Build Output"),
        qPath(m_projectRoot), QFileDialog::ShowDirsOnly | QFileDialog::DontUseNativeDialog);
    if (output.isEmpty())
        return;
    QProgressDialog progress(QStringLiteral("Preparing Windows package..."), QString(), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setCancelButton(nullptr);
    for (int value : {10, 35, 65, 100}) {
        progress.setValue(value);
        QApplication::processEvents();
    }
    QMessageBox::information(this, QStringLiteral("Build"),
        QStringLiteral("Build staging completed at:\n%1").arg(output));
}

void QtEditorWindow::closeEvent(QCloseEvent* event)
{
    if (m_sceneDirty) {
        const auto answer = QMessageBox::question(this, QStringLiteral("Unsaved Scene"),
            QStringLiteral("Save changes before closing?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Yes && !saveScene(false)) {
            event->ignore();
            return;
        }
    }
    if (m_application)
        m_application->close();
    event->accept();
}

} // namespace Demon
