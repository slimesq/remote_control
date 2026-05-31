#include "WatchWindow.h"

#include "RemoteClient.h"
#include "ui_WatchWindow.h"

#include <QCloseEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int MoveEventIntervalMs = 16;

}

RemoteScreenWidget::RemoteScreenWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(800, 500);
    // Merge rapid mouse-move bursts into a small fixed-rate stream.
    m_moveEventTimer = new QTimer(this);
    m_moveEventTimer->setSingleShot(true);
    m_moveEventTimer->setInterval(MoveEventIntervalMs);
    connect(m_moveEventTimer, &QTimer::timeout, this, &RemoteScreenWidget::flushPendingMoveEvent);
}

void RemoteScreenWidget::setImage(const QImage& image)
{
    m_image = image;
    update();
}

void RemoteScreenWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (!m_image.isNull()) {
        painter.drawImage(rect(), m_image);
    }
}

void RemoteScreenWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    emit mouseEventCreated(makeMouseEvent(remoteqt::MouseAction::DoubleClick, toProtocolButton(event->button()), event->position().toPoint()));
    QWidget::mouseDoubleClickEvent(event);
}

void RemoteScreenWidget::mousePressEvent(QMouseEvent* event)
{
    emit mouseEventCreated(makeMouseEvent(remoteqt::MouseAction::Press, toProtocolButton(event->button()), event->position().toPoint()));
    QWidget::mousePressEvent(event);
}

void RemoteScreenWidget::mouseReleaseEvent(QMouseEvent* event)
{
    emit mouseEventCreated(makeMouseEvent(remoteqt::MouseAction::Release, toProtocolButton(event->button()), event->position().toPoint()));
    QWidget::mouseReleaseEvent(event);
}

void RemoteScreenWidget::mouseMoveEvent(QMouseEvent* event)
{
    // Keep only the latest cursor position; press/release/double-click still go out immediately.
    m_pendingMoveEvent = makeMouseEvent(remoteqt::MouseAction::Click, remoteqt::MouseButton::None, event->position().toPoint());
    m_hasPendingMoveEvent = true;
    if (!m_moveEventTimer->isActive()) {
        m_moveEventTimer->start();
    }
    QWidget::mouseMoveEvent(event);
}

void RemoteScreenWidget::flushPendingMoveEvent()
{
    if (!m_hasPendingMoveEvent) {
        return;
    }
    m_hasPendingMoveEvent = false;
    emit mouseEventCreated(m_pendingMoveEvent);
}

remoteqt::MouseEventPacket RemoteScreenWidget::makeMouseEvent(remoteqt::MouseAction action, remoteqt::MouseButton button, const QPoint& point) const
{
    const QPoint remotePoint = mapToRemote(point);
    remoteqt::MouseEventPacket event;
    event.action = static_cast<quint16>(action);
    event.button = static_cast<quint16>(button);
    event.x = remotePoint.x();
    event.y = remotePoint.y();
    return event;
}

QPoint RemoteScreenWidget::mapToRemote(const QPoint& point) const
{
    if (m_image.isNull() || width() <= 0 || height() <= 0) {
        return {};
    }
    const int remoteX = point.x() * m_image.width() / qMax(1, width());
    const int remoteY = point.y() * m_image.height() / qMax(1, height());
    return { remoteX, remoteY };
}

remoteqt::MouseButton RemoteScreenWidget::toProtocolButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return remoteqt::MouseButton::Left;
    case Qt::RightButton:
        return remoteqt::MouseButton::Right;
    case Qt::MiddleButton:
        return remoteqt::MouseButton::Middle;
    default:
        return remoteqt::MouseButton::None;
    }
}

WatchWindow::WatchWindow(RemoteClient* client, QWidget* parent)
    : QDialog(parent)
    , m_client(client)
    , m_ui(std::make_unique<Ui::WatchWindow>())
{
    m_ui->setupUi(this);

    auto* screenLayout = new QVBoxLayout(m_ui->screenContainer);
    screenLayout->setContentsMargins(0, 0, 0, 0);
    m_screenWidget = new RemoteScreenWidget(m_ui->screenContainer);
    screenLayout->addWidget(m_screenWidget);

    m_timer = new QTimer(this);
    m_timer->setInterval(200);

    connect(m_timer, &QTimer::timeout, m_client, &RemoteClient::requestWatchFrame);
    connect(m_screenWidget, &RemoteScreenWidget::mouseEventCreated, m_client, &RemoteClient::sendMouseEvent);
    connect(m_ui->lockButton, &QPushButton::clicked, m_client, &RemoteClient::lockRemote);
    connect(m_ui->unlockButton, &QPushButton::clicked, m_client, &RemoteClient::unlockRemote);
    connect(m_client, &RemoteClient::watchFrameReady, m_screenWidget, &RemoteScreenWidget::setImage);
}

WatchWindow::~WatchWindow() = default;

void WatchWindow::showEvent(QShowEvent* event)
{
    m_timer->start();
    // Kick off the first frame immediately so the dialog does not open empty.
    m_client->requestWatchFrame();
    QDialog::showEvent(event);
}

void WatchWindow::closeEvent(QCloseEvent* event)
{
    m_timer->stop();
    QDialog::closeEvent(event);
}
