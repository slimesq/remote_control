#include "MainWindow.h"

#include "RemoteClient.h"
#include "WatchWindow.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {

constexpr int PathRole = Qt::UserRole;
constexpr int LoadedRole = Qt::UserRole + 1;

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_client(new RemoteClient(this))
{
    buildUi();
    wireSignals();
    m_client->setEndpoint(QStringLiteral("127.0.0.1"), 9527);
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("远程控制客户端 - Qt"));
    resize(980, 640);

    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    auto* rootLayout = new QVBoxLayout(m_centralWidget);

    auto* topLayout = new QHBoxLayout;
    rootLayout->addLayout(topLayout);

    topLayout->addWidget(new QLabel(tr("地址："), this));
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), this);
    topLayout->addWidget(m_hostEdit);

    topLayout->addWidget(new QLabel(tr("端口："), this));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(9527);
    topLayout->addWidget(m_portSpin);

    m_testButton = new QPushButton(tr("连接测试"), this);
    m_refreshButton = new QPushButton(tr("查看文件信息"), this);
    m_watchButton = new QPushButton(tr("远程监控"), this);
    topLayout->addWidget(m_testButton);
    topLayout->addWidget(m_refreshButton);
    topLayout->addWidget(m_watchButton);
    topLayout->addStretch();

    auto* splitter = new QSplitter(this);
    rootLayout->addWidget(splitter, 1);

    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderLabel(tr("目录"));
    splitter->addWidget(m_treeWidget);

    m_fileList = new QListWidget(this);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    splitter->addWidget(m_fileList);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    m_downloadProgress = new QProgressDialog(tr("正在下载..."), QString(), 0, 100, this);
    m_downloadProgress->setAutoClose(false);
    m_downloadProgress->setAutoReset(false);
    m_downloadProgress->hide();

    statusBar()->showMessage(tr("准备就绪"));
}

