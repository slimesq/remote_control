#include "client/MainWindow.h"

#include "client/RemoteClient.h"
#include "client/WatchWindow.h"
#include "ui_MainWindow.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTableWidgetItem>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVariant>
#include <QtAlgorithms>

namespace
{

/** @brief Stores the complete remote path associated with a directory-tree item. */
constexpr int PathRole{Qt::UserRole};

/** @brief Stores the current directory loading state. */
constexpr int DirectoryStateRole{Qt::UserRole + 1};

/** @brief Stores the directory entries cached for a directory-tree item. */
constexpr int DirectoryEntriesRole{Qt::UserRole + 2};

/** @brief Represents the valid loading states of a remote directory item. */
enum class DirectoryLoadState
{
    Unloaded,    ///< No directory entries have been cached.
    Loading,     ///< The initial directory request is in progress.
    Loaded,      ///< Valid directory entries are available in the cache.
    Refreshing,  ///< A refresh is in progress while the existing cache remains usable.
};

/**
 * @brief Returns the loading state stored by a directory-tree item.
 * @param _item Directory-tree item to inspect.
 * @return Stored directory loading state.
 */
[[nodiscard]] DirectoryLoadState directoryLoadState(QTreeWidgetItem const* _item)
{
    return static_cast<DirectoryLoadState>(_item->data(0, DirectoryStateRole).toInt());
}

/**
 * @brief Updates the loading state stored by a directory-tree item.
 * @param _item Directory-tree item to update.
 * @param _state New directory loading state.
 */
void setDirectoryLoadState(QTreeWidgetItem* _item, DirectoryLoadState _state)
{
    _item->setData(0, DirectoryStateRole, static_cast<int>(_state));
}

/**
 * @brief Checks whether a directory state has usable cached entries.
 * @param _state Directory loading state to inspect.
 * @return true when cached entries are available; otherwise false.
 */
[[nodiscard]] constexpr bool hasDirectoryCache(DirectoryLoadState _state) noexcept
{
    return _state == DirectoryLoadState::Loaded || _state == DirectoryLoadState::Refreshing;
}

/**
 * @brief Checks whether a directory request is currently active.
 * @param _state Directory loading state to inspect.
 * @return true while loading or refreshing; otherwise false.
 */
[[nodiscard]] constexpr bool isDirectoryRequestActive(DirectoryLoadState _state) noexcept
{
    return _state == DirectoryLoadState::Loading || _state == DirectoryLoadState::Refreshing;
}

/**
 * @brief Restores the stable directory state after a failed request.
 * @param _item Directory-tree item whose request failed.
 */
void restoreDirectoryStateAfterFailure(QTreeWidgetItem* _item)
{
    DirectoryLoadState const state{directoryLoadState(_item)};
    if (state == DirectoryLoadState::Loading)
    {
        setDirectoryLoadState(_item, DirectoryLoadState::Unloaded);
    }
    else if (state == DirectoryLoadState::Refreshing)
    {
        setDirectoryLoadState(_item, DirectoryLoadState::Loaded);
    }
}

constexpr int TreeHeaderMinimumSectionSize{140};
constexpr int DownloadProgressMaximum{100};
constexpr int ShortStatusMessageDurationMs{3000};
constexpr int LongStatusMessageDurationMs{5000};

}  // namespace

MainWindow::MainWindow(QWidget* _parent)
    : QMainWindow{_parent},
      m_client{new RemoteClient{this}},
      m_ui{std::make_unique<Ui::MainWindow>()}
{
    this->m_ui->setupUi(this);
    this->m_ui->mainSplitter->setStretchFactor(0, 3);
    this->m_ui->mainSplitter->setStretchFactor(1, 2);

    auto* const treeHeader{this->m_ui->treeWidget->header()};
    treeHeader->setStretchLastSection(true);
    treeHeader->setMinimumSectionSize(TreeHeaderMinimumSectionSize);

    auto* const fileHeader{this->m_ui->fileTable->horizontalHeader()};
    fileHeader->setStretchLastSection(false);
    fileHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    fileHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    statusBar()->showMessage(
        tr("Enter a host, test the connection, and then browse the remote machine."));
    this->wireSignals();
    this->m_client->setEndpoint(this->m_ui->hostEdit->text().trimmed(),
                                static_cast<quint16>(this->m_ui->portSpin->value()));
    this->updateActionState();
}

