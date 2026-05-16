#include "MainWindow.h"

#include "RemoteClient.h"
#include "WatchWindow.h"
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

namespace {

constexpr int PathRole = Qt::UserRole;
constexpr int LoadedRole = Qt::UserRole + 1;

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_client(new RemoteClient(this))
    , m_ui(std::make_unique<Ui::MainWindow>())
{
    m_ui->setupUi(this);
    m_ui->mainSplitter->setStretchFactor(0, 3);
    m_ui->mainSplitter->setStretchFactor(1, 2);
    m_ui->treeWidget->header()->setStretchLastSection(true);
    m_ui->treeWidget->header()->setMinimumSectionSize(140);
    m_ui->fileTable->horizontalHeader()->setStretchLastSection(true);
    m_ui->fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_ui->fileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    statusBar()->showMessage(tr("Enter a host, test the connection, and then browse the remote machine."));
    wireSignals();
    m_client->setEndpoint(m_ui->hostEdit->text().trimmed(), static_cast<quint16>(m_ui->portSpin->value()));
    updateActionState();
}

MainWindow::~MainWindow() = default;

QProgressDialog* MainWindow::ensureDownloadProgressDialog()
{
    if (!m_downloadProgress) {
        m_downloadProgress = new QProgressDialog(tr("Downloading..."), QString(), 0, 100, this);
        m_downloadProgress->setAutoClose(false);
        m_downloadProgress->setAutoReset(false);
        m_downloadProgress->setMinimumDuration(0);
        m_downloadProgress->setWindowModality(Qt::WindowModal);
        m_downloadProgress->hide();
    }

    return m_downloadProgress;
}

void MainWindow::setBusyState(bool busy, const QString& message)
{
    m_busy = busy;
    if (busy && !message.isEmpty()) {
        statusBar()->showMessage(message);
    }
    updateActionState();
}

void MainWindow::updateActionState()
{
    const bool hasEndpoint = !m_ui->hostEdit->text().trimmed().isEmpty() && m_ui->portSpin->value() > 0;
    const bool canBrowseRemote = hasEndpoint && m_connectionVerified;
    const bool hasSelectedFile = !currentSelectedFilePath().isEmpty();

    m_ui->testButton->setEnabled(hasEndpoint && !m_busy);
    m_ui->refreshButton->setEnabled(canBrowseRemote && !m_busy);
    m_ui->watchButton->setEnabled(canBrowseRemote && !m_busy);
    m_ui->treeWidget->setEnabled(canBrowseRemote && !m_busy);
    m_ui->fileTable->setEnabled(canBrowseRemote && m_ui->fileTable->rowCount() > 0 && !m_busy);
    m_ui->openFileButton->setEnabled(canBrowseRemote && hasSelectedFile && !m_busy);
    m_ui->downloadFileButton->setEnabled(canBrowseRemote && hasSelectedFile && !m_busy);
    m_ui->deleteFileButton->setEnabled(canBrowseRemote && hasSelectedFile && !m_busy);
}

QString MainWindow::currentSelectedFilePath() const
{
    auto* item = m_ui->fileTable->currentItem();
    if (!item)
        return {};
    auto* nameItem = m_ui->fileTable->item(item->row(), 0);
    return nameItem ? nameItem->data(Qt::UserRole).toString() : QString {};
}

QString MainWindow::currentSelectedFileName() const
{
    auto* item = m_ui->fileTable->currentItem();
    if (!item)
        return {};
    auto* nameItem = m_ui->fileTable->item(item->row(), 0);
    return nameItem ? nameItem->text() : QString {};
}

void MainWindow::downloadSelectedFile()
{
    const QString filePath = currentSelectedFilePath();
    const QString fileName = currentSelectedFileName();
    if (filePath.isEmpty()) {
        return;
    }

    const QString savePath = QFileDialog::getSaveFileName(this, tr("Save File"), fileName);
    if (savePath.isEmpty()) {
        return;
    }

    auto* progressDialog = ensureDownloadProgressDialog();
    progressDialog->setLabelText(tr("Downloading: %1").arg(filePath));
    progressDialog->setValue(0);
    progressDialog->show();
    setBusyState(true, tr("Downloading: %1").arg(filePath));
    m_client->downloadFile(filePath, savePath);
}

void MainWindow::deleteSelectedFile()
{
    const QString filePath = currentSelectedFilePath();
    if (filePath.isEmpty()) {
        return;
    }
    setBusyState(true, tr("Deleting: %1").arg(filePath));
    m_client->deleteFile(filePath);
}

void MainWindow::clearRemoteView()
{
    m_ui->treeWidget->clear();
    m_ui->fileTable->setRowCount(0);
    updateActionState();
}

void MainWindow::showWatchWindow()
{
    if (!m_watchWindow) {
        m_watchWindow = new WatchWindow(m_client, this);
    }
    m_watchWindow->show();
    m_watchWindow->raise();
    m_watchWindow->activateWindow();
}

void MainWindow::openSelectedFile()
{
    const QString filePath = currentSelectedFilePath();
    if (filePath.isEmpty()) {
        return;
    }
    setBusyState(true, tr("Opening: %1").arg(filePath));
    m_client->runFile(filePath);
}

void MainWindow::wireSignals()
{
    connect(m_ui->hostEdit, &QLineEdit::textChanged, this, [this] {
        m_connectionVerified = false;
        m_client->setEndpoint(m_ui->hostEdit->text().trimmed(), static_cast<quint16>(m_ui->portSpin->value()));
        clearRemoteView();
        statusBar()->showMessage(tr("Connection settings changed. Test the connection again."));
    });
    connect(m_ui->portSpin, &QSpinBox::valueChanged, this, [this](int) {
        m_connectionVerified = false;
        m_client->setEndpoint(m_ui->hostEdit->text().trimmed(), static_cast<quint16>(m_ui->portSpin->value()));
        clearRemoteView();
        statusBar()->showMessage(tr("Connection settings changed. Test the connection again."));
    });

    connect(m_ui->testButton, &QPushButton::clicked, this, [this] {
        setBusyState(true, tr("Testing the connection..."));
        m_client->testConnection();
    });
    connect(m_ui->refreshButton, &QPushButton::clicked, this, [this] {
        setBusyState(true, tr("Loading drive list..."));
        m_client->requestDrives();
    });
    connect(m_ui->watchButton, &QPushButton::clicked, this, &MainWindow::showWatchWindow);
    connect(m_ui->openFileButton, &QPushButton::clicked, this, &MainWindow::openSelectedFile);
    connect(m_ui->downloadFileButton, &QPushButton::clicked, this, &MainWindow::downloadSelectedFile);
    connect(m_ui->deleteFileButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedFile);

    connect(m_ui->treeWidget, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        requestSelectedDirectory(item);
    });
    connect(m_ui->treeWidget, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        requestSelectedDirectory(item);
    });
    connect(m_ui->treeWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (!item->data(0, LoadedRole).toBool()) {
            requestSelectedDirectory(item);
        }
    });
    connect(m_ui->fileTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateActionState);

    connect(m_client, &RemoteClient::connectionTested, this, [this](bool success, const QString& message) {
        m_connectionVerified = success;
        if (!success) {
            clearRemoteView();
        }
        setBusyState(false);
        updateActionState();
        statusBar()->showMessage(message, 3000);
        QMessageBox::information(this, success ? tr("Connection Succeeded") : tr("Connection Failed"), message);
    });
    connect(m_client, &RemoteClient::drivesListed, this, &MainWindow::populateDriveTree);
    connect(m_client, &RemoteClient::directoryListed, this, &MainWindow::updateDirectoryView);
    connect(m_client, &RemoteClient::requestFailed, this, [this](remoteqt::Command command, const QString&, const QString& message) {
        if (command == remoteqt::Command::TestConnection || command == remoteqt::Command::DownloadFile) {
            statusBar()->showMessage(message, 5000);
            return;
        }
        setBusyState(false);
        updateActionState();
        statusBar()->showMessage(message, 5000);
        QMessageBox::warning(this, tr("Operation Failed"), message);
    });
    connect(m_client, &RemoteClient::commandCompleted, this, [this](remoteqt::Command command, const QString& context, const QString& message) {
        setBusyState(false);
        statusBar()->showMessage(message, 3000);
        if (command == remoteqt::Command::DeleteFile) {
            if (auto* item = m_ui->treeWidget->currentItem()) {
                requestSelectedDirectory(item);
            }
        } else if (command == remoteqt::Command::RunFile) {
            QMessageBox::information(this, tr("Open File"), tr("Open request sent: %1").arg(context));
        }
    });
    connect(m_client, &RemoteClient::downloadProgress, this, [this](const QString&, qint64 received, qint64 total) {
        if (!m_downloadProgress) {
            return;
        }
        m_downloadProgress->setMaximum(static_cast<int>(qMax<qint64>(1, total)));
        m_downloadProgress->setValue(static_cast<int>(received));
    });
    connect(m_client, &RemoteClient::downloadFinished, this, [this](const QString&, const QString& localPath, bool success, const QString& message) {
        setBusyState(false);
        if (m_downloadProgress) {
            m_downloadProgress->hide();
        }
        if (success) {
            QMessageBox::information(this, tr("Download Completed"), tr("%1\n%2").arg(message, localPath));
        } else {
            QMessageBox::warning(this, tr("Download Failed"), message);
        }
    });

    connect(m_ui->fileTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem*) {
        openSelectedFile();
    });
    connect(m_ui->fileTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = m_ui->fileTable->itemAt(pos);
        if (!item) {
            return;
        }
        m_ui->fileTable->setCurrentItem(item);
        QMenu menu(this);
        QAction* downloadAction = menu.addAction(tr("Download File"));
        QAction* deleteAction = menu.addAction(tr("Delete File"));
        QAction* runAction = menu.addAction(tr("Open File"));
        QAction* chosen = menu.exec(m_ui->fileTable->viewport()->mapToGlobal(pos));
        if (chosen == downloadAction) {
            downloadSelectedFile();
        } else if (chosen == deleteAction) {
            deleteSelectedFile();
        } else if (chosen == runAction) {
            openSelectedFile();
        }
    });
}

