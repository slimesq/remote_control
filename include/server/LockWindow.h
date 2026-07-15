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
    explicit LockWindow(QWidget* _parent = nullptr);
    ~LockWindow() override;

    void lockMachine();
    void unlockMachine();
    bool isLocked() const;

protected:
    void closeEvent(QCloseEvent* _event) override;
    void focusOutEvent(QFocusEvent* _event) override;
    void keyPressEvent(QKeyEvent* _event) override;

private:
    void updateSystemUi(bool _locked);

    std::unique_ptr<Ui::LockWindow> m_ui;
    bool m_locked = false;
};
