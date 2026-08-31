#include "QtComponentsPanel.h"

#include "scene/Components.h"

#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <type_traits>

namespace Demon {
namespace {

using Changed = std::function<void()>;

QDoubleSpinBox* floatField(QObject* context, float& value, const Changed& changed,
                           double minimum = -100000.0, double maximum = 100000.0,
                           double step = 0.1, int decimals = 3)
{
    auto* field = new QDoubleSpinBox;
    field->setRange(minimum, maximum);
    field->setDecimals(decimals);
    field->setSingleStep(step);
    field->setValue(value);
    QObject::connect(field, &QDoubleSpinBox::valueChanged, context, [&value, changed](double next) {
        value = static_cast<float>(next);
        changed();
    });
    return field;
}

QSpinBox* intField(QObject* context, int& value, const Changed& changed,
                   int minimum = -100000, int maximum = 100000)
{
    auto* field = new QSpinBox;
    field->setRange(minimum, maximum);
    field->setValue(value);
    QObject::connect(field, &QSpinBox::valueChanged, context, [&value, changed](int next) {
        value = next;
        changed();
    });
    return field;
}

QSpinBox* uintField(QObject* context, uint32_t& value, const Changed& changed,
                    int minimum = 0, int maximum = 1000000)
{
    auto* field = new QSpinBox;
    field->setRange(minimum, maximum);
    field->setValue(static_cast<int>(std::min<uint32_t>(value, static_cast<uint32_t>(maximum))));
    QObject::connect(field, &QSpinBox::valueChanged, context, [&value, changed](int next) {
        value = static_cast<uint32_t>(next);
        changed();
    });
    return field;
}

QCheckBox* boolField(QObject* context, bool& value, const Changed& changed)
{
    auto* field = new QCheckBox;
    field->setChecked(value);
    QObject::connect(field, &QCheckBox::toggled, context, [&value, changed](bool next) {
        value = next;
        changed();
    });
    return field;
}

QLineEdit* textField(QObject* context, std::string& value, const Changed& changed)
{
    auto* field = new QLineEdit(QString::fromStdString(value));
    QObject::connect(field, &QLineEdit::editingFinished, context, [&value, field, changed] {
        value = field->text().toStdString();
        changed();
    });
    return field;
}

QWidget* pathField(QWidget* context, std::string& value, const Changed& changed,
                   const QString& filter = QStringLiteral("All Files (*)"))
{
    auto* widget = new QWidget;
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    auto* field = textField(context, value, changed);
    auto* browse = new QToolButton;
    browse->setText(QStringLiteral("..."));
    browse->setToolTip(QStringLiteral("Browse asset"));
    QObject::connect(browse, &QToolButton::clicked, context, [context, field, &value, changed, filter] {
        const QString path = QFileDialog::getOpenFileName(context, QStringLiteral("Select Asset"),
            field->text(), filter, nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) {
            field->setText(path);
            value = path.toStdString();
            changed();
        }
    });
    layout->addWidget(field, 1);
    layout->addWidget(browse);
    return widget;
}

QWidget* vectorFields(QObject* context, float* values, int count, const Changed& changed,
                      double minimum = -100000.0, double maximum = 100000.0,
                      double step = 0.1)
{
    auto* widget = new QWidget;
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    static constexpr const char* labels[] = {"X", "Y", "Z", "W"};
    for (int index = 0; index < count; ++index) {
        auto* field = floatField(context, values[index], changed, minimum, maximum, step);
        field->setPrefix(QString::fromLatin1(labels[index]) + QStringLiteral(" "));
        field->setMinimumWidth(0);
        field->setMaximumWidth(count >= 4 ? 78 : 108);
        layout->addWidget(field);
    }
    return widget;
}

QPushButton* colorField(QWidget* context, glm::vec3& color, const Changed& changed)
{
    auto* button = new QPushButton;
    auto updateButton = [button, &color] {
        const QColor qcolor = QColor::fromRgbF(color.r, color.g, color.b);
        button->setText(qcolor.name(QColor::HexRgb).toUpper());
        button->setStyleSheet(QStringLiteral("text-align:left; padding-left:10px; background:%1; color:%2;")
            .arg(qcolor.name(), qcolor.lightnessF() > 0.55 ? QStringLiteral("#111") : QStringLiteral("#fff")));
    };
    updateButton();
    QObject::connect(button, &QPushButton::clicked, context, [context, button, &color, changed, updateButton] {
        const QColor initial = QColor::fromRgbF(color.r, color.g, color.b);
        const QColor next = QColorDialog::getColor(initial, context, QStringLiteral("Select Color"));
        if (next.isValid()) {
            color = {static_cast<float>(next.redF()), static_cast<float>(next.greenF()), static_cast<float>(next.blueF())};
            updateButton();
            changed();
        }
    });
    return button;
}

QPushButton* colorField(QWidget* context, glm::vec4& color, const Changed& changed)
{
    auto* button = new QPushButton;
    auto updateButton = [button, &color] {
        const QColor qcolor = QColor::fromRgbF(color.r, color.g, color.b, color.a);
        button->setText(qcolor.name(QColor::HexArgb).toUpper());
        button->setStyleSheet(QStringLiteral("text-align:left; padding-left:10px; background:%1; color:%2;")
            .arg(qcolor.name(), qcolor.lightnessF() > 0.55 ? QStringLiteral("#111") : QStringLiteral("#fff")));
    };
    updateButton();
    QObject::connect(button, &QPushButton::clicked, context, [context, &color, changed, updateButton] {
        const QColor initial = QColor::fromRgbF(color.r, color.g, color.b, color.a);
        const QColor next = QColorDialog::getColor(initial, context, QStringLiteral("Select Color"),
                                                    QColorDialog::ShowAlphaChannel);
        if (next.isValid()) {
            color = {static_cast<float>(next.redF()), static_cast<float>(next.greenF()),
                     static_cast<float>(next.blueF()), static_cast<float>(next.alphaF())};
            updateButton();
            changed();
        }
    });
    return button;
}

template<typename Enum>
QComboBox* enumField(QObject* context, Enum& value, const QStringList& labels, const Changed& changed)
{
    auto* field = new QComboBox;
    field->addItems(labels);
    field->setCurrentIndex(std::clamp(static_cast<int>(value), 0, static_cast<int>(labels.size()) - 1));
    QObject::connect(field, &QComboBox::currentIndexChanged, context, [&value, changed](int index) {
        value = static_cast<Enum>(index);
        changed();
    });
    return field;
}

QFormLayout* componentCard(QVBoxLayout* parent, const QString& title,
                           const std::function<void()>& remove = {})
{
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("componentCard"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 10);
    cardLayout->setSpacing(7);

    auto* header = new QHBoxLayout;
    auto* label = new QLabel(title);
    label->setObjectName(QStringLiteral("componentTitle"));
    header->addWidget(label);
    header->addStretch();
    if (remove) {
        auto* button = new QToolButton;
        button->setText(QStringLiteral("Remove"));
        button->setObjectName(QStringLiteral("removeComponent"));
        QObject::connect(button, &QToolButton::clicked, card, remove);
        header->addWidget(button);
    }
    cardLayout->addLayout(header);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    cardLayout->addLayout(form);
    parent->addWidget(card);
    return form;
}

} // namespace

QtComponentsPanel::QtComponentsPanel(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void QtComponentsPanel::setScene(std::shared_ptr<Scene> scene)
{
    if (m_scene == scene)
        return;
    m_scene = std::move(scene);
    m_entity = NULL_ENTITY;
    refresh();
}

void QtComponentsPanel::setSelectedEntity(EntityID entity)
{
    m_entity = entity;
    refresh();
}

void QtComponentsPanel::markChanged()
{
    if (m_changed)
        m_changed();
}

void QtComponentsPanel::refresh()
{
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("componentsRoot"));
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);
    setWidget(content);

