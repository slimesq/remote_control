#pragma once

#include "common/Protocol.h"

#include <QMainWindow>
#include <memory>

class QProgressDialog;
class QTableWidgetItem;
class QTreeWidgetItem;
class RemoteClient;
class WatchWindow;

namespace Ui
{
class MainWindow;
}

/** @brief Provides the main client window for connecting to and browsing a remote host. */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /** @brief Creates the main client window. */
    explicit MainWindow(QWidget* _parent = nullptr);

    /** @brief Destroys the main client window. */
    ~MainWindow() override;

    /** @brief Updates the remote server endpoint displayed and used by the client. */
    void setEndpoint(QString const& _host, quint16 _port);

private:
    [[nodiscard]] QProgressDialog* ensureDownloadProgressDialog();
    void setBusyState(bool _busy, QString const& _message = {});
    void updateActionState();
    void clearRemoteView();
    void showWatchWindow();
    [[nodiscard]] QString currentSelectedFilePath() const;
    [[nodiscard]] QString currentSelectedFileName() const;
    void openSelectedFile();
    void downloadSelectedFile();
    void deleteSelectedFile();
    void wireSignals();
    void requestSelectedDirectory(QTreeWidgetItem* _item);
    void populateDriveTree(QStringList const& _drives);
    void updateDirectoryView(QString const& _path,
                             QList<remote_control::FileEntry> const& _entries);
    [[nodiscard]] QTreeWidgetItem* findItemByPath(QString const& _path) const;
    [[nodiscard]] static QString normalizeDrive(QString const& _drive);
    [[nodiscard]] static QString joinPath(QString const& _basePath, QString const& _fileName);

    RemoteClient* m_client{nullptr};
    WatchWindow* m_watchWindow{nullptr};
    std::unique_ptr<Ui::MainWindow> m_ui;
    QProgressDialog* m_downloadProgress{nullptr};
    bool m_connectionVerified{false};
    bool m_busy{false};
};
