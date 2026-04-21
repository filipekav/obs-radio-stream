#pragma once

#include <QDockWidget>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QTimer;

struct obs_output;
typedef struct obs_output obs_output_t;

class RadioDock : public QDockWidget {
    Q_OBJECT
public:
    RadioDock(QWidget* parent = nullptr);
    ~RadioDock();

private slots:
    void onStartClicked();
    void onStopClicked();
    void updateStatus();

private:
    void initUI();
    void loadSettings();
    void saveSettings();

    QLineEdit* urlInput;
    QSpinBox* portInput;
    QLineEdit* mountInput;
    QLineEdit* passInput;
    QComboBox* bitrateInput;

    QPushButton* startBtn;
    QPushButton* stopBtn;
    QLabel* statusLabel;

    QTimer* statusTimer;
    obs_output_t* output;
};
