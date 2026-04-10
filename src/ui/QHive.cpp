#include "QHive.h"

#include <QDir>
#include <QList>
#include <QUuid>
#include <QDebug>
#include <QToolBar>
#include <QFileInfo>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>

QHive::QHive(QWidget* parent)
	: QMainWindow(parent), downloadDialog(nullptr)
{
	ui.setupUi(this);
	setWindowIcon(QIcon(":/icons/title.png"));

	addDownload = new QAction(QIcon(":/icons/addbutton.png"), tr("添加下载任务"), this);
	ui.toolBar->addAction(addDownload);
	removeTask = new QAction(QIcon(":/icons/removetask.png"), tr("清除下载任务"), this);
	ui.toolBar->addAction(removeTask);
	selectAll = new QAction(QIcon(":/icons/unfullyselected.png"), tr("全选"), this);
	ui.toolBar->addAction(selectAll);

	m_downloadListArea = new QScrollArea(this);
	m_downloadListArea->setWidgetResizable(true);
	QWidget* containerWidget = new QWidget();
	m_downloadListLayout = new QVBoxLayout(containerWidget);
	m_downloadListLayout->setAlignment(Qt::AlignTop);
	m_downloadListArea->setWidget(containerWidget);
	ui.centralWidget->layout()->addWidget(m_downloadListArea);

	httpClient = HttpClient::instance();
	initConnections();
}

QHive::~QHive() = default;

void QHive::initConnections()
{
	connect(addDownload, &QAction::triggered, this, &QHive::openDownloadDialog);
	connect(removeTask, &QAction::triggered, this, &QHive::removeTaskListItem);
	connect(selectAll, &QAction::triggered, this, &QHive::onSelectAllTriggered);

	connect(httpClient, &HttpClient::headRequestError, this,
		[this](const QString& errorString) {
			QMessageBox::critical(this, tr("请求错误"), tr("错误信息:\n%1").arg(errorString));
		});
	connect(httpClient, &HttpClient::headRequestSuccess, this, &QHive::createDownloadTask);

	connect(httpClient, &HttpClient::downloadFailed, this, &QHive::handleFailure);
	connect(httpClient, &HttpClient::downloadSucceeded, this, &QHive::handleSuccess);
	connect(httpClient, &HttpClient::updateDownloadProgress,
		[this](const QString& taskId, qint64 received) {
			auto* taskWidget = m_activeDownloads.value(taskId);
			if (taskWidget) {
				taskWidget->updateProgress(received);
			}
			else {
				qWarning() << "[QHive] 收到下载进度更新，但不存在对应的任务项，ID：" << taskId;
			}
		});
}

void QHive::openDownloadDialog()
{
	if (downloadDialog) {
		downloadDialog->raise();
		downloadDialog->activateWindow();
		return;
	}
	else {
		downloadDialog = new DownloadDialog(this);
		downloadDialog->setAttribute(Qt::WA_DeleteOnClose);
		connect(downloadDialog, &QObject::destroyed, this, [this]() {
			downloadDialog = nullptr;
			});
		connect(downloadDialog, &DownloadDialog::startDownload, this,
			[this](const QString& taskId, const QUrl& url, const QString& initialFilename, const QString& saveDir) {
				httpClient->headRequest(taskId, url, initialFilename, saveDir);
			});
		downloadDialog->show();
	}
}

void QHive::removeTaskListItem()
{
	QList<QString> toDelete;
	QList<QString> downloadingTasks;
	for (auto it = m_activeDownloads.begin(); it != m_activeDownloads.end(); ++it) {
		if (it.value()->isChecked()) {
			toDelete.append(it.key());
			if (it.value()->isDownloading()) {
				downloadingTasks.append(it.key());
			}
		}
	}
	if (toDelete.isEmpty()) {
		QMessageBox::information(this, tr("没有选中项"), tr("请先选择要清除的下载任务。"));
		return;
	}

	// 如果有正在下载的任务，弹出确认框
	if (!downloadingTasks.isEmpty()) {
		int count = downloadingTasks.size();
		QMessageBox::StandardButton reply = QMessageBox::question(
			this,
			tr("确认删除"),
			tr("当前所选任务列表中还有 %1 个任务正在进行中，确定要删除这些任务吗？\n删除后下载将被取消。").arg(count),
			QMessageBox::Yes | QMessageBox::No,
			QMessageBox::Yes
		);
		if (reply != QMessageBox::Yes) {
			return;
		}
	}

	// 执行删除
	for (const QString& taskId : toDelete) {
		DownloadTaskItemWidget* taskWidget = m_activeDownloads.value(taskId);
		if (!m_taskResults.value(taskId, false)) {
			httpClient->cleanTaskResources(taskId);
		}
		m_selectedCount = qMax(0, m_selectedCount - 1);

		m_downloadListLayout->removeWidget(taskWidget);
		m_activeDownloads.remove(taskId);
		m_taskResults.remove(taskId);
		taskWidget->deleteLater();
	}
	updateSelectAllAction();
}

