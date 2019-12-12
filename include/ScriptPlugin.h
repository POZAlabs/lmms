/*
 * ScriptPlugin.h - base class for scripting plugins
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

#ifndef SCRIPTPLUGIN_H
#define SCRIPTPLUGIN_H

#include "Plugin.h"
#include "Engine.h"

class LMMS_EXPORT ScriptPlugin : public Plugin
{
	MM_OPERATORS
	Q_OBJECT
public:
	ScriptPlugin(const Plugin::Descriptor * desc,
			Model * parent,
			const Descriptor::SubPluginFeatures::Key * key);
	virtual ~ScriptPlugin() = default;

	inline QString nodeName() const override
	{
		return "scriptingplugin";
	}

	// FIXME this should be able to return a meaningful result value or maybe an exception
	virtual void evaluateScript(const QString & scriptName, const QString & scriptContent) = 0;

private:
	;
};


#endif // #ifndef SCRIPTPLUGIN_H
