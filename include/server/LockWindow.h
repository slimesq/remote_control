#pragma once

#include <QWidget>
#include <memory>

class QKeyEvent;

namespace Ui {
class LockWindow;
}

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
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateSystemUi(bool locked);

    std::unique_ptr<Ui::LockWindow> m_ui;
    bool m_locked = false;
};
