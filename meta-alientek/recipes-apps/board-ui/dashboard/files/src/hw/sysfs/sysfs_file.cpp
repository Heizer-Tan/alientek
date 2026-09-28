/* SPDX-License-Identifier: MIT */
#include "sysfs_file.hpp"

#include <QFile>

SysfsFile::SysfsFile(QString path) : path_(std::move(path)) {}

QString SysfsFile::readTrim() const
{
	QFile f(path_);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();
	return QString::fromUtf8(f.readAll()).trimmed();
}

bool SysfsFile::write(const QString &value) const
{
	QFile f(path_);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		return false;
	const QByteArray data = value.toUtf8();
	return f.write(data) == data.size();
}
