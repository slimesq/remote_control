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

constexpr int PathRole{Qt::UserRole};
constexpr int LoadedRole{Qt::UserRole + 1};
constexpr int DirectoryEntriesRole{Qt::UserRole + 2};
constexpr int LoadingRole{Qt::UserRole + 3};
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
        this->m_downloadProgress->setWindowModality(Qt::WindowModal);
        this->m_downloadProgress->hide();
    }

    return this->m_downloadProgress;
}

void MainWindow::setBusyState(bool _busy, QString const& _message)
{
    this->m_busy = _busy;
    if (_busy && !_message.isEmpty())
    {
        statusBar()->showMessage(_message);
    }
    this->updateActionState();
}

void MainWindow::updateActionState()
{
    bool const hasEndpoint{!this->m_ui->hostEdit->text().trimmed().isEmpty() &&
                           this->m_ui->portSpin->value() > 0};
    bool const canBrowseRemote{hasEndpoint && this->m_connectionVerified};
    bool const hasSelectedFile{!this->currentSelectedFilePath().isEmpty()};

    this->m_ui->testButton->setEnabled(hasEndpoint && !this->m_busy);
    this->m_ui->refreshButton->setEnabled(canBrowseRemote && !this->m_busy);
    this->m_ui->watchButton->setEnabled(canBrowseRemote && !this->m_busy);
    this->m_ui->treeWidget->setEnabled(canBrowseRemote && !this->m_busy);
    this->m_ui->fileTable->setEnabled(canBrowseRemote && this->m_ui->fileTable->rowCount() > 0 &&
                                      !this->m_busy);
    this->m_ui->openFileButton->setEnabled(canBrowseRemote && hasSelectedFile && !this->m_busy);
    this->m_ui->downloadFileButton->setEnabled(canBrowseRemote && hasSelectedFile && !this->m_busy);
    this->m_ui->deleteFileButton->setEnabled(canBrowseRemote && hasSelectedFile && !this->m_busy);
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
    this->setBusyState(true, tr("Downloading: %1").arg(filePath));
    this->m_client->downloadFile(filePath, savePath);
}

void MainWindow::deleteSelectedFile()
{
    QString const filePath{this->currentSelectedFilePath()};
    if (filePath.isEmpty())
    {
        return;
    }
    this->setBusyState(true, tr("Deleting: %1").arg(filePath));
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
    QString const filePath{this->currentSelectedFilePath()};
    if (filePath.isEmpty())
    {
        return;
    }
    this->setBusyState(true, tr("Opening: %1").arg(filePath));
    this->m_client->runFile(filePath);
}

