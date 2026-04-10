#include "DownloadTaskItemWidget.h"

#include <QDir>
#include <QIcon>
#include <QDebug>
#include <QString>
#include <QProcess>
#include <algorithm>
#include <QFileInfo>
#include <QToolButton>
#include <QProgressBar>
#include <QAbstractButton>
#include <QDesktopServices>

DownloadTaskItemWidget::DownloadTaskItemWidget(const QString& taskId,
	const QUrl& url,
	const QString& fileName,
	const QString& savePath,
	qint64 totalSize,
	QWidget* parent)
	: QWidget(parent), m_taskId(taskId), m_taskUrl(url), m_fileName(fileName), m_savedPath(savePath), m_totalSize(totalSize)
{
	ui.setupUi(this);
	ui.cancelButton->setToolTip(tr("取消下载"));
	ui.cancelButton->setIcon(QIcon(":/icons/canceldownload.png"));
	ui.retryButton->setToolTip(tr("重试下载"));
	ui.retryButton->setIcon(QIcon(":/icons/retrydownload.png"));
	ui.openFolderButton->setToolTip(tr("打开文件所在目录"));
	ui.openFolderButton->setIcon(QIcon(":/icons/openfolder.png"));
	initUI();
	m_lastTime.start();
}

DownloadTaskItemWidget::~DownloadTaskItemWidget() = default;

