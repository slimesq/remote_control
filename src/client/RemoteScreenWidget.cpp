#include "client/RemoteScreenWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

namespace
{

constexpr int MoveEventIntervalMs{16};
constexpr int MinimumScreenWidth{800};
constexpr int MinimumScreenHeight{500};

/**
 * @brief Returns a mouse position for both Qt 5 and Qt 6.
 * @param _event Mouse event supplied by Qt.
 * @return Mouse position in widget coordinates.
 */
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
    this->m_screenImage = _image;
    update();
}

void RemoteScreenWidget::cancelPendingMouseMove()
{
    this->m_moveEventTimer->stop();
    this->m_hasPendingMoveEvent = false;
}

void RemoteScreenWidget::paintEvent(QPaintEvent* _event)
{
    static_cast<void>(_event);
    QPainter painter{this};
    painter.fillRect(rect(), Qt::black);
    if (!this->m_screenImage.isNull())
    {
        painter.drawImage(rect(), this->m_screenImage);
    }
}

void RemoteScreenWidget::mouseDoubleClickEvent(QMouseEvent* _event)
{
    // Preserve input order by delivering the latest position before the button transition.
    this->flushPendingMoveEvent();
    emit mouseEventCreated(this->makeMouseEvent(remote_control::MouseAction::DoubleClick,
                                                this->toProtocolButton(_event->button()),
                                                mouseEventPosition(_event)));
    QWidget::mouseDoubleClickEvent(_event);
}

void RemoteScreenWidget::mousePressEvent(QMouseEvent* _event)
{
    // Preserve input order by delivering the latest position before the button transition.
    this->flushPendingMoveEvent();
    emit mouseEventCreated(this->makeMouseEvent(remote_control::MouseAction::Press,
                                                this->toProtocolButton(_event->button()),
                                                mouseEventPosition(_event)));
    QWidget::mousePressEvent(_event);
}

void RemoteScreenWidget::mouseReleaseEvent(QMouseEvent* _event)
{
    // Preserve input order by delivering the latest position before the button transition.
    this->flushPendingMoveEvent();
    emit mouseEventCreated(this->makeMouseEvent(remote_control::MouseAction::Release,
                                                this->toProtocolButton(_event->button()),
                                                mouseEventPosition(_event)));
    QWidget::mouseReleaseEvent(_event);
}

void RemoteScreenWidget::mouseMoveEvent(QMouseEvent* _event)
{
    // Keep only the latest cursor position; press/release/double-click still go out immediately.
    this->m_pendingMoveEvent = this->makeMouseEvent(remote_control::MouseAction::Move,
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
    // Explicit flushes must cancel the scheduled timeout so it cannot run after a later event.
    this->m_moveEventTimer->stop();
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
    // Coordinate scaling requires both a decoded source frame and non-zero widget dimensions.
    if (this->m_screenImage.isNull() || width() <= 0 || height() <= 0)
    {
        return {};
    }
    int const remoteX{_point.x() * this->m_screenImage.width() / width()};
    int const remoteY{_point.y() * this->m_screenImage.height() / height()};
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