MainWindow::~MainWindow() = default;

void MainWindow::setEndpoint(QString const& _host, quint16 _port)
{
    this->m_ui->hostEdit->setText(_host);
    this->m_ui->portSpin->setValue(_port);
    this->m_client->setEndpoint(_host.trimmed(), _port);
}

QProgressDialog* MainWindow::ensureDownloadProgressDialog()
{
    if (!this->m_downloadProgress)
    {
        this->m_downloadProgress =
            new QProgressDialog{tr("Downloading..."), QString{}, 0, DownloadProgressMaximum, this};
        this->m_downloadProgress->setAutoClose(false);
        this->m_downloadProgress->setAutoReset(false);
        this->m_downloadProgress->setMinimumDuration(0);
        this->m_downloadProgress->setWindowModality(Qt::NonModal);
        this->m_downloadProgress->hide();
    }

    return this->m_downloadProgress;
}

void MainWindow::updateActionState()
{
    bool const hasEndpoint{!this->m_ui->hostEdit->text().trimmed().isEmpty() &&
                           this->m_ui->portSpin->value() > 0};
    bool const canBrowseRemote{hasEndpoint && this->m_connectionVerified};
    QString const selectedFilePath{this->currentSelectedFilePath()};
    bool const hasSelectedFile{!selectedFilePath.isEmpty()};
    bool const hasActiveDownload{this->hasActiveDownload()};
    bool const selectedFileIsDownloading{this->isFileDownloading(selectedFilePath)};

    this->m_ui->hostEdit->setEnabled(!hasActiveDownload);
    this->m_ui->portSpin->setEnabled(!hasActiveDownload);
    this->m_ui->testButton->setEnabled(hasEndpoint && !this->m_connectionTestPending);
    this->m_ui->refreshButton->setEnabled(canBrowseRemote && !this->m_driveListPending);
    this->m_ui->watchButton->setEnabled(canBrowseRemote);
    this->m_ui->treeWidget->setEnabled(canBrowseRemote);
    this->m_ui->fileTable->setEnabled(canBrowseRemote && this->m_ui->fileTable->rowCount() > 0);
    this->m_ui->openFileButton->setEnabled(canBrowseRemote && hasSelectedFile &&
                                           !this->m_fileCommandPending);
    this->m_ui->downloadFileButton->setEnabled(canBrowseRemote && hasSelectedFile &&
                                               !hasActiveDownload);
    this->m_ui->deleteFileButton->setEnabled(canBrowseRemote && hasSelectedFile &&
                                             !this->m_fileCommandPending &&
                                             !selectedFileIsDownloading);
}

void MainWindow::setActiveDownloadPath(QString const& _remotePath)
{
    this->m_activeDownloadPath = _remotePath;
    this->updateActionState();
}

bool MainWindow::hasActiveDownload() const noexcept
{
    return !this->m_activeDownloadPath.isEmpty();
}

bool MainWindow::isFileDownloading(QString const& _remotePath) const noexcept
{
    return !_remotePath.isEmpty() && _remotePath == this->m_activeDownloadPath;
}

void MainWindow::handleEndpointChanged()
{
    this->m_connectionVerified = false;
    this->m_connectionTestPending = false;
    this->m_driveListPending = false;
    this->m_fileCommandPending = false;
    this->m_client->setEndpoint(this->m_ui->hostEdit->text().trimmed(),
                                static_cast<quint16>(this->m_ui->portSpin->value()));
    this->clearRemoteView();
    statusBar()->showMessage(tr("Connection settings changed. Test the connection again."));
}

