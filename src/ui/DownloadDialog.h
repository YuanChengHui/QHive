#pragma once

#include "ui_DownloadDialog.h" 

#include <QUrl>
#include <QDialog>
#include <QClipboard>


class DownloadDialog : public QDialog
{
	Q_OBJECT

public:
	explicit DownloadDialog(QWidget* parent = nullptr);
	~DownloadDialog() override;

signals:
	void startDownload(const QString& taskId, const QUrl& url, const QString& initialFilename, const QString& saveDir);

private slots:
	void on_downloadButton_clicked();
	void on_cancelDownload_clicked();
	void on_changeSavePathButton_clicked();

private:
	void getFileNameFromUrl();
	void populateUrlFromClipboard();


private:
	Ui::DownloadDialogClass ui;

	QClipboard* clipboard;
};
