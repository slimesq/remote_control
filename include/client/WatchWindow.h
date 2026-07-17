#pragma once

#include "common/Protocol.h"

#include <QDialog>
#include <QImage>
#include <memory>

class QTimer;
class RemoteClient;

namespace Ui
{
class WatchWindow;
}

/** @brief Displays remote screen frames and converts local mouse input to protocol events. */
class RemoteScreenWidget : public QWidget
{
    Q_OBJECT

public:
    /** @brief Creates a remote screen widget. */
    explicit RemoteScreenWidget(QWidget* _parent = nullptr);

    /** @brief Replaces the currently displayed remote frame. */
    void setImage(QImage const& _image);

signals:
    void mouseEventCreated(remote_control::MouseEventPacket const& _event);

protected:
    void paintEvent(QPaintEvent* _event) override;
    void mouseDoubleClickEvent(QMouseEvent* _event) override;
    void mousePressEvent(QMouseEvent* _event) override;
    void mouseReleaseEvent(QMouseEvent* _event) override;
    void mouseMoveEvent(QMouseEvent* _event) override;

private:
    void flushPendingMoveEvent();
    [[nodiscard]] remote_control::MouseEventPacket makeMouseEvent(
        remote_control::MouseAction _action,
        remote_control::MouseButton _button,
        QPoint const& _point) const;
    [[nodiscard]] QPoint mapToRemote(QPoint const& _point) const;
    [[nodiscard]] static remote_control::MouseButton toProtocolButton(
        Qt::MouseButton _button) noexcept;

    QImage m_image;
    QTimer* m_moveEventTimer{nullptr};
    remote_control::MouseEventPacket m_pendingMoveEvent{};
    bool m_hasPendingMoveEvent{false};
};

/** @brief Hosts the live remote screen view and periodic frame requests. */
class WatchWindow : public QDialog
{
    Q_OBJECT

public:
    /** @brief Creates a watch window backed by the provided client. */
    explicit WatchWindow(RemoteClient* _client, QWidget* _parent = nullptr);

    /** @brief Destroys the watch window. */
    ~WatchWindow() override;

protected:
    void showEvent(QShowEvent* _event) override;
    void closeEvent(QCloseEvent* _event) override;

private:
    RemoteClient* m_client{nullptr};
    RemoteScreenWidget* m_screenWidget{nullptr};
    std::unique_ptr<Ui::WatchWindow> m_ui;
    QTimer* m_timer{nullptr};
};