QString MainWindow::currentSelectedFilePath() const
{
    auto* const item{this->m_ui->fileTable->currentItem()};
    if (!item)
    {
        return {};
    }
    auto* const nameItem{this->m_ui->fileTable->item(item->row(), 0)};
    return nameItem ? nameItem->data(Qt::UserRole).toString() : QString{};
}

QString MainWindow::currentSelectedFileName() const
{
    auto* const item{this->m_ui->fileTable->currentItem()};
    if (!item)
    {
        return {};
    }
    auto* const nameItem{this->m_ui->fileTable->item(item->row(), 0)};
    return nameItem ? nameItem->text() : QString{};
}

void MainWindow::downloadSelectedFile()
{
    if (this->hasActiveDownload())
    {
        return;
    }

    QString const filePath{this->currentSelectedFilePath()};
    QString const fileName{this->currentSelectedFileName()};
    if (filePath.isEmpty())
    {
        return;
    }

    QString const savePath{QFileDialog::getSaveFileName(this, tr("Save File"), fileName)};
    if (savePath.isEmpty())
    {
        return;
    }

    auto* const progressDialog{this->ensureDownloadProgressDialog()};
    progressDialog->setLabelText(tr("Downloading: %1").arg(filePath));
    progressDialog->setValue(0);
    progressDialog->show();
    this->setActiveDownloadPath(filePath);
    statusBar()->showMessage(tr("Downloading: %1").arg(filePath));
    this->m_client->downloadFile(filePath, savePath);
}

void MainWindow::deleteSelectedFile()
{
    if (this->m_fileCommandPending)
    {
        return;
    }

    QString const filePath{this->currentSelectedFilePath()};
    if (filePath.isEmpty() || this->isFileDownloading(filePath))
    {
        return;
    }
    this->m_fileCommandPending = true;
    statusBar()->showMessage(tr("Deleting: %1").arg(filePath));
    this->updateActionState();
    this->m_client->deleteFile(filePath);
}

void MainWindow::clearRemoteView()
{
    this->m_ui->treeWidget->clear();
    this->m_ui->fileTable->setRowCount(0);
    this->updateActionState();
}

void MainWindow::showWatchWindow()
{
    if (!this->m_watchWindow)
    {
        this->m_watchWindow = new WatchWindow{this->m_client, this};
    }
    this->m_watchWindow->show();
    this->m_watchWindow->raise();
    this->m_watchWindow->activateWindow();
}

void MainWindow::openSelectedFile()
{
    if (this->m_fileCommandPending)
    {
        return;
    }

    QString const filePath{this->currentSelectedFilePath()};
    if (filePath.isEmpty())
    {
        return;
    }
    this->m_fileCommandPending = true;
    statusBar()->showMessage(tr("Opening: %1").arg(filePath));
    this->updateActionState();
    this->m_client->runFile(filePath);
}