void QHive::createDownloadTask(const QString& taskId,
	const QUrl& taskUrl,
	const QString& fileName,
	const QString& fullSavePath,
	qint64 totalSize)
{
	auto* taskWidget = new DownloadTaskItemWidget(taskId, taskUrl, fileName, fullSavePath, totalSize, this);
	m_downloadListLayout->addWidget(taskWidget);
	m_activeDownloads.insert(taskId, taskWidget);
	m_taskResults.insert(taskId, false);

	connect(taskWidget, &DownloadTaskItemWidget::requestRetry, this, &QHive::onTaskRetry);
	connect(taskWidget, &DownloadTaskItemWidget::requestCancel, this, &QHive::onTaskCancelled);

	connect(taskWidget, &DownloadTaskItemWidget::checkedStateChanged, this, &QHive::onCheckedStateChanged);
}

void QHive::handleFailure(const QString& taskId, const QString& savedPath, const QString& errorString)
{
	auto* taskWidget = m_activeDownloads.value(taskId);
	if (taskWidget) {
		taskWidget->setFailedUI();
		QMessageBox::critical(this, tr("下载失败"), tr("下载失败：\n文件：%1\n错误：%2").arg(savedPath, errorString));
	}
	else {
		qWarning() << "[QHive] 报告了下载失败，但不存在对应的任务项，ID：" << taskId;
	}
}

void QHive::handleSuccess(const QString& taskId, const QString& savedPath)
{
	auto* taskWidget = m_activeDownloads.value(taskId);
	if (taskWidget) {
		m_taskResults[taskId] = true;
		taskWidget->setSuccessUI();
		QMessageBox::information(this, tr("下载成功"), tr("文件已成功保存至：\n%1").arg(savedPath));
	}
	else {
		qWarning() << "[QHive] 报告了下载成功，但不存在对应的任务项，ID：" << taskId;
	}
}

void QHive::onTaskCancelled(const QString& taskId)
{
	if (m_activeDownloads.contains(taskId)) {
		httpClient->cancelDownload(taskId);
	}
	else {
		qWarning() << "取消失败，找不到任务项，ID：" << taskId;
	}
}

void QHive::onTaskRetry(const QString& taskId)
{
	auto* taskWidget = m_activeDownloads.value(taskId);
	if (!taskWidget) {
		qWarning() << "重试失败，找不到任务项，ID：" << taskId;
		return;
	}
	m_taskResults[taskId] = false;
	taskWidget->resetForRetry();
	httpClient->retryDownload(taskId);
}

void QHive::onSelectAllTriggered()
{
	if (m_activeDownloads.isEmpty())
		return;

	// 使用选中计数判断是否为全选状态
	bool allChecked = (m_selectedCount == m_activeDownloads.size());

	for (auto* taskWidget : m_activeDownloads) {
		if (taskWidget->isChecked() == allChecked) {
			taskWidget->setCheckState(!allChecked);
		}
	}

	// 根据切换结果更新选中计数
	if (allChecked) {
		m_selectedCount = 0;
	}
	else {
		m_selectedCount = m_activeDownloads.size();
	}

	updateSelectAllAction();
}

void QHive::updateSelectAllAction()
{
	if (m_activeDownloads.isEmpty()) {
		selectAll->setIcon(QIcon(":/icons/unfullyselected.png"));
		selectAll->setText(tr("全选"));
		return;
	}

	bool allChecked = (m_selectedCount == m_activeDownloads.size());

	if (allChecked) {
		selectAll->setIcon(QIcon(":/icons/selectall.png"));
		selectAll->setText(tr("取消全选"));
	}
	else {
		selectAll->setIcon(QIcon(":/icons/unfullyselected.png"));
		selectAll->setText(tr("全选"));
	}
}

void QHive::onCheckedStateChanged(bool checked)
{
	if (checked) {
		m_selectedCount = qMin<int>(m_selectedCount + 1, m_activeDownloads.size());
	}
	else {
		m_selectedCount = qMax(0, m_selectedCount - 1);
	}
	updateSelectAllAction();
}