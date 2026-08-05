#include "client/RemoteScreenWindow.h"

#include "client/RemoteClient.h"
#include "client/RemoteScreenWidget.h"
#include "ui_RemoteScreenWindow.h"

#include <QCloseEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

constexpr int MinimumWatchFrameIntervalMs{33};  // Caps requests at approximately 30 FPS.

}  // namespace

RemoteScreenWindow::RemoteScreenWindow(RemoteClient* _client, QWidget* _parent)
    : QDialog{_parent},
      m_client{_client},
      m_ui{std::make_unique<Ui::RemoteScreenWindow>()},
      m_frameRequestTimer{new QTimer{this}}
{
    this->m_ui->setupUi(this);

    auto* const screenLayout{new QVBoxLayout{this->m_ui->screenContainer}};
    screenLayout->setContentsMargins(0, 0, 0, 0);
    this->m_screenWidget = new RemoteScreenWidget{this->m_ui->screenContainer};
    screenLayout->addWidget(this->m_screenWidget);

    // A single-shot timer schedules only after the current frame request has completed.
    this->m_frameRequestTimer->setSingleShot(true);

    connect(
        this->m_frameRequestTimer, &QTimer::timeout, this, &RemoteScreenWindow::requestNextFrame);
    connect(this->m_screenWidget,
            &RemoteScreenWidget::mouseEventCreated,
            this->m_client,
            &RemoteClient::sendMouseEvent);
    connect(
        this->m_ui->lockButton, &QPushButton::clicked, this->m_client, &RemoteClient::lockRemote);
    connect(this->m_ui->unlockButton,
            &QPushButton::clicked,
            this->m_client,
            &RemoteClient::unlockRemote);
    connect(this->m_client,
            &RemoteClient::screenFrameReady,
            this->m_screenWidget,
            &RemoteScreenWidget::setImage);
    connect(this->m_client,
            &RemoteClient::screenFrameRequestFinished,
            this,
            &RemoteScreenWindow::scheduleNextFrame);
}

RemoteScreenWindow::~RemoteScreenWindow() = default;

void RemoteScreenWindow::showEvent(QShowEvent* _event)
{
    QDialog::showEvent(_event);
    this->m_frameRequestTimer->stop();
    this->m_frameRequestElapsed.invalidate();
    // Kick off the first frame immediately so the dialog does not open empty.
    this->requestNextFrame();
}

void RemoteScreenWindow::closeEvent(QCloseEvent* _event)
{
    this->m_frameRequestTimer->stop();
    this->m_frameRequestElapsed.invalidate();
    this->m_client->stopScreenStream();
    this->m_client->stopControlStream();
    QDialog::closeEvent(_event);
}

void RemoteScreenWindow::requestNextFrame()
{
    if (!this->isVisible())
    {
        return;
    }

    // Measure from request submission so processing time counts toward the frame interval.
    this->m_frameRequestElapsed.start();
    this->m_client->requestScreenFrame();
}

void RemoteScreenWindow::scheduleNextFrame()
{
    if (!this->isVisible())
    {
        return;
    }

    // Account for request latency and wait only for the remainder of the minimum frame interval.
    qint64 const elapsedMs{this->m_frameRequestElapsed.isValid()
                               ? this->m_frameRequestElapsed.elapsed()
                               : MinimumWatchFrameIntervalMs};
    int const delayMs{elapsedMs >= MinimumWatchFrameIntervalMs
                          ? 0
                          : MinimumWatchFrameIntervalMs - static_cast<int>(elapsedMs)};
    this->m_frameRequestTimer->start(delayMs);
}
