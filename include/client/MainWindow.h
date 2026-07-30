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
    /**
     * @brief Creates the main client window.
     * @param _parent Parent widget, or nullptr for a top-level window.
     */
    explicit MainWindow(QWidget* _parent = nullptr);

    /** @brief Destroys the main client window. */
    ~MainWindow() override;

    /**
     * @brief Updates the remote server endpoint displayed and used by the client.
     * @param _host Remote server host name or address.
     * @param _port Remote server TCP port.
     */
    void setEndpoint(QString const& _host, quint16 _port);

private:
    /**
     * @brief Creates the download progress dialog on first use.
     * @return Parent-owned download progress dialog.
     */
    [[nodiscard]] QProgressDialog* ensureDownloadProgressDialog();

    /** @brief Enables actions that are valid for the current state. */
    void updateActionState();

    /**
     * @brief Updates the active download path and related action availability.
     * @param _remotePath Remote path being downloaded, or empty to clear the active download.
     */
    void setActiveDownloadPath(QString const& _remotePath);

    /**
     * @brief Checks whether a download is active.
     * @return true when an active download path is recorded; otherwise false.
     */
    [[nodiscard]] bool hasActiveDownload() const noexcept;

    /**
     * @brief Checks whether the specified remote file is being downloaded.
     * @param _remotePath Remote file path to inspect.
     * @return true when the file is the active download; otherwise false.
     */
    [[nodiscard]] bool isFileDownloading(QString const& _remotePath) const noexcept;

    /** @brief Invalidates endpoint-specific state after host or port changes. */
    void handleEndpointChanged();

    /** @brief Clears the displayed remote drives and files. */
    void clearRemoteView();

    /** @brief Opens and activates the remote watch window. */
    void showWatchWindow();

    /**
     * @brief Returns the selected remote file path.
     * @return Selected remote path, or an empty string when unavailable.
     */
    [[nodiscard]] QString currentSelectedFilePath() const;

    /**
     * @brief Returns the selected remote file name.
     * @return Selected file name, or an empty string when unavailable.
     */
    [[nodiscard]] QString currentSelectedFileName() const;

    /** @brief Requests opening the selected remote file. */
    void openSelectedFile();

    /** @brief Downloads the selected remote file. */
    void downloadSelectedFile();

    /** @brief Requests deletion of the selected remote item. */
    void deleteSelectedFile();

    /** @brief Connects UI actions to client operations. */
    void wireSignals();

    /**
     * @brief Displays a cached directory or requests fresh contents.
     * @param _item Tree item containing the remote directory path.
     * @param _forceRefresh Whether cached contents must be ignored.
     */
    void requestSelectedDirectory(QTreeWidgetItem* _item, bool _forceRefresh = false);

    /**
     * @brief Displays the files contained in one directory listing.
     * @param _path Remote directory path.
     * @param _entries Cached or newly received directory entries.
     */
    void displayDirectoryFiles(QString const& _path,
                               QList<remote_control::FileEntry> const& _entries);

    /**
     * @brief Populates the tree with remote drive roots.
     * @param _drives Remote drive identifiers.
     */
    void populateDriveTree(QStringList const& _drives);

    /**
     * @brief Displays a remote directory listing.
     * @param _path Listed remote directory path.
     * @param _entries Entries returned for the directory.
     */
    void updateDirectoryView(QString const& _path,
                             QList<remote_control::FileEntry> const& _entries);

    /**
     * @brief Finds a tree item by its remote path.
     * @param _path Remote path to locate.
     * @return Matching tree item, or nullptr when not found.
     */
    [[nodiscard]] QTreeWidgetItem* findItemByPath(QString const& _path) const;

    /**
     * @brief Normalizes a remote drive root.
     * @param _drive Remote drive identifier.
     * @return Normalized drive root.
     */
    [[nodiscard]] static QString normalizeDrive(QString const& _drive);

    /**
     * @brief Joins a remote directory and file name.
     * @param _basePath Remote parent directory.
     * @param _fileName Child file or directory name.
     * @return Combined remote path.
     */
    [[nodiscard]] static QString joinPath(QString const& _basePath, QString const& _fileName);

    RemoteClient* m_client{nullptr};       ///< Client facade shared by all window operations.
    WatchWindow* m_watchWindow{nullptr};   ///< Lazily created remote-screen dialog.
    std::unique_ptr<Ui::MainWindow> m_ui;  ///< Generated main-window user interface.
    QProgressDialog* m_downloadProgress{nullptr};  ///< Progress dialog for the active download.
    QString m_activeDownloadPath;                  ///< Remote path of the active download.
    bool m_connectionVerified{false};              ///< Whether the current endpoint was tested.
    bool m_connectionTestPending{false};           ///< Whether a connection test is outstanding.
    bool m_driveListPending{false};                ///< Whether a drive-list request is outstanding.
    bool m_fileCommandPending{false};              ///< Whether a file command is outstanding.
};
