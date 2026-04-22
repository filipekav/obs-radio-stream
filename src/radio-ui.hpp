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
    void onToggleClicked();
    void updateStatus();

private:
    void initUI();
    void loadSettings();
    void saveSettings();

    QLineEdit* urlInput;
    QSpinBox* portInput;
    QLineEdit* mountInput;
    QLineEdit* userInput;
    QLineEdit* passInput;
    QComboBox* bitrateInput;

    QPushButton* toggleBtn;
    QLabel* statusLabel;

    QTimer* statusTimer;
    obs_output_t* output;
};
