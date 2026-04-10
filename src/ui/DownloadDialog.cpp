#include "DownloadDialog.h"

#include <QDir>
#include <QUuid>
#include <QDebug>
#include <QFileInfo>
#include <QUrlQuery>
#include <QMessageBox>	
#include <QFileDialog>	
#include <QApplication>
#include <QStandardPaths>	

DownloadDialog::DownloadDialog(QWidget* parent)
	: QDialog(parent), clipboard(QApplication::clipboard())
{
	ui.setupUi(this);
	setWindowIcon(QIcon(":/icons/download.png"));
	ui.changeSavePathButton->setIcon(QIcon(":/icons/openfolder.png"));
	ui.urlLineEdit->setFocus();

	QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
	if (downloadDir.isEmpty()) {
		downloadDir = QDir::homePath();
	}
	ui.savePath->setText(QDir::toNativeSeparators(downloadDir) + QDir::separator());

	connect(ui.urlLineEdit, &QLineEdit::textChanged, this, &DownloadDialog::getFileNameFromUrl);

	populateUrlFromClipboard();
	connect(clipboard, &QClipboard::dataChanged, this, &DownloadDialog::populateUrlFromClipboard);
}

DownloadDialog::~DownloadDialog() = default;

void DownloadDialog::on_downloadButton_clicked()
{
	// 禁止重复点击
	ui.downloadButton->setEnabled(false);

	QString urlStr = ui.urlLineEdit->text().trimmed();
	if (urlStr.isEmpty()) {
		ui.downloadButton->setEnabled(true);
		QMessageBox::critical(this, tr("输入为空"), tr("请输入一个有效的 URL 地址!"));
		return;
	}
	ui.downloadButton->setText(tr("检测中..."));

	QUrl url(urlStr);
	if (url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty()) {
		QString taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		QString saveDir = ui.savePath->text().trimmed();
		QString inferredFilename = ui.fileName->text().trimmed();
		emit startDownload(taskId, url, inferredFilename, saveDir);
		accept();	// 下载任务已移交，关闭对话框
	}
	else {
		ui.downloadButton->setText(tr("下载"));
		ui.downloadButton->setEnabled(true);
		QMessageBox::critical(this, tr("输入错误"), tr("请输入有效的 HTTP/HTTPS 链接！"));
		return;
	}
}

void DownloadDialog::on_cancelDownload_clicked()
{
	close();
}

void DownloadDialog::on_changeSavePathButton_clicked()
{
	QString currentPath = ui.savePath->text().trimmed();
	QString selectedDir = QFileDialog::getExistingDirectory(
		this,
		tr("选择下载保存位置"),
		currentPath,
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
	);
	if (!selectedDir.isEmpty()) {
		ui.savePath->setText(QDir::toNativeSeparators(selectedDir) + QDir::separator());
	}
}

void DownloadDialog::getFileNameFromUrl()
{
	QString urlStr = ui.urlLineEdit->text().trimmed();

	QUrl url(urlStr);
	if (url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty()) {
		ui.infoLabel->setStyleSheet("");
		ui.infoLabel->setText(tr("文件名："));

		QString fileName;
		QString path = url.path();

		if (!path.isEmpty() && path != "/") {
			fileName = QFileInfo(path).fileName();
		}
		else if (url.hasQuery() && !url.query().isEmpty()) {
			QUrlQuery query(url);
			for (auto key : { "fileName", "file", "name" }) {
				if (query.hasQueryItem(key)) {
					fileName = query.queryItemValue(key);
					break;
				}
			}
		}

		ui.fileName->setText(fileName.isEmpty() ? tr("???") : fileName);
	}
	else {
		ui.infoLabel->setStyleSheet("color: red;");
		ui.infoLabel->setText(tr("无效URL！"));
		ui.fileName->setText(tr(""));
	}
}

void DownloadDialog::populateUrlFromClipboard()
{
	QString clipboardText = clipboard->text().trimmed();
	if (clipboardText.isEmpty()) {
		return;
	}
	else {
		QUrl url(clipboardText);
		if (url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty()) {
			ui.urlLineEdit->setText(clipboardText);
		}
	}
}
