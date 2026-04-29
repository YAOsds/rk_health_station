#include "widgets/config_editor_window.h"

#include "runtime_config/app_runtime_config_validator.h"
#include "widgets/config_section_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QVBoxLayout>

namespace {
QString readString(const QJsonObject &object, const QString &key, const QString &current) {
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : current;
}

bool readBool(const QJsonObject &object, const QString &key, bool current) {
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : current;
}

int readInt(const QJsonObject &object, const QString &key, int current) {
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt() : current;
}

double readDouble(const QJsonObject &object, const QString &key, double current) {
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : current;
}

void applyJsonToConfig(const QJsonObject &root, AppRuntimeConfig *config) {
    if (!config) {
        return;
    }

    const QJsonObject system = root.value(QStringLiteral("system")).toObject();
    config->system.runtimeMode = readString(system, QStringLiteral("runtime_mode"), config->system.runtimeMode);

    const QJsonObject paths = root.value(QStringLiteral("paths")).toObject();
    config->paths.storageDir = readString(paths, QStringLiteral("storage_dir"), config->paths.storageDir);
    config->paths.databasePath = readString(paths, QStringLiteral("database_path"), config->paths.databasePath);

    const QJsonObject ipc = root.value(QStringLiteral("ipc")).toObject();
    config->ipc.healthSocketPath = readString(ipc, QStringLiteral("health_socket"), config->ipc.healthSocketPath);
    config->ipc.videoSocketPath = readString(ipc, QStringLiteral("video_socket"), config->ipc.videoSocketPath);
    config->ipc.analysisSocketPath = readString(ipc, QStringLiteral("analysis_socket"), config->ipc.analysisSocketPath);
    config->ipc.fallSocketPath = readString(ipc, QStringLiteral("fall_socket"), config->ipc.fallSocketPath);
    config->ipc.analysisSharedMemoryName = readString(ipc, QStringLiteral("analysis_shared_memory_name"), config->ipc.analysisSharedMemoryName);

    const QJsonObject video = root.value(QStringLiteral("video")).toObject();
    config->video.cameraId = readString(video, QStringLiteral("camera_id"), config->video.cameraId);
    config->video.devicePath = readString(video, QStringLiteral("device_path"), config->video.devicePath);
    config->video.pipelineBackend = readString(video, QStringLiteral("pipeline_backend"), config->video.pipelineBackend);
    config->video.analysisEnabled = readBool(video, QStringLiteral("analysis_enabled"), config->video.analysisEnabled);
    config->video.analysisConvertBackend = readString(video, QStringLiteral("analysis_convert_backend"), config->video.analysisConvertBackend);
    config->video.gstLaunchBin = readString(video, QStringLiteral("gst_launch_bin"), config->video.gstLaunchBin);

    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    config->analysis.transport = readString(analysis, QStringLiteral("transport"), config->analysis.transport);
    config->analysis.dmaHeap = readString(analysis, QStringLiteral("dma_heap"), config->analysis.dmaHeap);
    config->analysis.rgaOutputDmabuf = readBool(analysis, QStringLiteral("rga_output_dmabuf"), config->analysis.rgaOutputDmabuf);
    config->analysis.gstDmabufInput = readBool(analysis, QStringLiteral("gst_dmabuf_input"), config->analysis.gstDmabufInput);
    config->analysis.gstForceDmabufIo = readBool(analysis, QStringLiteral("gst_force_dmabuf_io"), config->analysis.gstForceDmabufIo);

    const QJsonObject fall = root.value(QStringLiteral("fall_detection")).toObject();
    config->fallDetection.enabled = readBool(fall, QStringLiteral("enabled"), config->fallDetection.enabled);
    config->fallDetection.poseModelPath = readString(fall, QStringLiteral("pose_model_path"), config->fallDetection.poseModelPath);
    config->fallDetection.stgcnModelPath = readString(fall, QStringLiteral("stgcn_model_path"), config->fallDetection.stgcnModelPath);
    config->fallDetection.lstmModelPath = readString(fall, QStringLiteral("lstm_model_path"), config->fallDetection.lstmModelPath);
    config->fallDetection.lstmWeightsPath = readString(fall, QStringLiteral("lstm_weights_path"), config->fallDetection.lstmWeightsPath);
    config->fallDetection.actionModelPath = readString(fall, QStringLiteral("action_model_path"), config->fallDetection.actionModelPath);
    config->fallDetection.actionBackend = readString(fall, QStringLiteral("action_backend"), config->fallDetection.actionBackend);
    config->fallDetection.maxTracks = readInt(fall, QStringLiteral("max_tracks"), config->fallDetection.maxTracks);
    config->fallDetection.trackHighThresh = readDouble(fall, QStringLiteral("track_high_thresh"), config->fallDetection.trackHighThresh);
    config->fallDetection.trackLowThresh = readDouble(fall, QStringLiteral("track_low_thresh"), config->fallDetection.trackLowThresh);
    config->fallDetection.newTrackThresh = readDouble(fall, QStringLiteral("new_track_thresh"), config->fallDetection.newTrackThresh);
    config->fallDetection.matchThresh = readDouble(fall, QStringLiteral("match_thresh"), config->fallDetection.matchThresh);
    config->fallDetection.lostTimeoutMs = readInt(fall, QStringLiteral("lost_timeout_ms"), config->fallDetection.lostTimeoutMs);
    config->fallDetection.minValidKeypoints = readInt(fall, QStringLiteral("min_valid_keypoints"), config->fallDetection.minValidKeypoints);
    config->fallDetection.minBoxArea = readDouble(fall, QStringLiteral("min_box_area"), config->fallDetection.minBoxArea);
    config->fallDetection.sequenceLength = readInt(fall, QStringLiteral("sequence_length"), config->fallDetection.sequenceLength);
    config->fallDetection.rknnInputDmabuf = readBool(fall, QStringLiteral("rknn_input_dmabuf"), config->fallDetection.rknnInputDmabuf);
    config->fallDetection.rknnIoMemMode = readString(fall, QStringLiteral("rknn_io_mem_mode"), config->fallDetection.rknnIoMemMode);

    const QJsonObject debug = root.value(QStringLiteral("debug")).toObject();
    config->debug.healthdEventMarkerPath = readString(debug, QStringLiteral("healthd_event_marker_path"), config->debug.healthdEventMarkerPath);
    config->debug.videoLatencyMarkerPath = readString(debug, QStringLiteral("video_latency_marker_path"), config->debug.videoLatencyMarkerPath);
    config->debug.fallLatencyMarkerPath = readString(debug, QStringLiteral("fall_latency_marker_path"), config->debug.fallLatencyMarkerPath);
    config->debug.fallPoseTimingPath = readString(debug, QStringLiteral("fall_pose_timing_path"), config->debug.fallPoseTimingPath);
    config->debug.fallTrackTracePath = readString(debug, QStringLiteral("fall_track_trace_path"), config->debug.fallTrackTracePath);
    config->debug.fallActionDebug = readBool(debug, QStringLiteral("fall_action_debug"), config->debug.fallActionDebug);
    config->debug.fallLstmTracePath = readString(debug, QStringLiteral("fall_lstm_trace_path"), config->debug.fallLstmTracePath);
}

void insertString(QJsonObject *object, const QString &key, const QString &value) {
    if (object) {
        object->insert(key, value);
    }
}

void insertBool(QJsonObject *object, const QString &key, bool value) {
    if (object) {
        object->insert(key, value);
    }
}

void insertInt(QJsonObject *object, const QString &key, int value) {
    if (object) {
        object->insert(key, value);
    }
}

void insertDouble(QJsonObject *object, const QString &key, double value) {
    if (object) {
        object->insert(key, value);
    }
}

QJsonObject toJsonObject(const AppRuntimeConfig &config) {
    QJsonObject root;

    QJsonObject system;
    insertString(&system, QStringLiteral("runtime_mode"), config.system.runtimeMode);
    root.insert(QStringLiteral("system"), system);

    QJsonObject paths;
    insertString(&paths, QStringLiteral("storage_dir"), config.paths.storageDir);
    insertString(&paths, QStringLiteral("database_path"), config.paths.databasePath);
    root.insert(QStringLiteral("paths"), paths);

    QJsonObject ipc;
    insertString(&ipc, QStringLiteral("health_socket"), config.ipc.healthSocketPath);
    insertString(&ipc, QStringLiteral("video_socket"), config.ipc.videoSocketPath);
    insertString(&ipc, QStringLiteral("analysis_socket"), config.ipc.analysisSocketPath);
    insertString(&ipc, QStringLiteral("fall_socket"), config.ipc.fallSocketPath);
    insertString(&ipc, QStringLiteral("analysis_shared_memory_name"), config.ipc.analysisSharedMemoryName);
    root.insert(QStringLiteral("ipc"), ipc);

    QJsonObject video;
    insertString(&video, QStringLiteral("camera_id"), config.video.cameraId);
    insertString(&video, QStringLiteral("device_path"), config.video.devicePath);
    insertString(&video, QStringLiteral("pipeline_backend"), config.video.pipelineBackend);
    insertBool(&video, QStringLiteral("analysis_enabled"), config.video.analysisEnabled);
    insertString(&video, QStringLiteral("analysis_convert_backend"), config.video.analysisConvertBackend);
    insertString(&video, QStringLiteral("gst_launch_bin"), config.video.gstLaunchBin);
    root.insert(QStringLiteral("video"), video);

    QJsonObject analysis;
    insertString(&analysis, QStringLiteral("transport"), config.analysis.transport);
    insertString(&analysis, QStringLiteral("dma_heap"), config.analysis.dmaHeap);
    insertBool(&analysis, QStringLiteral("rga_output_dmabuf"), config.analysis.rgaOutputDmabuf);
    insertBool(&analysis, QStringLiteral("gst_dmabuf_input"), config.analysis.gstDmabufInput);
    insertBool(&analysis, QStringLiteral("gst_force_dmabuf_io"), config.analysis.gstForceDmabufIo);
    root.insert(QStringLiteral("analysis"), analysis);

    QJsonObject fall;
    insertBool(&fall, QStringLiteral("enabled"), config.fallDetection.enabled);
    insertString(&fall, QStringLiteral("pose_model_path"), config.fallDetection.poseModelPath);
    insertString(&fall, QStringLiteral("stgcn_model_path"), config.fallDetection.stgcnModelPath);
    insertString(&fall, QStringLiteral("lstm_model_path"), config.fallDetection.lstmModelPath);
    insertString(&fall, QStringLiteral("lstm_weights_path"), config.fallDetection.lstmWeightsPath);
    insertString(&fall, QStringLiteral("action_model_path"), config.fallDetection.actionModelPath);
    insertString(&fall, QStringLiteral("action_backend"), config.fallDetection.actionBackend);
    insertInt(&fall, QStringLiteral("max_tracks"), config.fallDetection.maxTracks);
    insertDouble(&fall, QStringLiteral("track_high_thresh"), config.fallDetection.trackHighThresh);
    insertDouble(&fall, QStringLiteral("track_low_thresh"), config.fallDetection.trackLowThresh);
    insertDouble(&fall, QStringLiteral("new_track_thresh"), config.fallDetection.newTrackThresh);
    insertDouble(&fall, QStringLiteral("match_thresh"), config.fallDetection.matchThresh);
    insertInt(&fall, QStringLiteral("lost_timeout_ms"), config.fallDetection.lostTimeoutMs);
    insertInt(&fall, QStringLiteral("min_valid_keypoints"), config.fallDetection.minValidKeypoints);
    insertDouble(&fall, QStringLiteral("min_box_area"), config.fallDetection.minBoxArea);
    insertInt(&fall, QStringLiteral("sequence_length"), config.fallDetection.sequenceLength);
    insertBool(&fall, QStringLiteral("rknn_input_dmabuf"), config.fallDetection.rknnInputDmabuf);
    insertString(&fall, QStringLiteral("rknn_io_mem_mode"), config.fallDetection.rknnIoMemMode);
    root.insert(QStringLiteral("fall_detection"), fall);

    QJsonObject debug;
    insertString(&debug, QStringLiteral("healthd_event_marker_path"), config.debug.healthdEventMarkerPath);
    insertString(&debug, QStringLiteral("video_latency_marker_path"), config.debug.videoLatencyMarkerPath);
    insertString(&debug, QStringLiteral("fall_latency_marker_path"), config.debug.fallLatencyMarkerPath);
    insertString(&debug, QStringLiteral("fall_pose_timing_path"), config.debug.fallPoseTimingPath);
    insertString(&debug, QStringLiteral("fall_track_trace_path"), config.debug.fallTrackTracePath);
    insertBool(&debug, QStringLiteral("fall_action_debug"), config.debug.fallActionDebug);
    insertString(&debug, QStringLiteral("fall_lstm_trace_path"), config.debug.fallLstmTracePath);
    root.insert(QStringLiteral("debug"), debug);

    return root;
}

QString comboValue(const QComboBox *comboBox) {
    if (!comboBox) {
        return QString();
    }
    const QString data = comboBox->currentData().toString();
    return data.isEmpty() ? comboBox->currentText() : data;
}
}