void MainWindow::wireSignals()
{
    // Endpoint: invalidates stale server state and owns the connection-test lifecycle.
    connect(
        this->m_ui->hostEdit, &QLineEdit::textChanged, this, &MainWindow::handleEndpointChanged);
    connect(this->m_ui->portSpin,
            qOverload<int>(&QSpinBox::valueChanged),
            this,
            &MainWindow::handleEndpointChanged);
    connect(this->m_ui->testButton, &QPushButton::clicked, this, [this] {
        this->m_connectionTestPending = true;
        statusBar()->showMessage(tr("Testing the connection..."));
        this->updateActionState();
        this->m_client->testConnection();
    });
    connect(
        this->m_client,
        &RemoteClient::connectionTested,
        this,
        [this](bool _success, QString const& _message) {
            this->m_connectionVerified = _success;
            if (!_success)
            {
                this->clearRemoteView();
            }
            this->m_connectionTestPending = false;
            this->updateActionState();
            statusBar()->showMessage(_message, ShortStatusMessageDurationMs);
            QMessageBox::information(
                this, _success ? tr("Connection Succeeded") : tr("Connection Failed"), _message);
        });

    // Remote browser: tracks the current directory and maintains its cached contents.
    connect(this->m_ui->refreshButton, &QPushButton::clicked, this, [this] {
        this->m_driveListPending = true;
        statusBar()->showMessage(tr("Loading drive list..."));
        this->updateActionState();
        this->m_client->requestDrives();
    });
    connect(this->m_ui->treeWidget,
            &QTreeWidget::currentItemChanged,
            this,
            [this](QTreeWidgetItem* _current, QTreeWidgetItem*) {
                // QTreeWidget updates currentItem before emitting this signal.
                this->requestSelectedDirectory(_current);
            });
    connect(
        this->m_ui->treeWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* _item) {
            if (!hasDirectoryCache(directoryLoadState(_item)))
            {
                this->requestSelectedDirectory(_item);
            }
        });
    connect(this->m_client,
            &RemoteClient::driveListFinished,
            this,
            [this](QStringList const& _drives, bool _success, QString const& _message) {
                this->m_driveListPending = false;
                if (_success)
                {
                    this->populateDriveTree(_drives);
                    return;
                }

                this->updateActionState();
                statusBar()->showMessage(_message, LongStatusMessageDurationMs);
                QMessageBox::warning(this, tr("Drive List Failed"), _message);
            });
    connect(this->m_client,
            &RemoteClient::directoryListFinished,
            this,
            [this](QString const& _path,
                   QList<remote_control::FileEntry> const& _entries,
                   bool _success,
                   QString const& _message) {
                if (_success)
                {
                    this->updateDirectoryView(_path, _entries);
                    return;
                }

                if (auto* const item{this->findItemByPath(_path)})
                {
                    restoreDirectoryStateAfterFailure(item);
                }
                statusBar()->showMessage(_message, LongStatusMessageDurationMs);
                QMessageBox::warning(this, tr("Directory Request Failed"), _message);
            });

    // File table: tracks the current file and routes alternate UI actions.
    connect(this->m_ui->fileTable,
            &QTableWidget::currentItemChanged,
            this,
            &MainWindow::updateActionState);
    connect(
        this->m_ui->fileTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
            this->openSelectedFile();
        });
    connect(this->m_ui->fileTable,
            &QTableWidget::customContextMenuRequested,
            this,
            [this](QPoint const& _pos) {
                auto* const item{this->m_ui->fileTable->itemAt(_pos)};
                if (!item)
                {
                    return;
                }
                this->m_ui->fileTable->setCurrentItem(item);
                QMenu menu{this};
                QAction* const downloadAction{menu.addAction(tr("Download File"))};
                QAction* const deleteAction{menu.addAction(tr("Delete File"))};
                QAction* const runAction{menu.addAction(tr("Open File"))};
                QString const selectedPath{this->currentSelectedFilePath()};
                downloadAction->setEnabled(!this->hasActiveDownload());
                deleteAction->setEnabled(!this->m_fileCommandPending &&
                                         !this->isFileDownloading(selectedPath));
                runAction->setEnabled(!this->m_fileCommandPending);
                QAction const* const chosen{
                    menu.exec(this->m_ui->fileTable->viewport()->mapToGlobal(_pos))};
                if (chosen == downloadAction)
                {
                    this->downloadSelectedFile();
                }
                else if (chosen == deleteAction)
                {
                    this->deleteSelectedFile();
                }
                else if (chosen == runAction)
                {
                    this->openSelectedFile();
                }
            });

    // File commands: runs or deletes a selected remote file and clears the pending state.
    connect(this->m_ui->openFileButton, &QPushButton::clicked, this, &MainWindow::openSelectedFile);
    connect(
        this->m_ui->deleteFileButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedFile);
    connect(this->m_client,
            &RemoteClient::fileCommandFinished,
            this,
            [this](remote_control::Command _command,
                   QString const& _path,
                   bool _success,
                   QString const& _message) {
                this->m_fileCommandPending = false;
                this->updateActionState();
                statusBar()->showMessage(
                    _message,
                    _success ? ShortStatusMessageDurationMs : LongStatusMessageDurationMs);
                if (!_success)
                {
                    QMessageBox::warning(this, tr("Operation Failed"), _message);
                    return;
                }

                if (_command == remote_control::Command::DeleteFile)
                {
                    if (auto* const item{this->m_ui->treeWidget->currentItem()})
                    {
                        this->requestSelectedDirectory(item, true);
                    }
                }
                else if (_command == remote_control::Command::RunFile)
                {
                    QMessageBox::information(
                        this, tr("Open File"), tr("Open request sent: %1").arg(_path));
                }
            });

    // Downloads: owns the active-download state, progress dialog, and final notification.
    connect(this->m_ui->downloadFileButton,
            &QPushButton::clicked,
            this,
            &MainWindow::downloadSelectedFile);
    connect(this->m_client,
            &RemoteClient::downloadProgress,
            this,
            [this](QString const&, qint64 _received, qint64 _total) {
                if (!this->m_downloadProgress)
                {
                    return;
                }
                int const progressValue{
                    _total > 0 ? qBound(0,
                                        static_cast<int>(static_cast<long double>(_received) /
                                                         static_cast<long double>(_total) *
                                                         DownloadProgressMaximum),
                                        DownloadProgressMaximum)
                               : 0};
                this->m_downloadProgress->setMaximum(DownloadProgressMaximum);
                this->m_downloadProgress->setValue(progressValue);
            });
    connect(
        this->m_client,
        &RemoteClient::downloadFinished,
        this,
        [this](QString const&, QString const& _localPath, bool _success, QString const& _message) {
            this->setActiveDownloadPath({});
            if (this->m_downloadProgress)
            {
                this->m_downloadProgress->hide();
            }
            statusBar()->showMessage(
                _message, _success ? ShortStatusMessageDurationMs : LongStatusMessageDurationMs);
            if (_success)
            {
                QMessageBox::information(
                    this, tr("Download Completed"), tr("%1\n%2").arg(_message, _localPath));
            }
            else
            {
                QMessageBox::warning(this, tr("Download Failed"), _message);
            }
        });

    // Monitoring and control: reports persistent screen-watch and control-channel results.
    connect(this->m_ui->watchButton, &QPushButton::clicked, this, &MainWindow::showWatchWindow);
    connect(this->m_client, &RemoteClient::watchFailed, this, [this](QString const& _message) {
        statusBar()->showMessage(_message, LongStatusMessageDurationMs);
    });
    connect(this->m_client,
            &RemoteClient::controlCommandFinished,
            this,
            [this](remote_control::Command _command,
                   QString const&,
                   bool _success,
                   QString const& _message) {
                if (_command != remote_control::Command::MouseEvent || !_success)
                {
                    statusBar()->showMessage(
                        _message,
                        _success ? ShortStatusMessageDurationMs : LongStatusMessageDurationMs);
                }
            });
}

