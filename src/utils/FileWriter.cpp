#include "FileWriter.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

FileWriter::FileWriter(int id, QObject* parent)
	: QObject(parent)
	, m_id(id)
{
}

FileWriter::~FileWriter()
{
	closeAll();
}

void FileWriter::writeChunk(const QString& path, const QByteArray& data)
{
	QFile* file = getFile(path);
	if (!file) {
		emit writeFailed(path, QStringLiteral("无法打开文件: %1").arg(path));
		return;
	}

	qint64 written = file->write(data);
	if (written != data.size()) {
		emit writeFailed(path, QStringLiteral("写入文件失败: %1").arg(file->errorString()));
		return;
	}

	// 可选：立即 flush，确保数据落盘。对于大文件频繁 flush 可能影响性能，可以按需调用。
	// file->flush();

	emit writeFinished(path, written);
}

void FileWriter::closeFile(const QString& path)
{
	auto it = m_files.find(path);
	if (it != m_files.end()) {
		QFile* file = it.value();
		if (file->isOpen()) {
			file->close();
		}
		delete file;
		m_files.erase(it);
	}
}

void FileWriter::closeAll()
{
	for (auto it = m_files.begin(); it != m_files.end(); ++it) {
		QFile* file = it.value();
		if (file->isOpen()) {
			file->close();
		}
		delete file;
	}
	m_files.clear();
}

QFile* FileWriter::getFile(const QString& path)
{
	auto it = m_files.find(path);
	if (it != m_files.end()) {
		QFile* file = it.value();
		if (file->isOpen()) {
			return file;
		}
		else {
			// 文件被关闭了？重新打开
			if (file->open(QIODevice::WriteOnly | QIODevice::Append)) {
				return file;
			}
			else {
				return nullptr;
			}
		}
	}

	// 创建新文件
	QFile* file = new QFile(path);
	// 确保目录存在
	QFileInfo info(path);
	QDir dir = info.absoluteDir();
	if (!dir.exists()) {
		dir.mkpath(".");
	}

	if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
		delete file;
		return nullptr;
	}
	m_files.insert(path, file);
	return file;
}