void MainWindow::requestSelectedDirectory(QTreeWidgetItem* item)
{
    if (!item) {
        return;
    }
    const QString path = item->data(0, PathRole).toString();
    if (path.isEmpty()) {
        return;
    }
    setBusyState(true, tr("Loading: %1").arg(path));
    m_client->requestDirectory(path);
}

void MainWindow::populateDriveTree(const QStringList& drives)
{
    setBusyState(false);
    m_ui->treeWidget->clear();
    m_ui->fileTable->setRowCount(0);
    QTreeWidgetItem* firstDriveItem = nullptr;
    for (const QString& drive : drives) {
        const QString normalized = normalizeDrive(drive);
        auto* item = new QTreeWidgetItem(m_ui->treeWidget);
        item->setText(0, normalized);
        item->setData(0, PathRole, normalized + '\\');
        item->setData(0, LoadedRole, false);
        new QTreeWidgetItem(item, QStringList { tr("Loading...") });
        if (!firstDriveItem) {
            firstDriveItem = item;
        }
    }
    updateActionState();
    if (!firstDriveItem) {
        statusBar()->showMessage(tr("No drives were reported by the remote host."), 5000);
        return;
    }

    m_ui->treeWidget->setCurrentItem(firstDriveItem);
    requestSelectedDirectory(firstDriveItem);
}