void MainWindow::requestSelectedDirectory(QTreeWidgetItem* _item, bool _forceRefresh)
{
    if (!_item)
    {
        return;
    }
    QString const path{_item->data(0, PathRole).toString()};
    if (path.isEmpty())
    {
        return;
    }

    DirectoryLoadState const state{directoryLoadState(_item)};
    if (!_forceRefresh && hasDirectoryCache(state))
    {
        this->displayDirectoryFiles(
            path, _item->data(0, DirectoryEntriesRole).value<QList<remote_control::FileEntry>>());
        statusBar()->showMessage(tr("Directory loaded from cache: %1").arg(path),
                                 ShortStatusMessageDurationMs);
        return;
    }

    if (isDirectoryRequestActive(state))
    {
        return;
    }

    setDirectoryLoadState(_item,
                          state == DirectoryLoadState::Loaded ? DirectoryLoadState::Refreshing
                                                              : DirectoryLoadState::Loading);
    statusBar()->showMessage(tr("Loading: %1").arg(path));
    this->m_client->requestDirectory(path);
}

void MainWindow::displayDirectoryFiles(QString const& _path,
                                       QList<remote_control::FileEntry> const& _entries)
{
    this->m_ui->fileTable->setRowCount(0);
    for (remote_control::FileEntry const& entry : _entries)
    {
        if (entry.isInvalid || entry.isDirectory || entry.fileName == "." || entry.fileName == "..")
        {
            continue;
        }

        int const row{this->m_ui->fileTable->rowCount()};
        this->m_ui->fileTable->insertRow(row);

        auto* const nameItem{new QTableWidgetItem{entry.fileName}};
        nameItem->setData(Qt::UserRole, this->joinPath(_path, entry.fileName));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        this->m_ui->fileTable->setItem(row, 0, nameItem);

        auto* const typeItem{new QTableWidgetItem{tr("File")}};
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        this->m_ui->fileTable->setItem(row, 1, typeItem);
    }
    this->updateActionState();
}

