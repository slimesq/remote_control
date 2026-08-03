#pragma once

#include "common/Protocol.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <memory>

class QTimer;
class RemoteClient;

namespace Ui
{
class RemoteScreenWindow;
}

/** @brief Displays remote screen frames and converts local mouse input to protocol events. */
class RemoteScreenWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Creates a remote screen widget.
     * @param _parent Parent widget, or nullptr.
     */
    explicit RemoteScreenWidget(QWidget* _parent = nullptr);

    /**
     * @brief Replaces the currently displayed remote frame.
     * @param _image Remote screen image to display.
     */
    void setImage(QImage const& _image);

signals:
    /**
     * @brief Reports a mouse event mapped to remote coordinates.
     * @param _event Mapped protocol mouse event.
     */
    void mouseEventCreated(remote_control::MouseEventPacket const& _event);

protected:
    /**
     * @brief Paints the current remote frame.
     * @param _event Paint event supplied by Qt.
     */
    void paintEvent(QPaintEvent* _event) override;

    /**
     * @brief Forwards a mapped mouse double-click.
     * @param _event Mouse event supplied by Qt.
     */
    void mouseDoubleClickEvent(QMouseEvent* _event) override;

    /**
     * @brief Forwards a mapped mouse press.
     * @param _event Mouse event supplied by Qt.
     */
    void mousePressEvent(QMouseEvent* _event) override;

    /**
     * @brief Forwards a mapped mouse release.
     * @param _event Mouse event supplied by Qt.
     */
    void mouseReleaseEvent(QMouseEvent* _event) override;

    /**
     * @brief Queues a mapped mouse move for rate-limited delivery.
     * @param _event Mouse event supplied by Qt.
     */
    void mouseMoveEvent(QMouseEvent* _event) override;

private:
    /** @brief Sends the most recently queued mouse move. */
    void flushPendingMoveEvent();

    /**
     * @brief Creates a protocol mouse event from local input.
     * @param _action Protocol mouse action.
     * @param _button Protocol mouse button.
     * @param _point Mouse position in widget coordinates.
     * @return Mouse event mapped to remote coordinates.
     */
    [[nodiscard]] remote_control::MouseEventPacket makeMouseEvent(
        remote_control::MouseAction _action,
        remote_control::MouseButton _button,
        QPoint const& _point) const;

    /**
     * @brief Maps a widget position to remote screen coordinates.
     * @param _point Position in widget coordinates.
     * @return Corresponding remote screen position.
     */
    [[nodiscard]] QPoint mapToRemote(QPoint const& _point) const;

    /**
     * @brief Converts a Qt mouse button to its protocol value.
     * @param _button Qt mouse button.
     * @return Corresponding protocol mouse button.
     */
    [[nodiscard]] static remote_control::MouseButton toProtocolButton(
        Qt::MouseButton _button) noexcept;

    QImage m_screenImage;               ///< Most recently received remote-screen image.
    QTimer* m_moveEventTimer{nullptr};  ///< Rate-limits emitted cursor-move events.
    remote_control::MouseEventPacket m_pendingMoveEvent;  ///< Latest unsent cursor position.
    bool m_hasPendingMoveEvent{false};  ///< Whether a cursor position is waiting to be emitted.
};

/** @brief Hosts the live remote screen view and completion-driven frame requests. */
class RemoteScreenWindow : public QDialog
{
public:
    /**
     * @brief Creates a remote screen window backed by the provided client.
     * @param _client Client used for screen and mouse requests.
     * @param _parent Parent widget, or nullptr.
     */
    explicit RemoteScreenWindow(RemoteClient* _client, QWidget* _parent = nullptr);

    /** @brief Destroys the remote screen window. */
    ~RemoteScreenWindow() override;

protected:
    /**
     * @brief Starts completion-driven frame requests when the window appears.
     * @param _event Show event supplied by Qt.
     */
    void showEvent(QShowEvent* _event) override;

    /**
     * @brief Stops frame requests before the window closes.
     * @param _event Close event supplied by Qt.
     */
    void closeEvent(QCloseEvent* _event) override;

private:
    /** @brief Starts one frame request and records its start time. */
    void requestNextFrame();

    /** @brief Schedules the next frame within the configured frame-rate limit. */
    void scheduleNextFrame();

    RemoteClient* m_client{nullptr};  ///< Non-owning client used for screen and control requests.
    RemoteScreenWidget* m_screenWidget{nullptr};   ///< Child widget that displays remote frames.
    std::unique_ptr<Ui::RemoteScreenWindow> m_ui;  ///< Generated remote-screen user interface.
    QTimer* m_frameRequestTimer{nullptr};  ///< Schedules the next bounded-rate frame request.
    QElapsedTimer m_frameRequestElapsed;   ///< Measures time spent on the current frame.
};