void MainWindow::updateDirectoryView(const QString& path, const QList<remoteqt::FileEntry>& entries)
{
    setBusyState(false);
    auto* item = findItemByPath(path);
    if (!item) {
        return;
    }

    item->takeChildren();
    item->setData(0, LoadedRole, true);
    m_ui->fileTable->setRowCount(0);

    for (const remoteqt::FileEntry& entry : entries) {
        if (entry.fileName == "." || entry.fileName == "..") {
            continue;
        }
        if (entry.isDirectory) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(0, entry.fileName);
            child->setData(0, PathRole, joinPath(path, entry.fileName));
            child->setData(0, LoadedRole, false);
            new QTreeWidgetItem(child, QStringList { tr("Loading...") });
        } else {
            const int row = m_ui->fileTable->rowCount();
            m_ui->fileTable->insertRow(row);

            auto* nameItem = new QTableWidgetItem(entry.fileName);
            nameItem->setData(Qt::UserRole, joinPath(path, entry.fileName));
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            m_ui->fileTable->setItem(row, 0, nameItem);

            auto* typeItem = new QTableWidgetItem(tr("File"));
            typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
            m_ui->fileTable->setItem(row, 1, typeItem);
        }
    }
    item->setExpanded(true);
    updateActionState();
    statusBar()->showMessage(tr("Directory updated: %1").arg(path), 3000);
}

QTreeWidgetItem* MainWindow::findItemByPath(const QString& path) const
{
    QTreeWidgetItemIterator it(m_ui->treeWidget);
    while (*it) {
        if ((*it)->data(0, PathRole).toString().compare(path, Qt::CaseInsensitive) == 0) {
            return *it;
        }
        ++it;
    }
    return nullptr;
}

QString MainWindow::normalizeDrive(const QString& drive)
{
    QString value = drive.trimmed();
    if (!value.endsWith(':')) {
        value.append(':');
    }
    return value;
}

QString MainWindow::joinPath(const QString& basePath, const QString& fileName)
{
    QString path = basePath;
    if (!path.endsWith('\\') && !path.endsWith('/')) {
        path.append('\\');
    }
    return path + fileName;
}