void MainWindow::populateDriveTree(QStringList const& _drives)
{
    this->m_ui->treeWidget->clear();
    this->m_ui->fileTable->setRowCount(0);
    QTreeWidgetItem* firstDriveItem{nullptr};
    for (QString const& drive : _drives)
    {
        QString const normalized{this->normalizeDrive(drive)};
        auto* const item{new QTreeWidgetItem{this->m_ui->treeWidget}};
        item->setText(0, normalized);
        item->setData(0, PathRole, normalized + '\\');
        setDirectoryLoadState(item, DirectoryLoadState::Unloaded);
        new QTreeWidgetItem{item, QStringList{tr("Loading...")}};
        if (!firstDriveItem)
        {
            firstDriveItem = item;
        }
    }
    this->updateActionState();
    if (!firstDriveItem)
    {
        statusBar()->showMessage(tr("No drives were reported by the remote host."),
                                 LongStatusMessageDurationMs);
        return;
    }

    this->m_ui->treeWidget->setCurrentItem(firstDriveItem);
}

void MainWindow::updateDirectoryView(QString const& _path,
                                     QList<remote_control::FileEntry> const& _entries)
{
    auto* const item{this->findItemByPath(_path)};
    if (!item)
    {
        return;
    }

    // Fresh server data replaces both the placeholder and any stale child nodes.
    QList<QTreeWidgetItem*> const previousChildren{item->takeChildren()};
    qDeleteAll(previousChildren);

    for (remote_control::FileEntry const& entry : _entries)
    {
        if (entry.fileName == "." || entry.fileName == "..")
        {
            continue;
        }
        if (entry.isDirectory)
        {
            auto* const child{new QTreeWidgetItem{item}};
            child->setText(0, entry.fileName);
            child->setData(0, PathRole, this->joinPath(_path, entry.fileName));
            setDirectoryLoadState(child, DirectoryLoadState::Unloaded);
            new QTreeWidgetItem{child, QStringList{tr("Loading...")}};
        }
    }
    // Publish the cache before marking the item loaded so observers cannot see partial state.
    item->setData(0, DirectoryEntriesRole, QVariant::fromValue(_entries));
    setDirectoryLoadState(item, DirectoryLoadState::Loaded);
    if (this->m_ui->treeWidget->currentItem() == item)
    {
        this->displayDirectoryFiles(_path, _entries);
    }
    item->setExpanded(true);
    statusBar()->showMessage(tr("Directory updated: %1").arg(_path), ShortStatusMessageDurationMs);
}

QTreeWidgetItem* MainWindow::findItemByPath(QString const& _path) const
{
    QTreeWidgetItemIterator it{this->m_ui->treeWidget};
    while (*it)
    {
        if ((*it)->data(0, PathRole).toString().compare(_path, Qt::CaseInsensitive) == 0)
        {
            return *it;
        }
        ++it;
    }
    return nullptr;
}

QString MainWindow::normalizeDrive(QString const& _drive)
{
    QString value{_drive.trimmed()};
    if (!value.endsWith(':'))
    {
        value.append(':');
    }
    return value;
}

QString MainWindow::joinPath(QString const& _basePath, QString const& _fileName)
{
    QString path{_basePath};
    if (!path.endsWith('\\') && !path.endsWith('/'))
    {
        path.append('\\');
    }
    return path + _fileName;
}