void MainWindow::wireSignals()
{
    connect(m_hostEdit, &QLineEdit::textChanged, this, [this] {
        m_client->setEndpoint(m_hostEdit->text().trimmed(), static_cast<quint16>(m_portSpin->value()));
    });
    connect(m_portSpin, &QSpinBox::valueChanged, this, [this](int) {
        m_client->setEndpoint(m_hostEdit->text().trimmed(), static_cast<quint16>(m_portSpin->value()));
    });

    connect(m_testButton, &QPushButton::clicked, m_client, &RemoteClient::testConnection);
    connect(m_refreshButton, &QPushButton::clicked, m_client, &RemoteClient::requestDrives);
    connect(m_watchButton, &QPushButton::clicked, this, [this] {
        if (!m_watchWindow) {
            m_watchWindow = new WatchWindow(m_client, this);
        }
        m_watchWindow->show();
        m_watchWindow->raise();
        m_watchWindow->activateWindow();
    });

    connect(m_treeWidget, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        requestSelectedDirectory(item);
    });
    connect(m_treeWidget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (!item->data(0, LoadedRole).toBool()) {
            requestSelectedDirectory(item);
        }
    });

    connect(m_client, &RemoteClient::connectionTested, this, [this](bool success, const QString& message) {
        statusBar()->showMessage(message, 3000);
        QMessageBox::information(this, success ? tr("连接成功") : tr("连接失败"), message);
    });
    connect(m_client, &RemoteClient::drivesListed, this, &MainWindow::populateDriveTree);
    connect(m_client, &RemoteClient::directoryListed, this, &MainWindow::updateDirectoryView);
    connect(m_client, &RemoteClient::requestFailed, this, [this](remoteqt::Command, const QString&, const QString& message) {
        statusBar()->showMessage(message, 5000);
        QMessageBox::warning(this, tr("操作失败"), message);
    });
    connect(m_client, &RemoteClient::commandCompleted, this, [this](remoteqt::Command command, const QString& context, const QString& message) {
        statusBar()->showMessage(message, 3000);
        if (command == remoteqt::Command::DeleteFile) {
            if (auto* item = m_treeWidget->currentItem()) {
                requestSelectedDirectory(item);
            }
        } else if (command == remoteqt::Command::RunFile) {
            QMessageBox::information(this, tr("打开文件"), tr("已请求打开：%1").arg(context));
        }
    });
    connect(m_client, &RemoteClient::downloadProgress, this, [this](const QString&, qint64 received, qint64 total) {
        m_downloadProgress->setMaximum(static_cast<int>(qMax<qint64>(1, total)));
        m_downloadProgress->setValue(static_cast<int>(received));
    });
    connect(m_client, &RemoteClient::downloadFinished, this, [this](const QString&, const QString& localPath, bool success, const QString& message) {
        m_downloadProgress->hide();
        if (success) {
            QMessageBox::information(this, tr("下载完成"), tr("%1\n%2").arg(message, localPath));
        } else {
            QMessageBox::warning(this, tr("下载失败"), message);
        }
    });

    connect(m_fileList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = m_fileList->itemAt(pos);
        if (!item) {
            return;
        }
        const QString filePath = item->data(Qt::UserRole).toString();
        QMenu menu(this);
        QAction* downloadAction = menu.addAction(tr("下载文件"));
        QAction* deleteAction = menu.addAction(tr("删除文件"));
        QAction* runAction = menu.addAction(tr("打开文件"));
        QAction* chosen = menu.exec(m_fileList->viewport()->mapToGlobal(pos));
        if (chosen == downloadAction) {
            const QString savePath = QFileDialog::getSaveFileName(this, tr("保存文件"), item->text());
            if (!savePath.isEmpty()) {
                m_downloadProgress->setLabelText(tr("正在下载：%1").arg(filePath));
                m_downloadProgress->setValue(0);
                m_downloadProgress->show();
                m_client->downloadFile(filePath, savePath);
            }
        } else if (chosen == deleteAction) {
            m_client->deleteFile(filePath);
        } else if (chosen == runAction) {
            m_client->runFile(filePath);
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
    statusBar()->showMessage(tr("正在加载：%1").arg(path));
    m_client->requestDirectory(path);
}

void MainWindow::populateDriveTree(const QStringList& drives)
{
    m_treeWidget->clear();
    for (const QString& drive : drives) {
        const QString normalized = normalizeDrive(drive);
        auto* item = new QTreeWidgetItem(m_treeWidget);
        item->setText(0, normalized);
        item->setData(0, PathRole, normalized + '\\');
        item->setData(0, LoadedRole, false);
        new QTreeWidgetItem(item, QStringList { tr("加载中...") });
    }
    statusBar()->showMessage(tr("已加载磁盘列表"), 3000);
}

void MainWindow::updateDirectoryView(const QString& path, const QList<remoteqt::FileEntry>& entries)
{
    auto* item = findItemByPath(path);
    if (!item) {
        return;
    }

    item->takeChildren();
    item->setData(0, LoadedRole, true);
    m_fileList->clear();

    for (const remoteqt::FileEntry& entry : entries) {
        if (entry.fileName == "." || entry.fileName == "..") {
            continue;
        }
        if (entry.isDirectory) {
            auto* child = new QTreeWidgetItem(item);
            child->setText(0, entry.fileName);
            child->setData(0, PathRole, joinPath(path, entry.fileName));
            child->setData(0, LoadedRole, false);
            new QTreeWidgetItem(child, QStringList { tr("加载中...") });
        } else {
            auto* fileItem = new QListWidgetItem(entry.fileName, m_fileList);
            fileItem->setData(Qt::UserRole, joinPath(path, entry.fileName));
        }
    }
    item->setExpanded(true);
    statusBar()->showMessage(tr("目录已更新：%1").arg(path), 3000);
}

QTreeWidgetItem* MainWindow::findItemByPath(const QString& path) const
{
    QTreeWidgetItemIterator it(m_treeWidget);
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