    if (!m_scene || m_entity == NULL_ENTITY || !m_scene->entityExists(m_entity)) {
        auto* empty = new QLabel(QStringLiteral("Select an entity to inspect its components."));
        empty->setObjectName(QStringLiteral("emptyPanelMessage"));
        empty->setAlignment(Qt::AlignCenter);
        root->addStretch();
        root->addWidget(empty);
        root->addStretch();
        return;
    }

    const Changed changed = [this] { markChanged(); };

    if (auto* tag = m_scene->getComponent<TagComponent>(m_entity)) {
        auto* identity = componentCard(root, QStringLiteral("Entity"));
        auto* name = textField(this, tag->tag, [this] {
            markChanged();
            if (m_hierarchyChanged)
                m_hierarchyChanged();
        });
        identity->addRow(QStringLiteral("Name"), name);
        auto* id = new QLineEdit(QString::number(m_entity));
        id->setReadOnly(true);
        identity->addRow(QStringLiteral("Entity ID"), id);
    }

    auto componentForm = [this, root]<typename T>(const QString& title, T*) {
        return componentCard(root, title, [this] {
            if (!m_scene || m_entity == NULL_ENTITY)
                return;
            m_scene->removeComponent<T>(m_entity);
            markChanged();
            refresh();
        });
    };

    if (auto* transform = m_scene->getComponent<TransformComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Transform"), transform);
        form->addRow(QStringLiteral("Position"), vectorFields(this, &transform->translation.x, 3, changed));
        form->addRow(QStringLiteral("Rotation"), vectorFields(this, &transform->rotation.x, 3, changed, -360.0, 360.0, 1.0));
        form->addRow(QStringLiteral("Scale"), vectorFields(this, &transform->scale.x, 3, changed, 0.0001, 10000.0));
    }

