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
    void onToggleStreamClicked();
    void onToggleRecordClicked();
    void onToggleBothClicked();
    void onBrowseClicked();
    void updateStatus();
    void onProtocolChanged(int index);

private:
    void initUI();
    void loadSettings();
    void saveSettings();

    class QTabWidget* tabWidget;

    // Settings fields
    QLineEdit* urlInput;
    QComboBox* protocolInput;
    QSpinBox* portInput;
    QLineEdit* mountInput;
    QLineEdit* userInput;
    QLineEdit* passInput;
    QComboBox* bitrateInput;
    QLineEdit* pathDisplay;
    QPushButton* browseBtn;

    // Control fields
    QPushButton* toggleStreamBtn;
    QPushButton* toggleRecordBtn;
    QPushButton* toggleBothBtn;
    QLabel* statusLabel;
    QLabel* recordStatusLabel;

    std::chrono::steady_clock::time_point streamStartTime;
    std::chrono::steady_clock::time_point recordStartTime;

    QTimer* statusTimer;
    obs_output_t* output;

    class QFormLayout* formLayout;
    int currentProtocol;
    
    bool streamingActive = false;
    bool recordingActive = false;
};
