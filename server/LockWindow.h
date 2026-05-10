#pragma once

#include <QWidget>

class QLabel;

class LockWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LockWindow(QWidget* parent = nullptr);
    ~LockWindow() override;

    void lockMachine();
    void unlockMachine();
    bool isLocked() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void updateSystemUi(bool locked);

    QLabel* m_label = nullptr;
    bool m_locked = false;
};
