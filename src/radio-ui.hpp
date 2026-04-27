#pragma once

#include <QWidget>
#include <chrono>

class QCheckBox;

class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QTimer;

struct obs_output;
typedef struct obs_output obs_output_t;

class RadioDock : public QWidget {
    Q_OBJECT
public:
    RadioDock(QWidget* parent = nullptr);
    ~RadioDock();

private slots:
    void onToggleClicked();
    void onBrowseClicked();
    void updateStatus();

private:
    void initUI();
    void loadSettings();
    void saveSettings();

    QLineEdit* urlInput;
    QComboBox* protocolInput;
    QSpinBox* portInput;
    QLineEdit* mountInput;
    QLineEdit* userInput;
    QLineEdit* passInput;
    QComboBox* bitrateInput;

    QCheckBox* recordCheck;
    QLineEdit* pathDisplay;
    QPushButton* browseBtn;

    QPushButton* toggleBtn;
    QLabel* statusLabel;

    std::chrono::steady_clock::time_point startTime;

    QTimer* statusTimer;
    obs_output_t* output;
};
