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
    explicit MainWindow(QWidget* _parent = nullptr);
    ~MainWindow() override;
    void setEndpoint(const QString& _host, quint16 _port);

private:
    QProgressDialog* ensureDownloadProgressDialog();
    void setBusyState(bool _busy, const QString& _message = {});
    void updateActionState();
    void clearRemoteView();
    void showWatchWindow();
    QString currentSelectedFilePath() const;
    QString currentSelectedFileName() const;
    void openSelectedFile();
    void downloadSelectedFile();
    void deleteSelectedFile();
    void wireSignals();
    void requestSelectedDirectory(QTreeWidgetItem* _item);
    void populateDriveTree(const QStringList& _drives);
    void updateDirectoryView(const QString& _path, const QList<remote_control::FileEntry>& _entries);
    QTreeWidgetItem* findItemByPath(const QString& _path) const;
    static QString normalizeDrive(const QString& _drive);
    static QString joinPath(const QString& _basePath, const QString& _fileName);

    RemoteClient* m_client = nullptr;
    WatchWindow* m_watchWindow = nullptr;
    std::unique_ptr<Ui::MainWindow> m_ui;
    QProgressDialog* m_downloadProgress = nullptr;
    bool m_connectionVerified = false;
    bool m_busy = false;
};