ConfigEditorWindow::ConfigEditorWindow(const QString &configPath, QWidget *parent)
    : QMainWindow(parent)
    , configPath_(configPath)
    , currentConfig_(buildDefaultAppRuntimeConfig()) {
    buildUi();
    refreshWindowTitle();
}

bool ConfigEditorWindow::load() {
    currentConfig_ = buildDefaultAppRuntimeConfig();

    if (QFileInfo::exists(configPath_)) {
        QFile file(configPath_);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            populateFromConfig(currentConfig_);
            setDirty(false);
            setStatus(QStringLiteral("Failed to open %1").arg(configPath_), true);
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            populateFromConfig(currentConfig_);
            setDirty(false);
            setStatus(QStringLiteral("Invalid JSON in %1: %2").arg(configPath_, parseError.errorString()), true);
            return false;
        }

        applyJsonToConfig(document.object(), &currentConfig_);
        setStatus(QStringLiteral("Loaded %1").arg(configPath_));
    } else {
        setStatus(QStringLiteral("Editing defaults; file will be created at %1").arg(configPath_));
    }

    populateFromConfig(currentConfig_);
    setDirty(false);
    return true;
}

QString ConfigEditorWindow::valueForField(const QString &fieldPath) const {
    QWidget *widget = fieldWidgets_.value(fieldPath, nullptr);
    if (auto *lineEdit = qobject_cast<QLineEdit *>(widget)) {
        return lineEdit->text();
    }
    if (auto *checkBox = qobject_cast<QCheckBox *>(widget)) {
        return checkBox->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (auto *comboBox = qobject_cast<QComboBox *>(widget)) {
        return comboValue(comboBox);
    }
    if (auto *spinBox = qobject_cast<QSpinBox *>(widget)) {
        return QString::number(spinBox->value());
    }
    if (auto *doubleSpinBox = qobject_cast<QDoubleSpinBox *>(widget)) {
        return QString::number(doubleSpinBox->value(), 'f', doubleSpinBox->decimals());
    }
    return QString();
}

QWidget *ConfigEditorWindow::fieldWidget(const QString &fieldPath) const {
    return fieldWidgets_.value(fieldPath, nullptr);
}

bool ConfigEditorWindow::save() {
    const AppRuntimeConfig nextConfig = collectConfigFromWidgets();
    QStringList errors;
    QStringList warnings;
    validateAppRuntimeConfig(nextConfig, &errors, &warnings);
    if (!errors.isEmpty()) {
        setStatus(QStringLiteral("Cannot save: %1").arg(errors.join(QStringLiteral("; "))), true);
        return false;
    }

    QFileInfo info(configPath_);
    QDir().mkpath(info.absolutePath());
    QFile file(configPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        setStatus(QStringLiteral("Failed to write %1").arg(configPath_), true);
        return false;
    }

    const QJsonDocument document(toJsonObject(nextConfig));
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();

    currentConfig_ = nextConfig;
    setDirty(false);
    setStatus(warnings.isEmpty()
            ? QStringLiteral("Saved %1 at %2").arg(configPath_, QDateTime::currentDateTime().toString(Qt::ISODate))
            : QStringLiteral("Saved with warnings: %1").arg(warnings.join(QStringLiteral("; "))));
    return true;
}

void ConfigEditorWindow::restoreDefaults() {
    currentConfig_ = buildDefaultAppRuntimeConfig();
    populateFromConfig(currentConfig_);
    setDirty(true);
    setStatus(QStringLiteral("Restored built-in defaults; save to persist changes."));
}

void ConfigEditorWindow::buildUi() {
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);

    auto *hero = new QLabel(
        QStringLiteral("Edit the shared runtime configuration in one place. Environment variables stay available for temporary advanced overrides."),
        central);
    hero->setWordWrap(true);
    rootLayout->addWidget(hero);

    auto *toolbarLayout = new QHBoxLayout();
    auto *pathLabel = new QLabel(QStringLiteral("Config file: %1").arg(configPath_), central);
    pathLabel->setWordWrap(true);
    auto *reloadButton = new QPushButton(QStringLiteral("Reload"), central);
    auto *defaultsButton = new QPushButton(QStringLiteral("Restore Defaults"), central);
    auto *saveButton = new QPushButton(QStringLiteral("Save"), central);
    toolbarLayout->addWidget(pathLabel, 1);
    toolbarLayout->addWidget(reloadButton);
    toolbarLayout->addWidget(defaultsButton);
    toolbarLayout->addWidget(saveButton);
    rootLayout->addLayout(toolbarLayout);

    auto *scrollArea = new QScrollArea(central);
    scrollArea->setWidgetResizable(true);
    auto *scrollWidget = new QWidget(scrollArea);
    auto *sectionsLayout = new QVBoxLayout(scrollWidget);

    auto *systemSection = new ConfigSectionWidget(
        QStringLiteral("System"),
        QStringLiteral("How the bundle chooses host/system services and storage conventions."),
        scrollWidget);
    systemSection->formLayout()->addRow(QStringLiteral("Runtime mode"),
        registerComboBox(QStringLiteral("system.runtime_mode"), {
            {QStringLiteral("auto"), QStringLiteral("Auto")},
            {QStringLiteral("system"), QStringLiteral("System")},
            {QStringLiteral("bundle"), QStringLiteral("Bundle")},
        }));
    sectionsLayout->addWidget(systemSection);

    auto *pathsSection = new ConfigSectionWidget(
        QStringLiteral("Paths"),
        QStringLiteral("Persistent storage and database locations."),
        scrollWidget);
    pathsSection->formLayout()->addRow(QStringLiteral("Storage directory"), registerLineEdit(QStringLiteral("paths.storage_dir")));
    pathsSection->formLayout()->addRow(QStringLiteral("Database path"), registerLineEdit(QStringLiteral("paths.database_path")));
    sectionsLayout->addWidget(pathsSection);

    auto *ipcSection = new ConfigSectionWidget(
        QStringLiteral("IPC"),
        QStringLiteral("Unix socket names and the shared-memory channel used between services."),
        scrollWidget);
    ipcSection->formLayout()->addRow(QStringLiteral("Health socket"), registerLineEdit(QStringLiteral("ipc.health_socket")));
    ipcSection->formLayout()->addRow(QStringLiteral("Video socket"), registerLineEdit(QStringLiteral("ipc.video_socket")));
    ipcSection->formLayout()->addRow(QStringLiteral("Analysis socket"), registerLineEdit(QStringLiteral("ipc.analysis_socket")));
    ipcSection->formLayout()->addRow(QStringLiteral("Fall socket"), registerLineEdit(QStringLiteral("ipc.fall_socket")));
    ipcSection->formLayout()->addRow(QStringLiteral("Analysis SHM name"), registerLineEdit(QStringLiteral("ipc.analysis_shared_memory_name")));
    sectionsLayout->addWidget(ipcSection);

    auto *videoSection = new ConfigSectionWidget(
        QStringLiteral("Video"),
        QStringLiteral("Camera source selection and the preview/analysis pipeline mode."),
        scrollWidget);
    videoSection->formLayout()->addRow(QStringLiteral("Camera ID"), registerLineEdit(QStringLiteral("video.camera_id")));
    videoSection->formLayout()->addRow(QStringLiteral("Device path"), registerLineEdit(QStringLiteral("video.device_path")));
    videoSection->formLayout()->addRow(QStringLiteral("Pipeline backend"),
        registerComboBox(QStringLiteral("video.pipeline_backend"), {
            {QStringLiteral("process"), QStringLiteral("Process")},
            {QStringLiteral("inproc_gst"), QStringLiteral("In-process GStreamer")},
        }));
    videoSection->formLayout()->addRow(QStringLiteral("Analysis enabled"), registerCheckBox(QStringLiteral("video.analysis_enabled")));
    videoSection->formLayout()->addRow(QStringLiteral("Convert backend"),
        registerComboBox(QStringLiteral("video.analysis_convert_backend"), {
            {QStringLiteral("gstreamer_cpu"), QStringLiteral("GStreamer CPU")},
            {QStringLiteral("rga"), QStringLiteral("RGA")},
        }));
    videoSection->formLayout()->addRow(QStringLiteral("gst-launch binary"), registerLineEdit(QStringLiteral("video.gst_launch_bin")));
    sectionsLayout->addWidget(videoSection);

    auto *analysisSection = new ConfigSectionWidget(
        QStringLiteral("Analysis Transport"),
        QStringLiteral("Choose shared memory or DMA-BUF zero-copy transport, plus related toggles."),
        scrollWidget);
    analysisSection->formLayout()->addRow(QStringLiteral("Transport"),
        registerComboBox(QStringLiteral("analysis.transport"), {
            {QStringLiteral("shared_memory"), QStringLiteral("Shared memory")},
            {QStringLiteral("dmabuf"), QStringLiteral("DMA-BUF")},
            {QStringLiteral("dma"), QStringLiteral("DMA alias")},
        }));
    analysisSection->formLayout()->addRow(QStringLiteral("DMA heap"), registerLineEdit(QStringLiteral("analysis.dma_heap")));
    analysisSection->formLayout()->addRow(QStringLiteral("RGA output DMA-BUF"), registerCheckBox(QStringLiteral("analysis.rga_output_dmabuf")));
    analysisSection->formLayout()->addRow(QStringLiteral("GStreamer DMA-BUF input"), registerCheckBox(QStringLiteral("analysis.gst_dmabuf_input")));
    analysisSection->formLayout()->addRow(QStringLiteral("Force DMA-BUF I/O"), registerCheckBox(QStringLiteral("analysis.gst_force_dmabuf_io")));
    sectionsLayout->addWidget(analysisSection);

    auto *fallSection = new ConfigSectionWidget(
        QStringLiteral("Fall Detection"),
        QStringLiteral("Model paths, tracker thresholds, and RKNN dataflow options."),
        scrollWidget);
    fallSection->formLayout()->addRow(QStringLiteral("Enabled"), registerCheckBox(QStringLiteral("fall_detection.enabled")));
    fallSection->formLayout()->addRow(QStringLiteral("Pose model"), registerLineEdit(QStringLiteral("fall_detection.pose_model_path")));
    fallSection->formLayout()->addRow(QStringLiteral("STGCN model"), registerLineEdit(QStringLiteral("fall_detection.stgcn_model_path")));
    fallSection->formLayout()->addRow(QStringLiteral("LSTM model"), registerLineEdit(QStringLiteral("fall_detection.lstm_model_path")));
    fallSection->formLayout()->addRow(QStringLiteral("LSTM weights"), registerLineEdit(QStringLiteral("fall_detection.lstm_weights_path")));
    fallSection->formLayout()->addRow(QStringLiteral("Action model"), registerLineEdit(QStringLiteral("fall_detection.action_model_path")));
    fallSection->formLayout()->addRow(QStringLiteral("Action backend"),
        registerComboBox(QStringLiteral("fall_detection.action_backend"), {
            {QStringLiteral("lstm_rknn"), QStringLiteral("LSTM RKNN")},
            {QStringLiteral("rule_based"), QStringLiteral("Rule based")},
            {QStringLiteral("stgcn_rknn"), QStringLiteral("STGCN RKNN")},
        }));
    fallSection->formLayout()->addRow(QStringLiteral("Max tracks"), registerSpinBox(QStringLiteral("fall_detection.max_tracks"), 1, 32));
    fallSection->formLayout()->addRow(QStringLiteral("Track high thresh"), registerDoubleSpinBox(QStringLiteral("fall_detection.track_high_thresh"), 0.0, 1.0, 3));
    fallSection->formLayout()->addRow(QStringLiteral("Track low thresh"), registerDoubleSpinBox(QStringLiteral("fall_detection.track_low_thresh"), 0.0, 1.0, 3));
    fallSection->formLayout()->addRow(QStringLiteral("New track thresh"), registerDoubleSpinBox(QStringLiteral("fall_detection.new_track_thresh"), 0.0, 1.0, 3));
    fallSection->formLayout()->addRow(QStringLiteral("Match thresh"), registerDoubleSpinBox(QStringLiteral("fall_detection.match_thresh"), 0.0, 1.0, 3));
    fallSection->formLayout()->addRow(QStringLiteral("Lost timeout (ms)"), registerSpinBox(QStringLiteral("fall_detection.lost_timeout_ms"), 1, 60000));
    fallSection->formLayout()->addRow(QStringLiteral("Min valid keypoints"), registerSpinBox(QStringLiteral("fall_detection.min_valid_keypoints"), 1, 64));
    fallSection->formLayout()->addRow(QStringLiteral("Min box area"), registerDoubleSpinBox(QStringLiteral("fall_detection.min_box_area"), 1.0, 1000000.0, 1));
    fallSection->formLayout()->addRow(QStringLiteral("Sequence length"), registerSpinBox(QStringLiteral("fall_detection.sequence_length"), 1, 512));
    fallSection->formLayout()->addRow(QStringLiteral("RKNN input DMA-BUF"), registerCheckBox(QStringLiteral("fall_detection.rknn_input_dmabuf")));
    fallSection->formLayout()->addRow(QStringLiteral("RKNN I/O memory"),
        registerComboBox(QStringLiteral("fall_detection.rknn_io_mem_mode"), {
            {QStringLiteral("default"), QStringLiteral("Default")},
            {QStringLiteral("zero_copy"), QStringLiteral("Zero copy")},
            {QStringLiteral("full"), QStringLiteral("Full")},
        }));
    sectionsLayout->addWidget(fallSection);

    auto *debugSection = new ConfigSectionWidget(
        QStringLiteral("Debug & Markers"),
        QStringLiteral("Optional marker files and traces for debugging and profiling."),
        scrollWidget);
    debugSection->formLayout()->addRow(QStringLiteral("healthd event marker"), registerLineEdit(QStringLiteral("debug.healthd_event_marker_path")));
    debugSection->formLayout()->addRow(QStringLiteral("Video latency marker"), registerLineEdit(QStringLiteral("debug.video_latency_marker_path")));
    debugSection->formLayout()->addRow(QStringLiteral("Fall latency marker"), registerLineEdit(QStringLiteral("debug.fall_latency_marker_path")));
    debugSection->formLayout()->addRow(QStringLiteral("Pose timing path"), registerLineEdit(QStringLiteral("debug.fall_pose_timing_path")));
    debugSection->formLayout()->addRow(QStringLiteral("Track trace path"), registerLineEdit(QStringLiteral("debug.fall_track_trace_path")));
    debugSection->formLayout()->addRow(QStringLiteral("Action debug"), registerCheckBox(QStringLiteral("debug.fall_action_debug")));
    debugSection->formLayout()->addRow(QStringLiteral("LSTM trace path"), registerLineEdit(QStringLiteral("debug.fall_lstm_trace_path")));
    sectionsLayout->addWidget(debugSection);

    sectionsLayout->addStretch(1);
    scrollArea->setWidget(scrollWidget);
    rootLayout->addWidget(scrollArea, 1);

    setCentralWidget(central);
    statusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(statusLabel_, 1);

    connect(reloadButton, &QPushButton::clicked, this, [this]() { load(); });
    connect(defaultsButton, &QPushButton::clicked, this, &ConfigEditorWindow::restoreDefaults);
    connect(saveButton, &QPushButton::clicked, this, [this]() { save(); });

    resize(920, 760);
}

