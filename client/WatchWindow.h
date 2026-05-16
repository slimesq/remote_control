#pragma once

#include "../common/Protocol.h"

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
    explicit RemoteScreenWidget(QWidget* parent = nullptr);

    void setImage(const QImage& image);

signals:
    void mouseEventCreated(const remoteqt::MouseEventPacket& event);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    remoteqt::MouseEventPacket makeMouseEvent(remoteqt::MouseAction action, remoteqt::MouseButton button, const QPoint& point) const;
    QPoint mapToRemote(const QPoint& point) const;
    static remoteqt::MouseButton toProtocolButton(Qt::MouseButton button);

    QImage m_image;
};

class WatchWindow : public QDialog
{
    Q_OBJECT

public:
    explicit WatchWindow(RemoteClient* client, QWidget* parent = nullptr);
    ~WatchWindow() override;

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    RemoteClient* m_client = nullptr;
    RemoteScreenWidget* m_screenWidget = nullptr;
    std::unique_ptr<Ui::WatchWindow> m_ui;
    QTimer* m_timer = nullptr;
};
