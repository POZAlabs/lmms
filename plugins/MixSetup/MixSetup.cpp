/*
 * MixSetup.cpp - A sample scripting plugin
 *
 * Copyright (c) 2019 - 2020 Hyunjin Song <tteu.ingog/at/gmail.com>
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

#include "MixSetup.h"

#include "embed.h"
#include "plugin_export.h"

#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "FxMixer.h"
#include "Song.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT mixsetup_plugin_descriptor =
{
	STRINGIFY(PLUGIN_NAME),
	"MixSetup",
	QT_TRANSLATE_NOOP("pluginBrowser", "A sample scripting plugin"),
	"Hyunjin Song <tteu.ingog/at/gmail.com>",
	0x0100,
	Plugin::Scripting,
	new PluginPixmapLoader("logo"),
	NULL,
	NULL
} ;

}



MixSetup::MixSetup(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	ScriptPlugin(&mixsetup_plugin_descriptor, parent, key)
{
}



MixSetup::~MixSetup()
{
}



void MixSetup::evaluateScript(const QString & scriptName, const QString & scriptContent)
{
	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(scriptContent.toUtf8(), &err);
	if (err.error != QJsonParseError::NoError)
	{
		qWarning().noquote() << QStringLiteral("Failed to parse \"%1\" as a JSON file: \"%2\".").arg(scriptName).arg(err.errorString());
		return;
	}
	if (!doc.isObject())
	{
		qWarning("Bad input data format.");
		return;
	}

	Engine::getSong()->clearProject();

	QJsonObject obj = doc.object();
	QString fileToLoad = obj["basefile"].toString();
	if (!fileToLoad.isEmpty())
	{
		Engine::getSong()->loadProject(fileToLoad);
	}

	QJsonArray chs = obj["channels"].toArray();

	qInfo("Processing %d FX channels...", chs.size());
	for (auto val : chs)
	{
		if (!val.isObject())
		{
			qWarning("Channel descriptor is not an object.");
			continue;
		}

		QJsonObject chData = val.toObject();
		int idx = chData["index"].toInt(-1);
		if (idx < 0)
		{
			qWarning("Invalid channel index %d.", idx);
			continue;
		}

		// ensure the target FX channel exists
		for (int n = idx + 1 - Engine::fxMixer()->numChannels(); n > 0; --n)
		{
			Engine::fxMixer()->createChannel();
		}

		// reset the channel if requested
		if (chData["clear"].toBool(false))
		{
			Engine::fxMixer()->clearChannel(idx);
		}

		QJsonArray effects = chData["effects"].toArray();
		qInfo("Processing %d effects for channel %d...", effects.size(), idx);

		EffectChain & fxChain = Engine::fxMixer()->effectChannel(idx)->m_fxChain;
		for (auto val2 : effects)
		{
			if (!val2.isObject())
			{
				qWarning("Effect descriptor is not an object.");
				continue;
			}
			QJsonObject effectData = val2.toObject();

			QString effectName = effectData["name"].toString();
			if (effectName.isEmpty()) {continue;}

			Plugin::Descriptor::SubPluginFeatures::Key key;
			// XXX why do I need this 'class'?
			class Effect * e = Effect::instantiate(effectData["name"].toString(), &fxChain, &key);
			if (e)
			{
				QString fileName = effectData["file"].toString("");
				if (!fileName.isEmpty()) {e->loadFile(fileName);}
				/*
				// TODO effect presets are not implemented yet
				QString presetFileName = effectData["presetFile"].toString("");
				*/
				QString pluginPresetFileName = effectData["pluginPresetFile"].toString("");
				if (!pluginPresetFileName.isEmpty()) {e->loadPluginPresetFile(pluginPresetFileName);}
				fxChain.appendEffect(e);
			}
			else
			{
				qWarning().noquote() << QStringLiteral("Failed to add effect \"%1\"").arg(effectName);
			}
		}
	}

	QString fileToSave = obj["savefile"].toString();
	if (!fileToSave.isEmpty())
	{
		bool result = Engine::getSong()->saveProjectFile(fileToSave);
		if (!result)
		{
			qWarning("Failed to save the result.");
			return;
		}
	}
	QCoreApplication::processEvents();
	qInfo("Done.");
}




void MixSetup::saveSettings(QDomDocument & doc, QDomElement & element)
{
}



void MixSetup::loadSettings(const QDomElement & element)
{
}



PluginView * MixSetup::instantiateView(QWidget * parent)
{
	return nullptr;
}



extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main(Model* parent, void* data)
{
	return new MixSetup(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>(data));
}

}

