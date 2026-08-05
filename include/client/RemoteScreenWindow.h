#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <memory>

class QTimer;
class RemoteClient;
class RemoteScreenWidget;

namespace Ui
{
class RemoteScreenWindow;
}

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