void ConfigEditorWindow::populateFromConfig(const AppRuntimeConfig &config) {
    qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("system.runtime_mode")))->setCurrentIndex(
        qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("system.runtime_mode")))->findData(config.system.runtimeMode));

    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("paths.storage_dir")))->setText(config.paths.storageDir);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("paths.database_path")))->setText(config.paths.databasePath);

    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.health_socket")))->setText(config.ipc.healthSocketPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.video_socket")))->setText(config.ipc.videoSocketPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.analysis_socket")))->setText(config.ipc.analysisSocketPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.fall_socket")))->setText(config.ipc.fallSocketPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.analysis_shared_memory_name")))->setText(config.ipc.analysisSharedMemoryName);

    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("video.camera_id")))->setText(config.video.cameraId);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("video.device_path")))->setText(config.video.devicePath);
    qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("video.pipeline_backend")))->setCurrentIndex(
        qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("video.pipeline_backend")))->findData(config.video.pipelineBackend));
    qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("video.analysis_enabled")))->setChecked(config.video.analysisEnabled);
    qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("video.analysis_convert_backend")))->setCurrentIndex(
        qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("video.analysis_convert_backend")))->findData(config.video.analysisConvertBackend));
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("video.gst_launch_bin")))->setText(config.video.gstLaunchBin);

    qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("analysis.transport")))->setCurrentIndex(
        qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("analysis.transport")))->findData(config.analysis.transport));
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("analysis.dma_heap")))->setText(config.analysis.dmaHeap);
    qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("analysis.rga_output_dmabuf")))->setChecked(config.analysis.rgaOutputDmabuf);
    qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("analysis.gst_dmabuf_input")))->setChecked(config.analysis.gstDmabufInput);
    qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("analysis.gst_force_dmabuf_io")))->setChecked(config.analysis.gstForceDmabufIo);

    qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.enabled")))->setChecked(config.fallDetection.enabled);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.pose_model_path")))->setText(config.fallDetection.poseModelPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.stgcn_model_path")))->setText(config.fallDetection.stgcnModelPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.lstm_model_path")))->setText(config.fallDetection.lstmModelPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.lstm_weights_path")))->setText(config.fallDetection.lstmWeightsPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.action_model_path")))->setText(config.fallDetection.actionModelPath);
    qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.action_backend")))->setCurrentIndex(
        qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.action_backend")))->findData(config.fallDetection.actionBackend));
    qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.max_tracks")))->setValue(config.fallDetection.maxTracks);
    qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.track_high_thresh")))->setValue(config.fallDetection.trackHighThresh);
    qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.track_low_thresh")))->setValue(config.fallDetection.trackLowThresh);
    qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.new_track_thresh")))->setValue(config.fallDetection.newTrackThresh);
    qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.match_thresh")))->setValue(config.fallDetection.matchThresh);
    qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.lost_timeout_ms")))->setValue(config.fallDetection.lostTimeoutMs);
    qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.min_valid_keypoints")))->setValue(config.fallDetection.minValidKeypoints);
    qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.min_box_area")))->setValue(config.fallDetection.minBoxArea);
    qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.sequence_length")))->setValue(config.fallDetection.sequenceLength);
    qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.rknn_input_dmabuf")))->setChecked(config.fallDetection.rknnInputDmabuf);
    qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.rknn_io_mem_mode")))->setCurrentIndex(
        qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.rknn_io_mem_mode")))->findData(config.fallDetection.rknnIoMemMode));

    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.healthd_event_marker_path")))->setText(config.debug.healthdEventMarkerPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.video_latency_marker_path")))->setText(config.debug.videoLatencyMarkerPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_latency_marker_path")))->setText(config.debug.fallLatencyMarkerPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_pose_timing_path")))->setText(config.debug.fallPoseTimingPath);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_track_trace_path")))->setText(config.debug.fallTrackTracePath);
    qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("debug.fall_action_debug")))->setChecked(config.debug.fallActionDebug);
    qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_lstm_trace_path")))->setText(config.debug.fallLstmTracePath);
}

