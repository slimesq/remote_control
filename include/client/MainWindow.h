#pragma once

#include "Protocol.h"

#include <QMainWindow>
#include <memory>

class QProgressDialog;
class QTableWidgetItem;
class QTreeWidgetItem;
class RemoteClient;
class WatchWindow;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void setEndpoint(const QString& host, quint16 port);

private:
    QProgressDialog* ensureDownloadProgressDialog();
    void setBusyState(bool busy, const QString& message = {});
    void updateActionState();
    void clearRemoteView();
    void showWatchWindow();
    QString currentSelectedFilePath() const;
    QString currentSelectedFileName() const;
    void openSelectedFile();
    void downloadSelectedFile();
    void deleteSelectedFile();
    void wireSignals();
    void requestSelectedDirectory(QTreeWidgetItem* item);
    void populateDriveTree(const QStringList& drives);
    void updateDirectoryView(const QString& path, const QList<remoteqt::FileEntry>& entries);
    QTreeWidgetItem* findItemByPath(const QString& path) const;
    static QString normalizeDrive(const QString& drive);
    static QString joinPath(const QString& basePath, const QString& fileName);

    RemoteClient* m_client = nullptr;
    WatchWindow* m_watchWindow = nullptr;
    std::unique_ptr<Ui::MainWindow> m_ui;
    QProgressDialog* m_downloadProgress = nullptr;
    bool m_connectionVerified = false;
    bool m_busy = false;
};
