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
    
    output = obs_output_create("radio_output", "Icecast Radio Output", nullptr, nullptr);

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

    QFormLayout* form = new QFormLayout();
    
    urlInput = new QLineEdit();
    form->addRow(QString::fromUtf8(obs_module_text("ServerUrl")), urlInput);
    
    portInput = new QSpinBox();
    portInput->setRange(1, 65535);
    portInput->setValue(8000);
    form->addRow(QString::fromUtf8(obs_module_text("ServerPort")), portInput);
    
    mountInput = new QLineEdit();
    mountInput->setText("/stream");
    form->addRow(QString::fromUtf8(obs_module_text("Mountpoint")), mountInput);
    
    userInput = new QLineEdit();
    userInput->setText("source");
    form->addRow(QString::fromUtf8(obs_module_text("Username")), userInput);
    
    passInput = new QLineEdit();
    passInput->setEchoMode(QLineEdit::Password);
    form->addRow(QString::fromUtf8(obs_module_text("Password")), passInput);
    
    bitrateInput = new QComboBox();
    bitrateInput->addItems({"64", "96", "128", "192", "320"});
    bitrateInput->setCurrentText("128");
    form->addRow(QString::fromUtf8(obs_module_text("Bitrate")), bitrateInput);

    recordCheck = new QCheckBox(QString::fromUtf8(obs_module_text("RecordLocally")));
    form->addRow("", recordCheck);

    QHBoxLayout* pathLayout = new QHBoxLayout();
    pathDisplay = new QLineEdit();
    pathDisplay->setReadOnly(true);
    browseBtn = new QPushButton(QString::fromUtf8(obs_module_text("Browse")));
    pathLayout->addWidget(pathDisplay);
    pathLayout->addWidget(browseBtn);
    form->addRow(QString::fromUtf8(obs_module_text("RecordPath")), pathLayout);

    layout->addLayout(form);

    toggleBtn = new QPushButton(QString::fromUtf8(obs_module_text("StartStream")));
    toggleBtn->setStyleSheet("background-color: #28a745; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    layout->addWidget(toggleBtn);

    statusLabel = new QLabel(QString::fromUtf8(obs_module_text("StatusOffline")));
    statusLabel->setAlignment(Qt::AlignCenter);
    QFont f = statusLabel->font();
    f.setBold(true);
    f.setPointSize(12);
    statusLabel->setFont(f);
    layout->addWidget(statusLabel);
    
    layout->addStretch();

    connect(toggleBtn, &QPushButton::clicked, this, &RadioDock::onToggleClicked);
    connect(browseBtn, &QPushButton::clicked, this, &RadioDock::onBrowseClicked);
}

void RadioDock::loadSettings() {
    QSettings settings("OBSPlugins", "RadioStreamer");
    
    urlInput->setText(settings.value("server_url").toString());
    
    int port = settings.value("server_port", 8000).toInt();
    portInput->setValue(port);
    
    QString mount = settings.value("mountpoint", "/stream").toString();
    mountInput->setText(mount);
    
    userInput->setText(settings.value("username", "source").toString());
    
    passInput->setText(settings.value("password").toString());
    
    int br = settings.value("bitrate", 128).toInt();
    bitrateInput->setCurrentText(QString::number(br));

    recordCheck->setChecked(settings.value("record_locally", false).toBool());

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
}

void RadioDock::saveSettings() {
    QSettings settings("OBSPlugins", "RadioStreamer");

    settings.setValue("server_url", urlInput->text());
    settings.setValue("server_port", portInput->value());
    settings.setValue("mountpoint", mountInput->text());
    settings.setValue("username", userInput->text());
    settings.setValue("password", passInput->text());
    settings.setValue("bitrate", bitrateInput->currentText().toInt());
    settings.setValue("record_locally", recordCheck->isChecked());
    settings.setValue("record_path", pathDisplay->text());
}

void RadioDock::onToggleClicked() {
    if (obs_output_active(output)) {
        obs_output_stop(output);
    } else {
        saveSettings();

        obs_data_t* out_settings = obs_data_create();
        obs_data_set_string(out_settings, "server_url", urlInput->text().toUtf8().constData());
        obs_data_set_int(out_settings, "server_port", portInput->value());
        obs_data_set_string(out_settings, "mountpoint", mountInput->text().toUtf8().constData());
        obs_data_set_string(out_settings, "username", userInput->text().toUtf8().constData());
        obs_data_set_string(out_settings, "password", passInput->text().toUtf8().constData());
        obs_data_set_int(out_settings, "bitrate", bitrateInput->currentText().toInt());
        obs_data_set_bool(out_settings, "record_locally", recordCheck->isChecked());
        
        QString baseDir = pathDisplay->text();
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss");
        QString finalPath = QDir(baseDir).filePath(timestamp + ".mp3");
        obs_data_set_string(out_settings, "record_path", finalPath.toUtf8().constData());
        
        obs_output_update(output, out_settings);
        obs_data_release(out_settings);
        
        startTime = std::chrono::steady_clock::now();
        obs_output_start(output);
    }
}

void RadioDock::updateStatus() {
    if (!output) return;

    bool active = obs_output_active(output);
    
    if (active) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        int hours = duration / 3600;
        int minutes = (duration % 3600) / 60;
        int seconds = duration % 60;
        
        std::stringstream ss;
        ss << obs_module_text("StatusLive") << " (" << std::setfill('0') << std::setw(2) << hours << ":" 
           << std::setfill('0') << std::setw(2) << minutes << ":" 
           << std::setfill('0') << std::setw(2) << seconds << ")";
           
        statusLabel->setText(QString::fromStdString(ss.str()));
        statusLabel->setStyleSheet("color: #28a745;");

        toggleBtn->setText(QString::fromUtf8(obs_module_text("StopStream")));
        toggleBtn->setStyleSheet("background-color: #dc3545; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    } else {
        statusLabel->setText(QString::fromUtf8(obs_module_text("StatusOffline")));
        statusLabel->setStyleSheet("color: #dc3545;");

        toggleBtn->setText(QString::fromUtf8(obs_module_text("StartStream")));
        toggleBtn->setStyleSheet("background-color: #28a745; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    }
    
    urlInput->setEnabled(!active);
    portInput->setEnabled(!active);
    mountInput->setEnabled(!active);
    userInput->setEnabled(!active);
    passInput->setEnabled(!active);
    bitrateInput->setEnabled(!active);
    recordCheck->setEnabled(!active);
    browseBtn->setEnabled(!active);
}

void RadioDock::onBrowseClicked() {
    QString path = QFileDialog::getExistingDirectory(this, QString::fromUtf8(obs_module_text("SaveRecordDialogTitle")), pathDisplay->text(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty()) {
        pathDisplay->setText(path);
        recordCheck->setChecked(true);
    }
}