    if (auto* mesh = m_scene->getComponent<MeshRendererComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Mesh Renderer"), mesh);
        form->addRow(QStringLiteral("Mesh"), pathField(this, mesh->meshPath, changed,
            QStringLiteral("Meshes (*.fbx *.obj *.gltf *.glb);;All Files (*)")));
        form->addRow(QStringLiteral("Material"), pathField(this, mesh->materialPath, changed,
            QStringLiteral("Materials (*.material *.json);;All Files (*)")));
        form->addRow(QStringLiteral("Sub Mesh"), intField(this, mesh->subMeshIndex, changed, -1, 10000));
        form->addRow(QStringLiteral("Preserve Hierarchy"), boolField(this, mesh->preserveHierarchy, changed));
        form->addRow(QStringLiteral("Visible"), boolField(this, mesh->visible, changed));
        form->addRow(QStringLiteral("Cast Shadows"), boolField(this, mesh->castShadows, changed));
        form->addRow(QStringLiteral("Receive Shadows"), boolField(this, mesh->receiveShadows, changed));
    }

    if (auto* animator = m_scene->getComponent<AnimatorComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Animator"), animator);
        form->addRow(QStringLiteral("Current Clip"), textField(this, animator->currentClip, changed));
        form->addRow(QStringLiteral("Next Clip"), textField(this, animator->nextClip, changed));
        form->addRow(QStringLiteral("Playing"), boolField(this, animator->playing, changed));
        form->addRow(QStringLiteral("Looping"), boolField(this, animator->looping, changed));
        form->addRow(QStringLiteral("Playback Speed"), floatField(this, animator->playbackSpeed, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Blend Duration"), floatField(this, animator->blendDuration, changed, 0.0, 10.0));
    }

    if (auto* material = m_scene->getComponent<MaterialComponent>(m_entity)) {
        const Changed materialChanged = [this, material] {
            material->dirty = true;
            markChanged();
        };
        auto* form = componentForm(QStringLiteral("Material Override"), material);
        form->addRow(QStringLiteral("Material Asset"), pathField(this, material->materialPath, materialChanged));
        form->addRow(QStringLiteral("Albedo"), colorField(this, material->albedoColor, materialChanged));
        form->addRow(QStringLiteral("Metallic"), floatField(this, material->metallic, materialChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Roughness"), floatField(this, material->roughness, materialChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Ambient Occlusion"), floatField(this, material->ao, materialChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Emissive Color"), colorField(this, material->emissiveColor, materialChanged));
        form->addRow(QStringLiteral("Emissive Strength"), floatField(this, material->emissiveStrength, materialChanged, 0.0, 100.0));
        form->addRow(QStringLiteral("Double Sided"), boolField(this, material->doubleSided, materialChanged));
        form->addRow(QStringLiteral("Alpha Blend"), boolField(this, material->alphaBlend, materialChanged));
        form->addRow(QStringLiteral("Alpha Cutoff"), floatField(this, material->alphaCutoff, materialChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Albedo Texture"), pathField(this, material->albedoTexture, materialChanged));
        form->addRow(QStringLiteral("Normal Texture"), pathField(this, material->normalTexture, materialChanged));
        form->addRow(QStringLiteral("Metallic Texture"), pathField(this, material->metallicTexture, materialChanged));
        form->addRow(QStringLiteral("Emissive Texture"), pathField(this, material->emissiveTexture, materialChanged));
    }

    if (auto* camera = m_scene->getComponent<CameraComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Camera"), camera);
        auto* projection = new QComboBox;
        projection->addItems({QStringLiteral("Perspective"), QStringLiteral("Orthographic")});
        projection->setCurrentIndex(static_cast<int>(camera->camera.getProjectionType()));
        connect(projection, &QComboBox::currentIndexChanged, this, [this, camera](int index) {
            if (index == static_cast<int>(ProjectionType::Perspective))
                camera->camera.setPerspective(camera->camera.getFovY(), camera->camera.getAspect(),
                                              camera->camera.getNearClip(), camera->camera.getFarClip());
            else
                camera->camera.setOrthographic(10.0f, camera->camera.getNearClip(), camera->camera.getFarClip());
            markChanged();
        });
        form->addRow(QStringLiteral("Projection"), projection);
        auto* fov = new QDoubleSpinBox;
        fov->setRange(1.0, 179.0);
        fov->setValue(camera->camera.getFovY());
        connect(fov, &QDoubleSpinBox::valueChanged, this, [this, camera](double value) {
            camera->camera.setPerspective(static_cast<float>(value), camera->camera.getAspect(),
                                          camera->camera.getNearClip(), camera->camera.getFarClip());
            markChanged();
        });
        form->addRow(QStringLiteral("Field of View"), fov);
        float nearClip = camera->camera.getNearClip();
        float farClip = camera->camera.getFarClip();
        auto* clipWidget = new QWidget;
        auto* clipLayout = new QHBoxLayout(clipWidget);
        clipLayout->setContentsMargins(0, 0, 0, 0);
        auto* nearField = new QDoubleSpinBox;
        auto* farField = new QDoubleSpinBox;
        nearField->setRange(0.001, 1000.0);
        farField->setRange(0.01, 1000000.0);
        nearField->setValue(nearClip);
        farField->setValue(farClip);
        connect(nearField, &QDoubleSpinBox::valueChanged, this, [this, camera, farField](double value) {
            camera->camera.setPerspective(camera->camera.getFovY(), camera->camera.getAspect(),
                                          static_cast<float>(value), static_cast<float>(farField->value()));
            markChanged();
        });
        connect(farField, &QDoubleSpinBox::valueChanged, this, [this, camera, nearField](double value) {
            camera->camera.setPerspective(camera->camera.getFovY(), camera->camera.getAspect(),
                                          static_cast<float>(nearField->value()), static_cast<float>(value));
            markChanged();
        });
        clipLayout->addWidget(nearField);
        clipLayout->addWidget(farField);
        form->addRow(QStringLiteral("Near / Far"), clipWidget);
        form->addRow(QStringLiteral("Primary"), boolField(this, camera->primary, changed));
        form->addRow(QStringLiteral("Fixed Aspect"), boolField(this, camera->fixedAspect, changed));
    }

    if (auto* light = m_scene->getComponent<LightComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Light"), light);
        form->addRow(QStringLiteral("Type"), enumField(this, light->type,
            {QStringLiteral("Directional"), QStringLiteral("Point"), QStringLiteral("Spot")}, changed));
        form->addRow(QStringLiteral("Color"), colorField(this, light->color, changed));
        form->addRow(QStringLiteral("Intensity"), floatField(this, light->intensity, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Range"), floatField(this, light->range, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Inner Angle"), floatField(this, light->innerAngle, changed, 0.0, 180.0, 1.0));
        form->addRow(QStringLiteral("Outer Angle"), floatField(this, light->outerAngle, changed, 0.0, 180.0, 1.0));
        form->addRow(QStringLiteral("Cast Shadows"), boolField(this, light->castShadows, changed));
        form->addRow(QStringLiteral("Cookie"), pathField(this, light->cookieTexture, changed));
        form->addRow(QStringLiteral("Cookie Strength"), floatField(this, light->cookieStrength, changed, 0.0, 10.0));
    }

    if (auto* sky = m_scene->getComponent<SkyboxComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Skybox"), sky);
        form->addRow(QStringLiteral("Enabled"), boolField(this, sky->enabled, changed));
        form->addRow(QStringLiteral("Texture"), pathField(this, sky->texturePath, changed,
            QStringLiteral("Environment Maps (*.hdr *.exr *.dds);;All Files (*)")));
        form->addRow(QStringLiteral("Intensity"), floatField(this, sky->intensity, changed, 0.0, 100.0));
    }

    if (auto* fog = m_scene->getComponent<FogComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Height Fog"), fog);
        form->addRow(QStringLiteral("Enabled"), boolField(this, fog->enabled, changed));
        form->addRow(QStringLiteral("Color"), colorField(this, fog->color, changed));
        form->addRow(QStringLiteral("Density"), floatField(this, fog->density, changed, 0.0, 1.0, 0.001, 4));
        form->addRow(QStringLiteral("Height"), floatField(this, fog->height, changed));
        form->addRow(QStringLiteral("Height Falloff"), floatField(this, fog->heightFalloff, changed, 0.0, 10.0, 0.01));
        form->addRow(QStringLiteral("Start Distance"), floatField(this, fog->start, changed, 0.0, 100000.0));
    }

    if (auto* fog = m_scene->getComponent<VolumetricFogComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Volumetric Fog"), fog);
        form->addRow(QStringLiteral("Enabled"), boolField(this, fog->enabled, changed));
        form->addRow(QStringLiteral("Color"), colorField(this, fog->color, changed));
        form->addRow(QStringLiteral("Density"), floatField(this, fog->density, changed, 0.0, 1.0, 0.001, 4));
        form->addRow(QStringLiteral("Intensity"), floatField(this, fog->intensity, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Anisotropy"), floatField(this, fog->anisotropy, changed, -1.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Height"), floatField(this, fog->height, changed));
        form->addRow(QStringLiteral("Height Falloff"), floatField(this, fog->heightFalloff, changed, 0.0, 10.0, 0.01));
        form->addRow(QStringLiteral("Start Distance"), floatField(this, fog->startDistance, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Max Opacity"), floatField(this, fog->maxOpacity, changed, 0.0, 1.0, 0.01));
    }

    if (auto* fog = m_scene->getComponent<LocalVolumetricFogComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Local Volumetric Fog"), fog);
        form->addRow(QStringLiteral("Enabled"), boolField(this, fog->enabled, changed));
        form->addRow(QStringLiteral("Color"), colorField(this, fog->color, changed));
        form->addRow(QStringLiteral("Density"), floatField(this, fog->density, changed, 0.0, 1.0, 0.001, 4));
        form->addRow(QStringLiteral("Intensity"), floatField(this, fog->intensity, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Extents"), vectorFields(this, &fog->extents.x, 3, changed, 0.01, 100000.0));
        form->addRow(QStringLiteral("Edge Softness"), floatField(this, fog->edgeSoftness, changed, 0.0, 10.0, 0.01));
    }

    if (auto* clouds = m_scene->getComponent<VolumetricCloudComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Volumetric Clouds"), clouds);
        form->addRow(QStringLiteral("Enabled"), boolField(this, clouds->enabled, changed));
        form->addRow(QStringLiteral("Preset"), enumField(this, clouds->preset,
            {QStringLiteral("Clear"), QStringLiteral("Few Clouds"), QStringLiteral("Cloudy"),
             QStringLiteral("Overcast"), QStringLiteral("Thunder"), QStringLiteral("Sunset"),
             QStringLiteral("Storm")}, changed));
        form->addRow(QStringLiteral("Coverage"), floatField(this, clouds->coverage, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Density"), floatField(this, clouds->density, changed, 0.0, 2.0, 0.01));
        form->addRow(QStringLiteral("Altitude"), floatField(this, clouds->altitude, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Thickness"), floatField(this, clouds->thickness, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Scale"), floatField(this, clouds->scale, changed, 0.01, 100.0, 0.01));
        form->addRow(QStringLiteral("Speed"), floatField(this, clouds->speed, changed, -100.0, 100.0, 0.01));
        form->addRow(QStringLiteral("Darkness"), floatField(this, clouds->darkness, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Tint"), colorField(this, clouds->tint, changed));
    }

    if (auto* flare = m_scene->getComponent<LensFlareComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Lens Flare"), flare);
        form->addRow(QStringLiteral("Enabled"), boolField(this, flare->enabled, changed));
        form->addRow(QStringLiteral("Intensity"), floatField(this, flare->intensity, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Threshold"), floatField(this, flare->threshold, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Halo Width"), floatField(this, flare->haloWidth, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Ghost Spacing"), floatField(this, flare->ghostSpacing, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Dirt Intensity"), floatField(this, flare->dirtIntensity, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Tint"), colorField(this, flare->tint, changed));
    }

    if (auto* probe = m_scene->getComponent<ReflectionProbeComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Reflection Probe"), probe);
        form->addRow(QStringLiteral("Enabled"), boolField(this, probe->enabled, changed));
        form->addRow(QStringLiteral("Probe Asset"), pathField(this, probe->assetPath, changed));
        form->addRow(QStringLiteral("Priority"), intField(this, probe->priority, changed));
    }

    if (auto* probe = m_scene->getComponent<IrradianceProbeVolumeComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Irradiance Probe Volume"), probe);
        form->addRow(QStringLiteral("Enabled"), boolField(this, probe->enabled, changed));
        form->addRow(QStringLiteral("Extents"), vectorFields(this, &probe->extents.x, 3, changed, 0.01, 100000.0));
        form->addRow(QStringLiteral("Probe Counts"), vectorFields(this, &probe->probeCounts.x, 3, changed, 1.0, 64.0, 1.0));
        form->addRow(QStringLiteral("Tint"), colorField(this, probe->tint, changed));
        form->addRow(QStringLiteral("Intensity"), floatField(this, probe->intensity, changed, 0.0, 10.0));
        form->addRow(QStringLiteral("Sky Weight"), floatField(this, probe->skyWeight, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Bounce Weight"), floatField(this, probe->bounceWeight, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Normal Bias"), floatField(this, probe->normalBias, changed, 0.0, 10.0, 0.01));
        form->addRow(QStringLiteral("Leak Reduction"), floatField(this, probe->leakReduction, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Dynamic Update"), boolField(this, probe->dynamicUpdate, changed));
    }

    if (auto* body = m_scene->getComponent<RigidBodyComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Rigid Body"), body);
        form->addRow(QStringLiteral("Body Type"), enumField(this, body->type,
            {QStringLiteral("Static"), QStringLiteral("Dynamic"), QStringLiteral("Kinematic")}, changed));
        form->addRow(QStringLiteral("Mass"), floatField(this, body->mass, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Linear Damping"), floatField(this, body->linearDamping, changed, 0.0, 100.0, 0.01));
        form->addRow(QStringLiteral("Angular Damping"), floatField(this, body->angularDamping, changed, 0.0, 100.0, 0.01));
        form->addRow(QStringLiteral("Use Gravity"), boolField(this, body->useGravity, changed));
        form->addRow(QStringLiteral("Gravity Scale"), floatField(this, body->gravityScale, changed, -100.0, 100.0));
        form->addRow(QStringLiteral("Kinematic"), boolField(this, body->isKinematic, changed));
        form->addRow(QStringLiteral("Simulate"), boolField(this, body->simulatePhysics, changed));
        form->addRow(QStringLiteral("Lock Rotation"), boolField(this, body->lockRotation, changed));
        form->addRow(QStringLiteral("Linear Velocity"), vectorFields(this, &body->linearVelocity.x, 3, changed));
        form->addRow(QStringLiteral("Angular Velocity"), vectorFields(this, &body->angularVelocity.x, 3, changed));
    }

    if (auto* collider = m_scene->getComponent<BoxColliderComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Box Collider"), collider);
        form->addRow(QStringLiteral("Half Extents"), vectorFields(this, &collider->halfExtents.x, 3, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Offset"), vectorFields(this, &collider->offset.x, 3, changed));
        form->addRow(QStringLiteral("Friction"), floatField(this, collider->friction, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Restitution"), floatField(this, collider->restitution, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Trigger"), boolField(this, collider->isTrigger, changed));
    }

    if (auto* terrain = m_scene->getComponent<TerrainComponent>(m_entity)) {
        const Changed terrainChanged = [this, terrain] { terrain->dirty = true; markChanged(); };
        auto* form = componentForm(QStringLiteral("Terrain"), terrain);
        form->addRow(QStringLiteral("Resolution"), uintField(this, terrain->resolution, terrainChanged, 2, 4097));
        form->addRow(QStringLiteral("Size X"), floatField(this, terrain->sizeX, terrainChanged, 0.1, 100000.0));
        form->addRow(QStringLiteral("Size Z"), floatField(this, terrain->sizeZ, terrainChanged, 0.1, 100000.0));
        form->addRow(QStringLiteral("Max Height"), floatField(this, terrain->maxHeight, terrainChanged, 0.0, 100000.0));
        form->addRow(QStringLiteral("UV Scale"), floatField(this, terrain->uvScale, terrainChanged, 0.01, 10000.0));
        form->addRow(QStringLiteral("Low Color"), colorField(this, terrain->lowColor, terrainChanged));
        form->addRow(QStringLiteral("Mid Color"), colorField(this, terrain->midColor, terrainChanged));
        form->addRow(QStringLiteral("High Color"), colorField(this, terrain->highColor, terrainChanged));
        form->addRow(QStringLiteral("Cast Shadows"), boolField(this, terrain->castShadows, terrainChanged));
        form->addRow(QStringLiteral("Receive Shadows"), boolField(this, terrain->receiveShadows, terrainChanged));
        form->addRow(QStringLiteral("Collision"), boolField(this, terrain->collisionEnabled, terrainChanged));
    }

    if (auto* sculpt = m_scene->getComponent<TerrainSculptComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Terrain Sculpt"), sculpt);
        form->addRow(QStringLiteral("Tool"), enumField(this, sculpt->tool,
            {QStringLiteral("Raise"), QStringLiteral("Lower"), QStringLiteral("Flatten"), QStringLiteral("Smooth"),
             QStringLiteral("Noise"), QStringLiteral("Terrace"), QStringLiteral("Erode"), QStringLiteral("Sharpen")}, changed));
        form->addRow(QStringLiteral("Brush Radius"), floatField(this, sculpt->brushRadius, changed, 0.01, 100000.0));
        form->addRow(QStringLiteral("Brush Strength"), floatField(this, sculpt->brushStrength, changed, 0.0, 1000.0));
        form->addRow(QStringLiteral("Brush Falloff"), floatField(this, sculpt->brushFalloff, changed, 0.0, 100.0));
        form->addRow(QStringLiteral("Flatten Target"), floatField(this, sculpt->flattenTarget, changed));
        form->addRow(QStringLiteral("Noise Scale"), floatField(this, sculpt->noiseScale, changed, 0.0, 100.0));
        form->addRow(QStringLiteral("Terrace Spacing"), floatField(this, sculpt->terraceSpacing, changed, 0.0, 10000.0));
        form->addRow(QStringLiteral("Erosion Amount"), floatField(this, sculpt->erosionAmount, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Sharpen Amount"), floatField(this, sculpt->sharpenAmount, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Brush Center"), vectorFields(this, &sculpt->brushCenter.x, 2, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Auto Rebuild"), boolField(this, sculpt->autoRebuild, changed));
    }

    if (auto* foliage = m_scene->getComponent<TerrainFoliageComponent>(m_entity)) {
        const Changed foliageChanged = [this, foliage] { foliage->dirty = true; markChanged(); };
        auto* form = componentForm(QStringLiteral("Terrain Foliage"), foliage);
        form->addRow(QStringLiteral("Trees"), boolField(this, foliage->treesEnabled, foliageChanged));
        form->addRow(QStringLiteral("Grass"), boolField(this, foliage->grassEnabled, foliageChanged));
        form->addRow(QStringLiteral("Tree Mesh"), pathField(this, foliage->treeMeshPath, foliageChanged));
        form->addRow(QStringLiteral("Grass Mesh"), pathField(this, foliage->grassMeshPath, foliageChanged));
        form->addRow(QStringLiteral("Tree Scale Min"), floatField(this, foliage->treeMinScale, foliageChanged, 0.0, 100.0));
        form->addRow(QStringLiteral("Tree Scale Max"), floatField(this, foliage->treeMaxScale, foliageChanged, 0.0, 100.0));
        form->addRow(QStringLiteral("Grass Scale Min"), floatField(this, foliage->grassMinScale, foliageChanged, 0.0, 100.0));
        form->addRow(QStringLiteral("Grass Scale Max"), floatField(this, foliage->grassMaxScale, foliageChanged, 0.0, 100.0));
        form->addRow(QStringLiteral("Placement Jitter"), floatField(this, foliage->placementJitter, foliageChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Brush Radius"), floatField(this, foliage->brushRadius, foliageChanged, 0.0, 100000.0));
        form->addRow(QStringLiteral("Brush Density"), floatField(this, foliage->brushDensity, foliageChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Brush Center"), vectorFields(this, &foliage->brushCenter.x, 2, foliageChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Height Min"), floatField(this, foliage->minHeight, foliageChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Height Max"), floatField(this, foliage->maxHeight, foliageChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Max Slope"), floatField(this, foliage->maxSlopeDegrees, foliageChanged, 0.0, 90.0));
        form->addRow(QStringLiteral("Trunk Color"), colorField(this, foliage->treeTrunkColor, foliageChanged));
        form->addRow(QStringLiteral("Leaf Color"), colorField(this, foliage->treeLeafColor, foliageChanged));
        form->addRow(QStringLiteral("Grass Color"), colorField(this, foliage->grassColor, foliageChanged));
    }

    if (auto* water = m_scene->getComponent<WaterBodyComponent>(m_entity)) {
        const Changed waterChanged = [this, water] { water->dirty = true; markChanged(); };
        auto* form = componentForm(QStringLiteral("Water Body"), water);
        form->addRow(QStringLiteral("Type"), enumField(this, water->type,
            {QStringLiteral("Lake"), QStringLiteral("River"), QStringLiteral("Ocean"),
             QStringLiteral("Pool"), QStringLiteral("Custom Area")}, waterChanged));
        form->addRow(QStringLiteral("Resolution"), uintField(this, water->resolution, waterChanged, 2, 4097));
        form->addRow(QStringLiteral("Size"), vectorFields(this, &water->size.x, 2, waterChanged, 0.01, 100000.0));
        form->addRow(QStringLiteral("Depth"), floatField(this, water->depth, waterChanged, 0.0, 100000.0));
        form->addRow(QStringLiteral("Surface Color"), colorField(this, water->surfaceColor, waterChanged));
        form->addRow(QStringLiteral("Bottom Color"), colorField(this, water->bottomColor, waterChanged));
        form->addRow(QStringLiteral("Transparency"), floatField(this, water->transparency, waterChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Wave Amplitude"), floatField(this, water->waveAmplitude, waterChanged, 0.0, 1000.0));
        form->addRow(QStringLiteral("Wave Length"), floatField(this, water->waveLength, waterChanged, 0.01, 100000.0));
        form->addRow(QStringLiteral("Wave Speed"), floatField(this, water->waveSpeed, waterChanged, -1000.0, 1000.0));
        form->addRow(QStringLiteral("Choppiness"), floatField(this, water->choppiness, waterChanged, 0.0, 10.0));
        form->addRow(QStringLiteral("Roughness"), floatField(this, water->roughness, waterChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Foam"), floatField(this, water->foamIntensity, waterChanged, 0.0, 10.0));
        form->addRow(QStringLiteral("Edge Fade"), floatField(this, water->edgeFade, waterChanged, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Flow Direction"), vectorFields(this, &water->flowDirection.x, 2, waterChanged, -1.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Flow Speed"), floatField(this, water->flowSpeed, waterChanged, -1000.0, 1000.0));
        form->addRow(QStringLiteral("Fluid Density"), floatField(this, water->fluidDensity, waterChanged, 0.0, 100000.0));
        form->addRow(QStringLiteral("Drag"), floatField(this, water->drag, waterChanged, 0.0, 1000.0));
        form->addRow(QStringLiteral("Buoyancy"), floatField(this, water->buoyancyMultiplier, waterChanged, 0.0, 1000.0));
        form->addRow(QStringLiteral("Affects Bodies"), boolField(this, water->affectsRigidBodies, waterChanged));
    }

    if (auto* script = m_scene->getComponent<ScriptComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Script"), script);
        form->addRow(QStringLiteral("Class"), textField(this, script->className, changed));
        for (auto& field : script->fieldValues) {
            if (field.hidden)
                continue;
            QWidget* editor = nullptr;
            switch (field.type) {
                case ScriptFieldType::Bool:
                    editor = boolField(this, field.boolValue, changed);
                    break;
                case ScriptFieldType::Int: {
                    auto* spin = new QDoubleSpinBox;
                    spin->setDecimals(0);
                    spin->setRange(-9.0e15, 9.0e15);
                    spin->setValue(static_cast<double>(field.intValue));
                    connect(spin, &QDoubleSpinBox::valueChanged, this, [&field, changed](double value) {
                        field.intValue = static_cast<int64_t>(value);
                        changed();
                    });
                    editor = spin;
                    break;
                }
                case ScriptFieldType::Float:
                    editor = floatField(this, field.floatValue, changed);
                    break;
                case ScriptFieldType::String:
                    editor = textField(this, field.stringValue, changed);
                    break;
                case ScriptFieldType::Vec3:
                    editor = vectorFields(this, &field.vec3Value.x, 3, changed);
                    break;
                default:
                    editor = new QLabel(QStringLiteral("Unsupported field type"));
                    break;
            }
            form->addRow(QString::fromStdString(field.name), editor);
        }
    }

    if (auto* audio = m_scene->getComponent<AudioSourceComponent>(m_entity)) {
        auto* form = componentForm(QStringLiteral("Audio Source"), audio);
        form->addRow(QStringLiteral("Clip"), pathField(this, audio->clipPath, changed,
            QStringLiteral("Audio (*.wav *.ogg *.mp3 *.flac);;All Files (*)")));
        form->addRow(QStringLiteral("Volume"), floatField(this, audio->volume, changed, 0.0, 1.0, 0.01));
        form->addRow(QStringLiteral("Pitch"), floatField(this, audio->pitch, changed, 0.01, 4.0, 0.01));
        form->addRow(QStringLiteral("Range"), floatField(this, audio->range, changed, 0.0, 100000.0));
        form->addRow(QStringLiteral("Loop"), boolField(this, audio->loop, changed));
        form->addRow(QStringLiteral("Play on Awake"), boolField(this, audio->playOnAwake, changed));
        form->addRow(QStringLiteral("Spatial"), boolField(this, audio->spatial, changed));
    }

    auto* addButton = new QPushButton(QStringLiteral("+ Add Component"));
    addButton->setObjectName(QStringLiteral("addComponentButton"));
    auto* addMenu = new QMenu(addButton);
    auto addAction = [this, addMenu]<typename T>(const QString& name, T component = {}) {
        if (m_scene->hasComponent<T>(m_entity))
            return;
        QAction* action = addMenu->addAction(name);
        connect(action, &QAction::triggered, this, [this, component = std::move(component)]() mutable {
            m_scene->addComponent<T>(m_entity, std::move(component));
            markChanged();
            refresh();
        });
    };
    addAction(QStringLiteral("Transform"), TransformComponent{});
    addAction(QStringLiteral("Mesh Renderer"), MeshRendererComponent{.meshPath = "builtin:cube"});
    addAction(QStringLiteral("Animator"), AnimatorComponent{});
    addAction(QStringLiteral("Material Override"), MaterialComponent{});
    addAction(QStringLiteral("Camera"), CameraComponent{});
    addAction(QStringLiteral("Light"), LightComponent{});
    addAction(QStringLiteral("Skybox"), SkyboxComponent{});
    addAction(QStringLiteral("Height Fog"), FogComponent{});
    addAction(QStringLiteral("Volumetric Fog"), VolumetricFogComponent{});
    addAction(QStringLiteral("Local Volumetric Fog"), LocalVolumetricFogComponent{});
    addAction(QStringLiteral("Volumetric Clouds"), VolumetricCloudComponent{});
    addAction(QStringLiteral("Lens Flare"), LensFlareComponent{});
    addAction(QStringLiteral("Reflection Probe"), ReflectionProbeComponent{});
    addAction(QStringLiteral("Irradiance Probe Volume"), IrradianceProbeVolumeComponent{});
    addAction(QStringLiteral("Rigid Body"), RigidBodyComponent{});
    addAction(QStringLiteral("Box Collider"), BoxColliderComponent{});
    addAction(QStringLiteral("Terrain"), TerrainComponent{});
    addAction(QStringLiteral("Terrain Sculpt"), TerrainSculptComponent{});
    addAction(QStringLiteral("Terrain Foliage"), TerrainFoliageComponent{});
    addAction(QStringLiteral("Water Body"), WaterBodyComponent{});
    addAction(QStringLiteral("Script"), ScriptComponent{});
    addAction(QStringLiteral("Audio Source"), AudioSourceComponent{});
    addButton->setMenu(addMenu);
    addButton->setEnabled(!addMenu->isEmpty());
    root->addWidget(addButton);
    root->addStretch();
}

} // namespace Demon