AppRuntimeConfig ConfigEditorWindow::collectConfigFromWidgets() const {
    AppRuntimeConfig config = buildDefaultAppRuntimeConfig();

    config.system.runtimeMode = comboValue(qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("system.runtime_mode"))));

    config.paths.storageDir = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("paths.storage_dir")))->text();
    config.paths.databasePath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("paths.database_path")))->text();

    config.ipc.healthSocketPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.health_socket")))->text();
    config.ipc.videoSocketPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.video_socket")))->text();
    config.ipc.analysisSocketPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.analysis_socket")))->text();
    config.ipc.fallSocketPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.fall_socket")))->text();
    config.ipc.analysisSharedMemoryName = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("ipc.analysis_shared_memory_name")))->text();

    config.video.cameraId = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("video.camera_id")))->text();
    config.video.devicePath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("video.device_path")))->text();
    config.video.pipelineBackend = comboValue(qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("video.pipeline_backend"))));
    config.video.analysisEnabled = qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("video.analysis_enabled")))->isChecked();
    config.video.analysisConvertBackend = comboValue(qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("video.analysis_convert_backend"))));
    config.video.gstLaunchBin = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("video.gst_launch_bin")))->text();

    config.analysis.transport = comboValue(qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("analysis.transport"))));
    config.analysis.dmaHeap = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("analysis.dma_heap")))->text();
    config.analysis.rgaOutputDmabuf = qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("analysis.rga_output_dmabuf")))->isChecked();
    config.analysis.gstDmabufInput = qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("analysis.gst_dmabuf_input")))->isChecked();
    config.analysis.gstForceDmabufIo = qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("analysis.gst_force_dmabuf_io")))->isChecked();

    config.fallDetection.enabled = qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.enabled")))->isChecked();
    config.fallDetection.poseModelPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.pose_model_path")))->text();
    config.fallDetection.stgcnModelPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.stgcn_model_path")))->text();
    config.fallDetection.lstmModelPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.lstm_model_path")))->text();
    config.fallDetection.lstmWeightsPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.lstm_weights_path")))->text();
    config.fallDetection.actionModelPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("fall_detection.action_model_path")))->text();
    config.fallDetection.actionBackend = comboValue(qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.action_backend"))));
    config.fallDetection.maxTracks = qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.max_tracks")))->value();
    config.fallDetection.trackHighThresh = qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.track_high_thresh")))->value();
    config.fallDetection.trackLowThresh = qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.track_low_thresh")))->value();
    config.fallDetection.newTrackThresh = qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.new_track_thresh")))->value();
    config.fallDetection.matchThresh = qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.match_thresh")))->value();
    config.fallDetection.lostTimeoutMs = qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.lost_timeout_ms")))->value();
    config.fallDetection.minValidKeypoints = qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.min_valid_keypoints")))->value();
    config.fallDetection.minBoxArea = qobject_cast<QDoubleSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.min_box_area")))->value();
    config.fallDetection.sequenceLength = qobject_cast<QSpinBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.sequence_length")))->value();
    config.fallDetection.rknnInputDmabuf = qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.rknn_input_dmabuf")))->isChecked();
    config.fallDetection.rknnIoMemMode = comboValue(qobject_cast<QComboBox *>(fieldWidgets_.value(QStringLiteral("fall_detection.rknn_io_mem_mode"))));

    config.debug.healthdEventMarkerPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.healthd_event_marker_path")))->text();
    config.debug.videoLatencyMarkerPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.video_latency_marker_path")))->text();
    config.debug.fallLatencyMarkerPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_latency_marker_path")))->text();
    config.debug.fallPoseTimingPath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_pose_timing_path")))->text();
    config.debug.fallTrackTracePath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_track_trace_path")))->text();
    config.debug.fallActionDebug = qobject_cast<QCheckBox *>(fieldWidgets_.value(QStringLiteral("debug.fall_action_debug")))->isChecked();
    config.debug.fallLstmTracePath = qobject_cast<QLineEdit *>(fieldWidgets_.value(QStringLiteral("debug.fall_lstm_trace_path")))->text();

    return config;
}

