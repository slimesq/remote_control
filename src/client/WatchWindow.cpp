#include "client/WatchWindow.h"

#include "client/RemoteClient.h"
#include "ui_WatchWindow.h"

#include <QCloseEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

constexpr int MoveEventIntervalMs{16};
constexpr int MinimumScreenWidth{800};
constexpr int MinimumScreenHeight{500};
constexpr int WatchFrameIntervalMs{200};

QPoint mouseEventPosition(QMouseEvent const* _event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return _event->position().toPoint();
#else
    return _event->pos();
#endif
}

}  // namespace

RemoteScreenWidget::RemoteScreenWidget(QWidget* _parent)
    : QWidget{_parent}, m_moveEventTimer{new QTimer{this}}
{
    setMouseTracking(true);
    setMinimumSize(MinimumScreenWidth, MinimumScreenHeight);
    // Merge rapid mouse-move bursts into a small fixed-rate stream.
    this->m_moveEventTimer->setSingleShot(true);
    this->m_moveEventTimer->setInterval(MoveEventIntervalMs);
    connect(
        this->m_moveEventTimer, &QTimer::timeout, this, &RemoteScreenWidget::flushPendingMoveEvent);
}

void RemoteScreenWidget::setImage(QImage const& _image)
{
    this->m_image = _image;
    update();
}

void RemoteScreenWidget::paintEvent(QPaintEvent* _event)
{
    static_cast<void>(_event);
    QPainter painter{this};
    painter.fillRect(rect(), Qt::black);
    if (!this->m_image.isNull())
    {
        painter.drawImage(rect(), this->m_image);
    }
}

void RemoteScreenWidget::mouseDoubleClickEvent(QMouseEvent* _event)
{
    emit mouseEventCreated(this->makeMouseEvent(remote_control::MouseAction::DoubleClick,
                                                this->toProtocolButton(_event->button()),
                                                mouseEventPosition(_event)));
    QWidget::mouseDoubleClickEvent(_event);
}

void RemoteScreenWidget::mousePressEvent(QMouseEvent* _event)
{
    emit mouseEventCreated(this->makeMouseEvent(remote_control::MouseAction::Press,
                                                this->toProtocolButton(_event->button()),
                                                mouseEventPosition(_event)));
    QWidget::mousePressEvent(_event);
}

void RemoteScreenWidget::mouseReleaseEvent(QMouseEvent* _event)
{
    emit mouseEventCreated(this->makeMouseEvent(remote_control::MouseAction::Release,
                                                this->toProtocolButton(_event->button()),
                                                mouseEventPosition(_event)));
    QWidget::mouseReleaseEvent(_event);
}

void RemoteScreenWidget::mouseMoveEvent(QMouseEvent* _event)
{
    // Keep only the latest cursor position; press/release/double-click still go out immediately.
    this->m_pendingMoveEvent = this->makeMouseEvent(remote_control::MouseAction::Click,
                                                    remote_control::MouseButton::None,
                                                    mouseEventPosition(_event));
    this->m_hasPendingMoveEvent = true;
    if (!this->m_moveEventTimer->isActive())
    {
        this->m_moveEventTimer->start();
    }
    QWidget::mouseMoveEvent(_event);
}

void RemoteScreenWidget::flushPendingMoveEvent()
{
    if (!this->m_hasPendingMoveEvent)
    {
        return;
    }
    this->m_hasPendingMoveEvent = false;
    emit mouseEventCreated(this->m_pendingMoveEvent);
}

remote_control::MouseEventPacket RemoteScreenWidget::makeMouseEvent(
    remote_control::MouseAction _action,
    remote_control::MouseButton _button,
    QPoint const& _point) const
{
    QPoint const remotePoint{this->mapToRemote(_point)};
    remote_control::MouseEventPacket event;
    event.action = static_cast<quint16>(_action);
    event.button = static_cast<quint16>(_button);
    event.x = remotePoint.x();
    event.y = remotePoint.y();
    return event;
}

QPoint RemoteScreenWidget::mapToRemote(QPoint const& _point) const
{
    if (this->m_image.isNull() || width() <= 0 || height() <= 0)
    {
        return {};
    }
    int const remoteX{_point.x() * this->m_image.width() / qMax(1, width())};
    int const remoteY{_point.y() * this->m_image.height() / qMax(1, height())};
    return {remoteX, remoteY};
}

remote_control::MouseButton RemoteScreenWidget::toProtocolButton(Qt::MouseButton _button) noexcept
{
    switch (_button)
    {
        case Qt::LeftButton:
            return remote_control::MouseButton::Left;
        case Qt::RightButton:
            return remote_control::MouseButton::Right;
        case Qt::MiddleButton:
            return remote_control::MouseButton::Middle;
        default:
            return remote_control::MouseButton::None;
    }
}

WatchWindow::WatchWindow(RemoteClient* _client, QWidget* _parent)
    : QDialog{_parent},
      m_client{_client},
      m_ui{std::make_unique<Ui::WatchWindow>()},
      m_timer{new QTimer{this}}
{
    this->m_ui->setupUi(this);

    auto* const screenLayout{new QVBoxLayout{this->m_ui->screenContainer}};
    screenLayout->setContentsMargins(0, 0, 0, 0);
    this->m_screenWidget = new RemoteScreenWidget{this->m_ui->screenContainer};
    screenLayout->addWidget(this->m_screenWidget);

    this->m_timer->setInterval(WatchFrameIntervalMs);

    connect(this->m_timer, &QTimer::timeout, this->m_client, &RemoteClient::requestWatchFrame);
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
            &RemoteClient::watchFrameReady,
            this->m_screenWidget,
            &RemoteScreenWidget::setImage);
}

WatchWindow::~WatchWindow() = default;

void WatchWindow::showEvent(QShowEvent* _event)
{
    this->m_timer->start();
    // Kick off the first frame immediately so the dialog does not open empty.
    this->m_client->requestWatchFrame();
    QDialog::showEvent(_event);
}

void WatchWindow::closeEvent(QCloseEvent* _event)
{
    this->m_timer->stop();
    QDialog::closeEvent(_event);
}