static QString formatBytes(double bytes) {
	if (bytes < 1024.0) return QString("%1 B").arg(static_cast<int>(bytes));
	if (bytes < 1024.0 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
	if (bytes < 1024.0 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 2);
	return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

void DownloadTaskItemWidget::initUI()
{
	if (m_fileName.length() > 30) {
		QString display = m_fileName.left(15) + "..." + m_fileName.right(10);
		ui.fileNameLabel->setText(display);
		ui.fileNameLabel->setToolTip(m_fileName);
	}
	else {
		ui.fileNameLabel->setText(m_fileName);
	}
	ui.speedLabel->setText("0 B/s");
	ui.checkBox->setVisible(false);
	ui.checkBox->setChecked(false);
	// 为了确保复选框本身不会直接处理鼠标事件（从而绕过父控件的统一逻辑），
	// 当复选框可见时将其设置为透明接收鼠标事件，让父控件接收并处理鼠标事件。
	ui.checkBox->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_isDownloading = true;
	showRetryButton(false);
	showCancelButton(true);
	showOpenFolderButton(false);
	updateStatus("border-radius: 4px;\nbackground-color: gray;");
}

void DownloadTaskItemWidget::mousePressEvent(QMouseEvent* event)
{
	QWidget* child = childAt(event->pos());

	// 如果点击落在某个子控件上，尝试向上查找包含它的 QCheckBox
	if (child) {
		QWidget* w = child;
		while (w && !qobject_cast<QCheckBox*>(w)) {
			w = w->parentWidget();
		}
		if (auto cb = qobject_cast<QCheckBox*>(w)) {
			// 找到复选框（或其子控件），统一通过 setCheckState 处理切换，阻止复选框自身再触发一次
			bool current = isChecked();
			bool desired = !current;
			setCheckState(desired);
			event->accept();
			return;
		}

		// 点击的是其他交互控件（按钮或进度条），保持原有行为
		if (qobject_cast<QAbstractButton*>(child) || qobject_cast<QProgressBar*>(child)) {
			QWidget::mousePressEvent(event);
			return;
		}
	}

	// 点击其他区域（空白、标签等）：切换复选框状态，通过 setCheckState 统一处理
	{
		bool current = isChecked();
		bool desired = !current;
		setCheckState(desired);
		event->accept();
		return;
	}
}

void DownloadTaskItemWidget::resetForRetry()
{
	m_lastReceived = 0;
	ui.progressBar->setValue(0);
	ui.percentLabel->setText("0%");
	ui.speedLabel->setText("0 B/s");
	ui.speedLabel->setStyleSheet("");

	m_isDownloading = true;
	ui.checkBox->setVisible(false);
	showCancelButton(true);
	showRetryButton(false);
	showOpenFolderButton(false);
	updateStatus("border-radius: 4px;\nbackground-color: gray;");
	m_lastTime.restart();
}

void DownloadTaskItemWidget::updateStatus(const QString& status)
{
	ui.statusLabel->setStyleSheet(status);
}

void DownloadTaskItemWidget::updateProgress(qint64 received)
{
	qint64 delta = received - m_lastReceived;
	if (delta > 0) {
		updateSpeed(delta);
	}
	m_lastReceived = received;

	if (m_totalSize > 0) {
		int percent = static_cast<int>((static_cast<double>(received) / m_totalSize) * 100.0);
		percent = std::clamp(percent, 0, 100);
		ui.progressBar->setValue(percent);
		ui.percentLabel->setText(QString("%1%").arg(percent));
	}
	else {
		ui.progressBar->setRange(0, 0);
		ui.percentLabel->setText(tr("已下载 %1").arg(formatBytes(received)));
	}
}

void DownloadTaskItemWidget::updateSpeed(qint64 delta)
{
	qint64 elapsed = m_lastTime.restart();
	if (elapsed <= 0) return;

	double speed = delta * 1000.0 / elapsed;
	ui.speedLabel->setText(formatBytes(speed) + "/s");
}

void DownloadTaskItemWidget::showRetryButton(bool show)
{
	ui.retryButton->setVisible(show);
}

void DownloadTaskItemWidget::showCancelButton(bool show)
{
	ui.cancelButton->setVisible(show);
}

void DownloadTaskItemWidget::showOpenFolderButton(bool show)
{
	ui.openFolderButton->setVisible(show);
}

bool DownloadTaskItemWidget::isChecked()
{
	return ui.checkBox->isChecked();
}

void DownloadTaskItemWidget::setCheckState(bool newState)
{
	if (m_isDownloading) {
		ui.checkBox->setVisible(newState);
		ui.checkBox->setChecked(newState);
	}
	else {
		// 已完成或失败：复选框始终可见，保持可见并设置状态
		ui.checkBox->setChecked(newState);
	}
	emit checkedStateChanged(newState);
}

void DownloadTaskItemWidget::setFailedUI()
{
	m_lastTime.invalidate();
	updateStatus("border-radius: 4px;\nbackground-color: red;");

	m_isDownloading = false;
	if (!ui.checkBox->isVisible()) { ui.checkBox->setVisible(true); }
	if (isChecked()) { ui.checkBox->setChecked(false); }

	ui.speedLabel->setText(tr("下载失败"));
	ui.speedLabel->setStyleSheet("color: red;");
	showRetryButton(true);
	showCancelButton(false);
	showOpenFolderButton(true);
	if (m_totalSize <= 0) {
		ui.progressBar->setRange(0, 1);
		ui.progressBar->setValue(0);
	}
}

void DownloadTaskItemWidget::setSuccessUI()
{
	m_lastTime.invalidate();
	updateStatus("border-radius: 4px;\nbackground-color: green;");

	m_isDownloading = false;
	if (!ui.checkBox->isVisible()) { ui.checkBox->setVisible(true); }
	if (isChecked()) { ui.checkBox->setChecked(false); }

	ui.speedLabel->setText(tr("下载完成"));
	ui.speedLabel->setStyleSheet("color: green;");
	showRetryButton(false);
	showCancelButton(false);
	showOpenFolderButton(true);
	if (m_totalSize <= 0) {
		ui.progressBar->setRange(0, 1);
		ui.progressBar->setValue(1);
	}
}

void DownloadTaskItemWidget::on_cancelButton_clicked()
{
	emit requestCancel(m_taskId);
}

void DownloadTaskItemWidget::on_retryButton_clicked()
{
	emit requestRetry(m_taskId);
}

static void showInFolder(const QString& filePath)
{
	if (filePath.isEmpty()) {
		QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::homePath()));
		return;
	}

	QFileInfo info(filePath);
	const bool exists = info.exists();
	QString targetDir;

	if (exists) {
		if (info.isDir()) {
			targetDir = info.absoluteFilePath();
		}
		else {
			targetDir = info.absolutePath();
		}
	}
	else {
		const QString absPath = info.absolutePath();
		if (!absPath.isEmpty() && QDir(absPath).exists()) {
			targetDir = absPath;
		}
		else {
			targetDir = QDir::homePath();
		}
	}

#if defined(Q_OS_WIN)
	if (exists && info.isFile()) {
		const QString nativePath = QDir::toNativeSeparators(info.absoluteFilePath());
		QStringList args;
		args << QStringLiteral("/select,%1").arg(nativePath);
		QProcess::startDetached(QStringLiteral("explorer"), args);
	}
	else {
		const QString nativeDir = QDir::toNativeSeparators(targetDir);
		QProcess::startDetached(QStringLiteral("explorer"), QStringList() << nativeDir);
	}
#elif defined(Q_OS_MAC)
	if (exists && info.isFile()) {
		QProcess::startDetached(QStringLiteral("open"), QStringList() << QStringLiteral("-R") << info.absoluteFilePath());
	}
	else {
		QProcess::startDetached(QStringLiteral("open"), QStringList() << targetDir);
	}
#else
	QDesktopServices::openUrl(QUrl::fromLocalFile(targetDir));
#endif
}

void DownloadTaskItemWidget::on_openFolderButton_clicked()
{
	showInFolder(m_savedPath);
}