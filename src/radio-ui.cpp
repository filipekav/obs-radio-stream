#include "radio-ui.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QFileDialog>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <iomanip>
#include <sstream>
#include <obs-module.h>
#include <obs-frontend-api.h>

RadioDock::RadioDock(QWidget* parent) : QWidget(parent) {
    
    output = obs_output_create("radio_output", "Radio Stream Output", nullptr, nullptr);

    initUI();
    loadSettings();

    statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, this, &RadioDock::updateStatus);
    statusTimer->start(500);

    updateStatus();
}

RadioDock::~RadioDock() {
    saveSettings();
    if (output) {
        obs_output_release(output);
    }
}

void RadioDock::initUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);

    formLayout = new QFormLayout();
    
    protocolInput = new QComboBox();
    protocolInput->addItems({"Icecast / AzuraCast", "SHOUTcast (v1)"});
    formLayout->addRow("Protocol:", protocolInput);
    
    connect(protocolInput, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RadioDock::onProtocolChanged);
    currentProtocol = 0;
    
    urlInput = new QLineEdit();
    formLayout->addRow(QString::fromUtf8(obs_module_text("ServerUrl")), urlInput);
    
    portInput = new QSpinBox();
    portInput->setRange(1, 65535);
    portInput->setValue(8000);
    formLayout->addRow(QString::fromUtf8(obs_module_text("ServerPort")), portInput);
    
    mountInput = new QLineEdit();
    mountInput->setText("/stream");
    formLayout->addRow(QString::fromUtf8(obs_module_text("Mountpoint")), mountInput);
    
    userInput = new QLineEdit();
    userInput->setText("source");
    formLayout->addRow(QString::fromUtf8(obs_module_text("Username")), userInput);
    
    passInput = new QLineEdit();
    passInput->setEchoMode(QLineEdit::Password);
    formLayout->addRow(QString::fromUtf8(obs_module_text("Password")), passInput);
    
    bitrateInput = new QComboBox();
    bitrateInput->addItems({"64", "96", "128", "192", "320"});
    bitrateInput->setCurrentText("128");
    formLayout->addRow(QString::fromUtf8(obs_module_text("Bitrate")), bitrateInput);

    QHBoxLayout* pathLayout = new QHBoxLayout();
    pathDisplay = new QLineEdit();
    pathDisplay->setReadOnly(true);
    browseBtn = new QPushButton(QString::fromUtf8(obs_module_text("Browse")));
    pathLayout->addWidget(pathDisplay);
    pathLayout->addWidget(browseBtn);
    formLayout->addRow(QString::fromUtf8(obs_module_text("RecordPath")), pathLayout);

    layout->addLayout(formLayout);

    // ── Stream Controls ──
    QHBoxLayout* streamBtnLayout = new QHBoxLayout();
    toggleStreamBtn = new QPushButton(QString::fromUtf8(obs_module_text("StartStream")));
    toggleStreamBtn->setStyleSheet("background-color: #28a745; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    statusLabel = new QLabel(QString::fromUtf8(obs_module_text("StatusOffline")));
    statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont f = statusLabel->font();
    f.setBold(true);
    statusLabel->setFont(f);
    
    streamBtnLayout->addWidget(toggleStreamBtn, 2);
    streamBtnLayout->addWidget(statusLabel, 3);
    layout->addLayout(streamBtnLayout);

    // ── Record Controls ──
    QHBoxLayout* recordBtnLayout = new QHBoxLayout();
    toggleRecordBtn = new QPushButton(QString::fromUtf8(obs_module_text("StartRecord")));
    toggleRecordBtn->setStyleSheet("background-color: #28a745; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    recordStatusLabel = new QLabel(QString::fromUtf8(obs_module_text("StatusOffline")));
    recordStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    recordStatusLabel->setFont(f);
    
    recordBtnLayout->addWidget(toggleRecordBtn, 2);
    recordBtnLayout->addWidget(recordStatusLabel, 3);
    layout->addLayout(recordBtnLayout);

    layout->addStretch();

    connect(toggleStreamBtn, &QPushButton::clicked, this, &RadioDock::onToggleStreamClicked);
    connect(toggleRecordBtn, &QPushButton::clicked, this, &RadioDock::onToggleRecordClicked);
    connect(browseBtn, &QPushButton::clicked, this, &RadioDock::onBrowseClicked);
}

void RadioDock::loadSettings() {
    QSettings settings("OBSPlugins", "RadioStreamer");
    
    currentProtocol = settings.value("protocol_type", 0).toInt();
    protocolInput->blockSignals(true);
    protocolInput->setCurrentIndex(currentProtocol);
    protocolInput->blockSignals(false);
    
    QString prefix = (currentProtocol == 0) ? "ice_" : "sc_";
    
    urlInput->setText(settings.value(prefix + "server_url").toString());
    
    int port = settings.value(prefix + "server_port", 8000).toInt();
    portInput->setValue(port);
    
    QString mount = settings.value(prefix + "mountpoint", "/stream").toString();
    mountInput->setText(mount);
    
    userInput->setText(settings.value(prefix + "username", "source").toString());
    
    passInput->setText(settings.value(prefix + "password").toString());
    
    int br = settings.value(prefix + "bitrate", 128).toInt();
    bitrateInput->setCurrentText(QString::number(br));

    QString recordPath = settings.value("record_path", "").toString();
    if (recordPath.isEmpty()) {
        QString musicDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        recordPath = QDir::cleanPath(musicDir);
    } else {
        if (recordPath.endsWith(".mp3", Qt::CaseInsensitive)) {
            recordPath = QFileInfo(recordPath).absolutePath();
        }
    }
    pathDisplay->setText(recordPath);

    bool isIce = (currentProtocol == 0);
    mountInput->setVisible(isIce);
    if (formLayout->labelForField(mountInput)) formLayout->labelForField(mountInput)->setVisible(isIce);
    userInput->setVisible(isIce);
    if (formLayout->labelForField(userInput)) formLayout->labelForField(userInput)->setVisible(isIce);
}

void RadioDock::saveSettings() {
    QSettings settings("OBSPlugins", "RadioStreamer");

    settings.setValue("protocol_type", protocolInput->currentIndex());
    
    QString prefix = (currentProtocol == 0) ? "ice_" : "sc_";
    
    settings.setValue(prefix + "server_url", urlInput->text());
    settings.setValue(prefix + "server_port", portInput->value());
    settings.setValue(prefix + "mountpoint", mountInput->text());
    settings.setValue(prefix + "username", userInput->text());
    settings.setValue(prefix + "password", passInput->text());
    settings.setValue(prefix + "bitrate", bitrateInput->currentText().toInt());
    
    settings.setValue("record_path", pathDisplay->text());
}

void RadioDock::onToggleStreamClicked() {
    if (streamingActive) {
        streamingActive = false;
        if (!recordingActive) {
            obs_output_stop(output);
        } else {
            obs_data_t* out_settings = obs_output_get_settings(output);
            obs_data_set_bool(out_settings, "stream_active", false);
            obs_output_update(output, out_settings);
            obs_data_release(out_settings);
        }
    } else {
        saveSettings();
        streamingActive = true;
        streamStartTime = std::chrono::steady_clock::now();

        obs_data_t* out_settings = obs_output_get_settings(output);
        if (!out_settings) {
            out_settings = obs_data_create();
        }
        obs_data_set_int(out_settings, "protocol_type", protocolInput->currentIndex());
        obs_data_set_string(out_settings, "server_url", urlInput->text().toUtf8().constData());
        obs_data_set_int(out_settings, "server_port", portInput->value());
        obs_data_set_string(out_settings, "mountpoint", mountInput->text().toUtf8().constData());
        obs_data_set_string(out_settings, "username", userInput->text().toUtf8().constData());
        obs_data_set_string(out_settings, "password", passInput->text().toUtf8().constData());
        obs_data_set_int(out_settings, "bitrate", bitrateInput->currentText().toInt());
        obs_data_set_bool(out_settings, "stream_active", true);
        obs_data_set_bool(out_settings, "record_active", recordingActive);

        QString baseDir = pathDisplay->text();
        if (recordingActive) {
            const char* existing_path = obs_data_get_string(out_settings, "record_path");
            if (!existing_path || strlen(existing_path) == 0) {
                QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss");
                QString finalPath = QDir(baseDir).filePath(timestamp + ".mp3");
                obs_data_set_string(out_settings, "record_path", finalPath.toUtf8().constData());
            }
        }

        obs_output_update(output, out_settings);

        if (!obs_output_active(output)) {
            obs_output_start(output);
        }
        obs_data_release(out_settings);
    }
}

void RadioDock::onToggleRecordClicked() {
    if (recordingActive) {
        recordingActive = false;
        if (!streamingActive) {
            obs_output_stop(output);
        } else {
            obs_data_t* out_settings = obs_output_get_settings(output);
            obs_data_set_bool(out_settings, "record_active", false);
            obs_output_update(output, out_settings);
            obs_data_release(out_settings);
        }
    } else {
        saveSettings();
        recordingActive = true;
        recordStartTime = std::chrono::steady_clock::now();

        obs_data_t* out_settings = obs_output_get_settings(output);
        if (!out_settings) {
            out_settings = obs_data_create();
        }
        obs_data_set_int(out_settings, "protocol_type", protocolInput->currentIndex());
        obs_data_set_string(out_settings, "server_url", urlInput->text().toUtf8().constData());
        obs_data_set_int(out_settings, "server_port", portInput->value());
        obs_data_set_string(out_settings, "mountpoint", mountInput->text().toUtf8().constData());
        obs_data_set_string(out_settings, "username", userInput->text().toUtf8().constData());
        obs_data_set_string(out_settings, "password", passInput->text().toUtf8().constData());
        obs_data_set_int(out_settings, "bitrate", bitrateInput->currentText().toInt());
        obs_data_set_bool(out_settings, "stream_active", streamingActive);
        obs_data_set_bool(out_settings, "record_active", true);

        QString baseDir = pathDisplay->text();
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss");
        QString finalPath = QDir(baseDir).filePath(timestamp + ".mp3");
        obs_data_set_string(out_settings, "record_path", finalPath.toUtf8().constData());

        obs_output_update(output, out_settings);

        if (!obs_output_active(output)) {
            obs_output_start(output);
        }
        obs_data_release(out_settings);
    }
}

void RadioDock::updateStatus() {
    if (!output) return;

    bool active = obs_output_active(output);
    RadioStreamer* streamer = RadioStreamer::get_active_streamer();

    if (!active) {
        streamingActive = false;
        recordingActive = false;
    } else {
        if (streamer) {
            // Keep the active states synced with the actual streamer state
            streamingActive = streamer->is_running() && (streamer->is_connected() || streamer->is_reconnecting());
            recordingActive = streamer->is_recording();
        }
    }

    // ── Update Stream Status UI ──
    if (streamingActive) {
        if (streamer && streamer->is_reconnecting()) {
            int attempt = streamer->get_reconnect_attempt();
            int max = streamer->get_reconnect_max();
            QString text = QString::fromUtf8(obs_module_text("StatusReconnecting")) + QString(" (%1/%2)").arg(attempt).arg(max);
            statusLabel->setText(text);
            statusLabel->setStyleSheet("color: #ffc107;");
        } else {
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - streamStartTime).count();
            int hours = duration / 3600;
            int minutes = (duration % 3600) / 60;
            int seconds = duration % 60;
            
            std::stringstream ss;
            ss << obs_module_text("StatusLive") << " (" << std::setfill('0') << std::setw(2) << hours << ":" 
               << std::setfill('0') << std::setw(2) << minutes << ":" 
               << std::setfill('0') << std::setw(2) << seconds << ")";
               
            statusLabel->setText(QString::fromStdString(ss.str()));
            statusLabel->setStyleSheet("color: #28a745;");
        }

        toggleStreamBtn->setText(QString::fromUtf8(obs_module_text("StopStream")));
        toggleStreamBtn->setStyleSheet("background-color: #dc3545; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    } else {
        statusLabel->setText(QString::fromUtf8(obs_module_text("StatusOffline")));
        statusLabel->setStyleSheet("color: #dc3545;");

        toggleStreamBtn->setText(QString::fromUtf8(obs_module_text("StartStream")));
        toggleStreamBtn->setStyleSheet("background-color: #28a745; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    }

    // ── Update Record Status UI ──
    if (recordingActive) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - recordStartTime).count();
        int hours = duration / 3600;
        int minutes = (duration % 3600) / 60;
        int seconds = duration % 60;
        
        std::stringstream ss;
        ss << obs_module_text("StatusRecording") << " (" << std::setfill('0') << std::setw(2) << hours << ":" 
           << std::setfill('0') << std::setw(2) << minutes << ":" 
           << std::setfill('0') << std::setw(2) << seconds << ")";
           
        recordStatusLabel->setText(QString::fromStdString(ss.str()));
        recordStatusLabel->setStyleSheet("color: #28a745;");

        toggleRecordBtn->setText(QString::fromUtf8(obs_module_text("StopRecord")));
        toggleRecordBtn->setStyleSheet("background-color: #dc3545; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    } else {
        recordStatusLabel->setText(QString::fromUtf8(obs_module_text("StatusOffline")));
        recordStatusLabel->setStyleSheet("color: #dc3545;");

        toggleRecordBtn->setText(QString::fromUtf8(obs_module_text("StartRecord")));
        toggleRecordBtn->setStyleSheet("background-color: #28a745; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    }

    bool running = streamingActive || recordingActive;
    protocolInput->setEnabled(!running);
    urlInput->setEnabled(!running);
    portInput->setEnabled(!running);
    mountInput->setEnabled(!running);
    userInput->setEnabled(!running);
    passInput->setEnabled(!running);
    bitrateInput->setEnabled(!running);
    browseBtn->setEnabled(!running);
}

void RadioDock::onBrowseClicked() {
    QString path = QFileDialog::getExistingDirectory(this, QString::fromUtf8(obs_module_text("SaveRecordDialogTitle")), pathDisplay->text(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty()) {
        pathDisplay->setText(path);
    }
}

void RadioDock::onProtocolChanged(int index) {
    saveSettings();
    currentProtocol = index;
    loadSettings();
}
