#include "QHive.h"
#include "DatabaseManager.h"
#include "NetworkContextPool.h"
#include "FileWriterPool.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	// 数据库
	if (!DatabaseManager::instance()->init()) {
		QMessageBox::critical(nullptr, "错误", "初始化数据库失败！");
		return -1;
	}

	// 全局网络上下文池
	NetworkContextPool::instance()->init(4);

	// 文件写入池
	FileWriterPool::instance()->init(2);

	// 主窗口
	QHive window;
	window.show();

	int ret = app.exec();

	// 逆序关闭
	FileWriterPool::instance()->shutdown();
	NetworkContextPool::instance()->shutdown();
	DatabaseManager::instance()->close();

	return ret;
}