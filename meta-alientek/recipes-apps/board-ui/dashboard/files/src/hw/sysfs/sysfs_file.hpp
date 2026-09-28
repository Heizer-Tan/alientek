/* SPDX-License-Identifier: MIT */
#pragma once

#include <QString>

/* 单个 sysfs 文本节点：按路径读/写 */
class SysfsFile final {
public:
	explicit SysfsFile(QString path);

	QString path() const { return path_; }

	/* 读出并 trim；失败返回空串 */
	QString readTrim() const;

	/* 整文件覆盖写入；成功返回 true */
	bool write(const QString &value) const;

private:
	QString path_;
};
