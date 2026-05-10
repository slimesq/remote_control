#pragma once

#include "../common/Protocol.h"

#include <QMainWindow>

class QListWidget;
class QLineEdit;
class QProgressDialog;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;
class RemoteClient;
class WatchWindow;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void wireSignals();
    void requestSelectedDirectory(QTreeWidgetItem* item);
    void populateDriveTree(const QStringList& drives);
    void updateDirectoryView(const QString& path, const QList<remoteqt::FileEntry>& entries);
    QTreeWidgetItem* findItemByPath(const QString& path) const;
    static QString normalizeDrive(const QString& drive);
    static QString joinPath(const QString& basePath, const QString& fileName);

    RemoteClient* m_client = nullptr;
    WatchWindow* m_watchWindow = nullptr;
    QWidget* m_centralWidget = nullptr;
    QLineEdit* m_hostEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QPushButton* m_testButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_watchButton = nullptr;
    QTreeWidget* m_treeWidget = nullptr;
    QListWidget* m_fileList = nullptr;
    QProgressDialog* m_downloadProgress = nullptr;
};