void MainWindow::wireSignals()
{
    connect(this->m_ui->hostEdit, &QLineEdit::textChanged, this, [this] {
        this->m_connectionVerified = false;
        this->m_client->setEndpoint(this->m_ui->hostEdit->text().trimmed(),
                                    static_cast<quint16>(this->m_ui->portSpin->value()));
        this->clearRemoteView();
        statusBar()->showMessage(tr("Connection settings changed. Test the connection again."));
    });
    connect(this->m_ui->portSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
        this->m_connectionVerified = false;
        this->m_client->setEndpoint(this->m_ui->hostEdit->text().trimmed(),
                                    static_cast<quint16>(this->m_ui->portSpin->value()));
        this->clearRemoteView();
        statusBar()->showMessage(tr("Connection settings changed. Test the connection again."));
    });

    connect(this->m_ui->testButton, &QPushButton::clicked, this, [this] {
        this->setBusyState(true, tr("Testing the connection..."));
        this->m_client->testConnection();
    });
    connect(this->m_ui->refreshButton, &QPushButton::clicked, this, [this] {
        this->setBusyState(true, tr("Loading drive list..."));
        this->m_client->requestDrives();
    });
    connect(this->m_ui->watchButton, &QPushButton::clicked, this, &MainWindow::showWatchWindow);
    connect(this->m_ui->openFileButton, &QPushButton::clicked, this, &MainWindow::openSelectedFile);
    connect(this->m_ui->downloadFileButton,
            &QPushButton::clicked,
            this,
            &MainWindow::downloadSelectedFile);
    connect(
        this->m_ui->deleteFileButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedFile);

    connect(
        this->m_ui->treeWidget, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* _item) {
            this->requestSelectedDirectory(_item);
        });
    connect(
        this->m_ui->treeWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* _item) {
            if (!_item->data(0, LoadedRole).toBool())
            {
                this->requestSelectedDirectory(_item);
            }
        });
    connect(this->m_ui->fileTable,
            &QTableWidget::itemSelectionChanged,
            this,
            &MainWindow::updateActionState);

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
            this->setBusyState(false);
            this->updateActionState();
            statusBar()->showMessage(_message, ShortStatusMessageDurationMs);
            QMessageBox::information(
                this, _success ? tr("Connection Succeeded") : tr("Connection Failed"), _message);
        });
    connect(this->m_client, &RemoteClient::drivesListed, this, &MainWindow::populateDriveTree);
    connect(this->m_client, &RemoteClient::directoryListed, this, &MainWindow::updateDirectoryView);
    connect(
        this->m_client,
        &RemoteClient::requestFailed,
        this,
        [this](remote_control::Command _command, QString const& _context, QString const& _message) {
            if (_command == remote_control::Command::ListDirectory)
            {
                if (auto* const item{this->findItemByPath(_context)})
                {
                    item->setData(0, LoadingRole, false);
                }
            }
            if (_command == remote_control::Command::TestConnection ||
                _command == remote_control::Command::DownloadFile)
            {
                statusBar()->showMessage(_message, LongStatusMessageDurationMs);
                return;
            }
            if (_command == remote_control::Command::WatchScreen ||
                _command == remote_control::Command::MouseEvent ||
                _command == remote_control::Command::LockMachine ||
                _command == remote_control::Command::UnlockMachine)
            {
                statusBar()->showMessage(_message, LongStatusMessageDurationMs);
                return;
            }
            this->setBusyState(false);
            this->updateActionState();
            statusBar()->showMessage(_message, LongStatusMessageDurationMs);
            QMessageBox::warning(this, tr("Operation Failed"), _message);
        });
    connect(
        this->m_client,
        &RemoteClient::commandCompleted,
        this,
        [this](remote_control::Command _command, QString const& _context, QString const& _message) {
            if (_command == remote_control::Command::MouseEvent)
            {
                return;
            }
            if (_command == remote_control::Command::DeleteFile ||
                _command == remote_control::Command::RunFile)
            {
                this->setBusyState(false);
            }
            statusBar()->showMessage(_message, ShortStatusMessageDurationMs);
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
                    this, tr("Open File"), tr("Open request sent: %1").arg(_context));
            }
        });
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
            this->setBusyState(false);
            if (this->m_downloadProgress)
            {
                this->m_downloadProgress->hide();
            }
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

    connect(
        this->m_ui->fileTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
            this->openSelectedFile();
        });
    connect(
        this->m_ui->fileTable,
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
            QAction* const chosen{menu.exec(this->m_ui->fileTable->viewport()->mapToGlobal(_pos))};
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

    if (!_forceRefresh && _item->data(0, LoadedRole).toBool())
    {
        this->displayDirectoryFiles(
            path, _item->data(0, DirectoryEntriesRole).value<QList<remote_control::FileEntry>>());
        statusBar()->showMessage(tr("Directory loaded from cache: %1").arg(path),
                                 ShortStatusMessageDurationMs);
        return;
    }

    if (_item->data(0, LoadingRole).toBool())
    {
        return;
    }

    _item->setData(0, LoadingRole, true);
    this->setBusyState(true, tr("Loading: %1").arg(path));
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
    this->setBusyState(false);
    this->m_ui->treeWidget->clear();
    this->m_ui->fileTable->setRowCount(0);
    QTreeWidgetItem* firstDriveItem{nullptr};
    for (QString const& drive : _drives)
    {
        QString const normalized{this->normalizeDrive(drive)};
        auto* const item{new QTreeWidgetItem{this->m_ui->treeWidget}};
        item->setText(0, normalized);
        item->setData(0, PathRole, normalized + '\\');
        item->setData(0, LoadedRole, false);
        item->setData(0, LoadingRole, false);
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
    this->requestSelectedDirectory(firstDriveItem);
}

void MainWindow::updateDirectoryView(QString const& _path,
                                     QList<remote_control::FileEntry> const& _entries)
{
    this->setBusyState(false);
    auto* const item{this->findItemByPath(_path)};
    if (!item)
    {
        return;
    }

    // Fresh server data replaces both the placeholder and any stale child nodes.
    QList<QTreeWidgetItem*> const previousChildren{item->takeChildren()};
    qDeleteAll(previousChildren);
    item->setData(0, LoadedRole, true);
    item->setData(0, LoadingRole, false);
    item->setData(0, DirectoryEntriesRole, QVariant::fromValue(_entries));

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
            child->setData(0, LoadedRole, false);
            child->setData(0, LoadingRole, false);
            new QTreeWidgetItem{child, QStringList{tr("Loading...")}};
        }
    }
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
