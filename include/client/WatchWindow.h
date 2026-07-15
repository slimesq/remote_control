#pragma once

#include "Protocol.h"

#include <QDialog>
#include <QImage>
#include <memory>

class QTimer;
class RemoteClient;

namespace Ui {
class WatchWindow;
}

class RemoteScreenWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RemoteScreenWidget(QWidget* _parent = nullptr);

    void setImage(const QImage& _image);

signals:
    void mouseEventCreated(const remote_control::MouseEventPacket& _event);

protected:
    void paintEvent(QPaintEvent* _event) override;
    void mouseDoubleClickEvent(QMouseEvent* _event) override;
    void mousePressEvent(QMouseEvent* _event) override;
    void mouseReleaseEvent(QMouseEvent* _event) override;
    void mouseMoveEvent(QMouseEvent* _event) override;

private:
    void flushPendingMoveEvent();
    remote_control::MouseEventPacket makeMouseEvent(remote_control::MouseAction _action, remote_control::MouseButton _button, const QPoint& _point) const;
    QPoint mapToRemote(const QPoint& _point) const;
    static remote_control::MouseButton toProtocolButton(Qt::MouseButton _button);

    QImage m_image;
    QTimer* m_moveEventTimer = nullptr;
    remote_control::MouseEventPacket m_pendingMoveEvent {};
    bool m_hasPendingMoveEvent = false;
};

class WatchWindow : public QDialog
{
    Q_OBJECT

public:
    explicit WatchWindow(RemoteClient* _client, QWidget* _parent = nullptr);
    ~WatchWindow() override;

protected:
    void showEvent(QShowEvent* _event) override;
    void closeEvent(QCloseEvent* _event) override;

private:
    RemoteClient* m_client = nullptr;
    RemoteScreenWidget* m_screenWidget = nullptr;
    std::unique_ptr<Ui::WatchWindow> m_ui;
    QTimer* m_timer = nullptr;
};
