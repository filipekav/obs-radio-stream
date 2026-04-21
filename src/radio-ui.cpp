#include "radio-ui.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <obs-module.h>
#include <obs-frontend-api.h>

RadioDock::RadioDock(QWidget* parent) : QDockWidget(parent) {
    setWindowTitle("Controle de Rádio Icecast");
    setObjectName("RadioDock");
    
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
    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);

    QFormLayout* form = new QFormLayout();
    
    urlInput = new QLineEdit();
    form->addRow("URL do Servidor:", urlInput);
    
    portInput = new QSpinBox();
    portInput->setRange(1, 65535);
    portInput->setValue(8000);
    form->addRow("Porta:", portInput);
    
    mountInput = new QLineEdit();
    mountInput->setText("/stream");
    form->addRow("Ponto de Montagem:", mountInput);
    
    passInput = new QLineEdit();
    passInput->setEchoMode(QLineEdit::Password);
    form->addRow("Senha:", passInput);
    
    bitrateInput = new QComboBox();
    bitrateInput->addItems({"64", "96", "128", "192", "320"});
    bitrateInput->setCurrentText("128");
    form->addRow("Taxa de Bits (kbps):", bitrateInput);

    layout->addLayout(form);

    startBtn = new QPushButton("Iniciar Transmissão de Rádio");
    startBtn->setStyleSheet("background-color: #28a745; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    stopBtn = new QPushButton("Parar Transmissão");
    stopBtn->setStyleSheet("background-color: #dc3545; color: white; padding: 8px; font-weight: bold; border-radius: 4px;");
    
    layout->addWidget(startBtn);
    layout->addWidget(stopBtn);

    statusLabel = new QLabel("Status: Offline");
    statusLabel->setAlignment(Qt::AlignCenter);
    QFont f = statusLabel->font();
    f.setBold(true);
    f.setPointSize(12);
    statusLabel->setFont(f);
    layout->addWidget(statusLabel);
    
    layout->addStretch();
    setWidget(widget);

    connect(startBtn, &QPushButton::clicked, this, &RadioDock::onStartClicked);
    connect(stopBtn, &QPushButton::clicked, this, &RadioDock::onStopClicked);
}

void RadioDock::loadSettings() {
    obs_data_t* settings = obs_frontend_get_persistent_data();
    if (!settings) return;

    obs_data_t* d = obs_data_get_obj(settings, "icecast_radio_settings");
    
    if (d) {
        urlInput->setText(obs_data_get_string(d, "server_url"));
        
        int port = obs_data_get_int(d, "server_port");
        if (port) portInput->setValue(port);
        
        const char* mount = obs_data_get_string(d, "mountpoint");
        if (mount && strlen(mount) > 0) mountInput->setText(mount);
        
        passInput->setText(obs_data_get_string(d, "password"));
        
        int br = obs_data_get_int(d, "bitrate");
        if (br) bitrateInput->setCurrentText(QString::number(br));
        
        obs_data_release(d);
    }
    obs_data_release(settings);
}

void RadioDock::saveSettings() {
    obs_data_t* settings = obs_frontend_get_persistent_data();
    if (!settings) return;

    obs_data_t* d = obs_data_create();

    obs_data_set_string(d, "server_url", urlInput->text().toUtf8().constData());
    obs_data_set_int(d, "server_port", portInput->value());
    obs_data_set_string(d, "mountpoint", mountInput->text().toUtf8().constData());
    obs_data_set_string(d, "password", passInput->text().toUtf8().constData());
    obs_data_set_int(d, "bitrate", bitrateInput->currentText().toInt());

    obs_data_set_obj(settings, "icecast_radio_settings", d);
    
    obs_data_release(d);
    obs_data_release(settings);
}

void RadioDock::onStartClicked() {
    if (obs_output_active(output)) return;
    
    saveSettings();

    obs_data_t* out_settings = obs_data_create();
    obs_data_set_string(out_settings, "server_url", urlInput->text().toUtf8().constData());
    obs_data_set_int(out_settings, "server_port", portInput->value());
    obs_data_set_string(out_settings, "mountpoint", mountInput->text().toUtf8().constData());
    obs_data_set_string(out_settings, "password", passInput->text().toUtf8().constData());
    obs_data_set_int(out_settings, "bitrate", bitrateInput->currentText().toInt());
    
    obs_output_update(output, out_settings);
    obs_data_release(out_settings);
    
    obs_output_start(output);
}

void RadioDock::onStopClicked() {
    obs_output_stop(output);
}

void RadioDock::updateStatus() {
    if (!output) return;

    bool active = obs_output_active(output);
    startBtn->setEnabled(!active);
    stopBtn->setEnabled(active);
    
    urlInput->setEnabled(!active);
    portInput->setEnabled(!active);
    mountInput->setEnabled(!active);
    passInput->setEnabled(!active);
    bitrateInput->setEnabled(!active);

    if (active) {
        statusLabel->setText("Status: Ao Vivo");
        statusLabel->setStyleSheet("color: #28a745;");
    } else {
        statusLabel->setText("Status: Offline");
        statusLabel->setStyleSheet("color: #dc3545;");
    }
}
