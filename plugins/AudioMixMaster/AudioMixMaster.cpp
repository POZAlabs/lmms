/*
 * AudioMixMaster.cpp - A sample scripting plugin
 *
 * Copyright (c) 2020 Hyunjin Song <tteu.ingog/at/gmail.com>
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

#include "AudioMixMaster.h"

#include "embed.h"
#include "plugin_export.h"

#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "FxMixer.h"
#include "ImportFilter.h"
#include "SampleTrack.h"
#include "Song.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <tuple>
#include <vector>

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT audiomixmaster_plugin_descriptor =
{
	STRINGIFY(PLUGIN_NAME),
	"AudioMixMaster",
	QT_TRANSLATE_NOOP("pluginBrowser", "A sample scripting plugin"),
	"Hyunjin Song <tteu.ingog/at/gmail.com>",
	0x0100,
	Plugin::Scripting,
	new PluginPixmapLoader("logo"),
	NULL,
	NULL
} ;

}



AudioMixMaster::AudioMixMaster(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	ScriptPlugin(&audiomixmaster_plugin_descriptor, parent, key)
{
}



AudioMixMaster::~AudioMixMaster()
{
}



struct ExtraChannelInfo
{
	bool isRoot;
	bool isMuted;
	bool isSolo;
	int inputIndex;
	bool inputIsBus;
	int outputIndex;
	bool outputIsBus;
};



ExtraChannelInfo parseExtraChannelInfo(const QJsonObject &obj)
{
	return ExtraChannelInfo
	{
		.isRoot = obj["isRoot"].toBool(false),
		.isMuted = obj["isMuted"].toBool(false),
		.isSolo = obj["isSolo"].toBool(false),
		.inputIndex = obj["inIdx"].toInt(-1),
		.inputIsBus = obj["inIsBus"].toBool(false),
		.outputIndex = obj["outIdx"].toInt(-1),
		.outputIsBus = obj["outIsBus"].toBool(false)
	};
}



void AudioMixMaster::evaluateScript(const QString & scriptName, const QString & scriptContent)
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
	QJsonArray inputs = obj["inputs"].toArray();

	auto processEffects = [](QJsonArray effects, int idx)
	{
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
	};


	// override masterIndex if you want to route the output to other channels
	auto processChannels = [&processEffects] (QJsonArray chs, int &curIndex, int masterIndex = 0)
	{
		int beginIndex = curIndex;
		int numChannelsToAdd = chs.size();
		int curIndexInGroup = 0;
		int rootIndex = -1;
		std::vector<int> inputIndexes, outputIndexes;
		std::vector<std::tuple<int, int, float> > sends; // TODO use a struct if desired
		inputIndexes.resize(numChannelsToAdd);
		outputIndexes.resize(numChannelsToAdd);

		qInfo("Processing %d FX channels...", numChannelsToAdd);
		// Engine::fxMixer()->allocateChannelsTo(beginIndex - 1 + numChannelsToAdd);
		for (int i = Engine::fxMixer()->numChannels(); i < beginIndex + numChannelsToAdd; ++i)
		{
			Engine::fxMixer()->createChannel();

			// delete the default send to master
			Engine::fxMixer()->deleteChannelSend(i, 0);
		}

		for (auto val : chs)
		{
			if (!val.isObject())
			{
				qWarning("Channel descriptor is not an object.");
				continue;
			}

			QJsonObject chData = val.toObject();
			ExtraChannelInfo extraInfo = parseExtraChannelInfo(chData);

			// set mute, ignoring solo
			Engine::fxMixer()->effectChannel(curIndex)->m_muteModel.setValue(extraInfo.isMuted);

			// process effects
			processEffects(chData["effects"].toArray(), curIndex);

			// handle routing
			if (extraInfo.isRoot)
			{
				rootIndex = curIndexInGroup + beginIndex;
			}
			inputIndexes[curIndexInGroup] = extraInfo.inputIsBus ? extraInfo.inputIndex : -2;
			outputIndexes[curIndexInGroup] = extraInfo.outputIsBus ? extraInfo.outputIndex : -3;
			// FIXME is this condition always right with internal routing?
			if (extraInfo.isRoot || (!extraInfo.outputIsBus && extraInfo.outputIndex == 0))
			{
				// send to master
				Engine::fxMixer()->createChannelSend(curIndex, masterIndex);
			}
			QJsonArray sendsData = chData["sends"].toArray();
			for (auto val2 : sendsData)
			{
				if (!val2.isObject())
				{
					qWarning("Send descriptor is not an object.");
					continue;
				}
				QJsonObject sendData = val2.toObject();
				int sendTarget = sendData["target"].toInt(-1);
				float sendGain = static_cast<float>(sendData["gain"].toDouble(0.0));
				if (sendTarget >= 0 && sendTarget != extraInfo.inputIndex)
				{
					sends.push_back({curIndex, sendTarget, sendGain});
				}
			}
			++curIndex;
			++curIndexInGroup;
		}
		// complete routing
		for (int src = 0; src < numChannelsToAdd; ++src)
		{
			for (int dst = 0; dst < numChannelsToAdd; ++dst)
			{
				if (src != dst && outputIndexes[src] == inputIndexes[dst])
				{
					Engine::fxMixer()->createChannelSend(beginIndex + src, beginIndex + dst);
				}
			}
		}
		for (auto sendInfo : sends)
		{
			for (int dst = 0; dst < numChannelsToAdd; ++dst)
			{
				if (std::get<1>(sendInfo) == inputIndexes[dst])
				{
					Engine::fxMixer()->createChannelSend(std::get<0>(sendInfo), beginIndex + dst, std::get<2>(sendInfo));
				}
			}
		}

		curIndex += numChannelsToAdd;
		return rootIndex;
	};

	// now process channels
	int curIndex = 1; // 0 is master
	int masterIndex = 0;

	// master channel
	if (obj["master"].isObject())
	{
		// using CST
		QJsonObject masterChannel = obj["master"].toObject();
		processEffects(masterChannel["effects"].toArray(), 0);
	}
	else if (obj["master"].isArray())
	{
		// using PATCH
		masterIndex = processChannels(obj["master"].toArray(), curIndex, 0);
	}
	else if (!obj["master"].isNull())
	{
		qWarning("Invalid type for master channel info");
	}

	// others for inputs
	for (auto elem : inputs) // TODO
	{
		if (!elem.isObject())
		{
			qWarning("Input descriptor is not an object.");
			continue;
		}

		QJsonObject currentInput = elem.toObject();

		// setup channels
		int rootIndex = processChannels(currentInput["channels"].toArray(), curIndex, masterIndex);

		QString sampleFile = currentInput["audiofile"].toString();
		if (!sampleFile.isEmpty())
		{
			// add a sample track
			SampleTrack *st = static_cast<SampleTrack*>(Track::create(Track::SampleTrack, Engine::getSong()));
			st->effectChannelModel()->setInitValue(rootIndex);

			// load the sample track
			SampleTCO *stco = static_cast<SampleTCO*>(st->createTCO(MidiTime(0)));
			stco->setSampleFile(sampleFile);
		}

		QString midiFile = currentInput["midifile"].toString();
		if (!midiFile.isEmpty())
		{
			ImportFilter::import(midiFile, Engine::getSong(), currentInput["midiconfig"]);
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




void AudioMixMaster::saveSettings(QDomDocument & doc, QDomElement & element)
{
}



void AudioMixMaster::loadSettings(const QDomElement & element)
{
}



PluginView * AudioMixMaster::instantiateView(QWidget * parent)
{
	return nullptr;
}



extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main(Model* parent, void* data)
{
	return new AudioMixMaster(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>(data));
}

}

