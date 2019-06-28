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

#ifndef JSONDATAFILE_H
#define JSONDATAFILE_H

#include "lmms_export.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>


class LMMS_EXPORT JsonDataFile : public QJsonDocument
{
public:
	JsonDataFile();
	JsonDataFile(const QString& fileName);
	JsonDataFile(const JsonDataFile& other);
	JsonDataFile(JsonDataFile&& other);
	JsonDataFile(const QJsonDocument& other);
	JsonDataFile(QJsonDocument&& other);
	JsonDataFile(const QJsonArray& array);
	JsonDataFile(const QJsonObject& object);

	JsonDataFile& operator=(const JsonDataFile& other);
	JsonDataFile& operator=(JsonDataFile&& other);
	JsonDataFile& operator=(const QJsonDocument& other);
	JsonDataFile& operator=(QJsonDocument&& other);

	static JsonDataFile fromFile(const QString& fileName);
	void readFile(const QString& fileName);
	void writeFile(const QString& fileName);

	QJsonValue value();
};


#endif // #ifndef JSONDATAFILE_H

