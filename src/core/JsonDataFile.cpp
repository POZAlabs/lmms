/*
 * JsonDataFile.h - a convenient wrapper of QJsonDocument,
 *                  used for managing JSON data files
 *
 * Copyright (c) 2019 Hyunjin Song <tteu.ingog/at/gmail.com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include "JsonDataFile.h"

#include <QFile>

#include <utility>


JsonDataFile::JsonDataFile() : QJsonDocument()
{
}

JsonDataFile::JsonDataFile(const QString& fileName) : QJsonDocument()
{
	readFile(fileName);
}

JsonDataFile::JsonDataFile(const JsonDataFile& other) :
		QJsonDocument(static_cast<const QJsonDocument&>(other))
{
}
JsonDataFile::JsonDataFile(JsonDataFile&& other) :
		QJsonDocument(static_cast<QJsonDocument&&>(other))
{
}
JsonDataFile::JsonDataFile(const QJsonDocument& other) :
		QJsonDocument(other)
{
}
JsonDataFile::JsonDataFile(QJsonDocument&& other) :
		QJsonDocument(std::move(other))
{
}
JsonDataFile::JsonDataFile(const QJsonArray& array) :
		QJsonDocument(array)
{
}
JsonDataFile::JsonDataFile(const QJsonObject& object) :
		QJsonDocument(object)
{
}


JsonDataFile& JsonDataFile::operator=(const JsonDataFile& other)
{
	static_cast<QJsonDocument&>(*this) = static_cast<const QJsonDocument&>(other);
	return *this;
}

JsonDataFile& JsonDataFile::operator=(JsonDataFile&& other)
{
	static_cast<QJsonDocument&>(*this) = static_cast<const QJsonDocument&>(other);
	return *this;
}

JsonDataFile& JsonDataFile::operator=(const QJsonDocument& other)
{
	static_cast<QJsonDocument&>(*this) = other;
	return *this;
}

JsonDataFile& JsonDataFile::operator=(QJsonDocument&& other)
{
	static_cast<QJsonDocument&>(*this) = std::move(other);
	return *this;
}


JsonDataFile JsonDataFile::fromFile(const QString& fileName)
{
	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly)) {return JsonDataFile();}
	return JsonDataFile(QJsonDocument::fromJson(file.readAll()));
	file.close();
}

void JsonDataFile::readFile(const QString& fileName)
{
	QFile file(fileName);
	if (!file.open(QIODevice::ReadOnly)) {return;}
	static_cast<JsonDataFile&>(*this) = QJsonDocument::fromJson(file.readAll());
}

void JsonDataFile::writeFile(const QString& fileName)
{
	QFile file(fileName);
	if (!file.open(QIODevice::WriteOnly)) {return;}
	file.write(toJson());
}


QJsonValue JsonDataFile::value()
{
	if (isArray()) return QJsonValue(array());
	else if (isObject()) return QJsonValue(object());
	else return QJsonValue();
}