void ConfigEditorWindow::setDirty(bool dirty) {
    dirty_ = dirty;
    refreshWindowTitle();
}

void ConfigEditorWindow::setStatus(const QString &text, bool error) {
    if (statusLabel_) {
        statusLabel_->setText(text);
        statusLabel_->setStyleSheet(error ? QStringLiteral("color:#b42318;") : QString());
    }
}

void ConfigEditorWindow::refreshWindowTitle() {
    const QString suffix = dirty_ ? QStringLiteral(" *") : QString();
    setWindowTitle(QStringLiteral("Health Runtime Config%1").arg(suffix));
}

QWidget *ConfigEditorWindow::registerLineEdit(const QString &fieldPath) {
    auto *lineEdit = new QLineEdit(this);
    connect(lineEdit, &QLineEdit::textEdited, this, [this]() { setDirty(true); });
    fieldWidgets_.insert(fieldPath, lineEdit);
    return lineEdit;
}

QWidget *ConfigEditorWindow::registerCheckBox(const QString &fieldPath) {
    auto *checkBox = new QCheckBox(this);
    connect(checkBox, &QCheckBox::toggled, this, [this]() { setDirty(true); });
    fieldWidgets_.insert(fieldPath, checkBox);
    return checkBox;
}

QWidget *ConfigEditorWindow::registerComboBox(
    const QString &fieldPath, const QList<QPair<QString, QString>> &options) {
    auto *comboBox = new QComboBox(this);
    for (const auto &option : options) {
        comboBox->addItem(option.second, option.first);
    }
    connect(comboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int) { setDirty(true); });
    fieldWidgets_.insert(fieldPath, comboBox);
    return comboBox;
}

QWidget *ConfigEditorWindow::registerSpinBox(const QString &fieldPath, int minimum, int maximum) {
    auto *spinBox = new QSpinBox(this);
    spinBox->setRange(minimum, maximum);
    connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), this,
        [this](int) { setDirty(true); });
    fieldWidgets_.insert(fieldPath, spinBox);
    return spinBox;
}

QWidget *ConfigEditorWindow::registerDoubleSpinBox(
    const QString &fieldPath, double minimum, double maximum, int decimals) {
    auto *spinBox = new QDoubleSpinBox(this);
    spinBox->setRange(minimum, maximum);
    spinBox->setDecimals(decimals);
    spinBox->setSingleStep(0.01);
    connect(spinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
        [this](double) { setDirty(true); });
    fieldWidgets_.insert(fieldPath, spinBox);
    return spinBox;
}
