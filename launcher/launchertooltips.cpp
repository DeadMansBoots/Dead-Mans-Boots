/*
 * launchertooltips.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "launchertooltips.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace
{
const QJsonObject & tooltips()
{
	static const QJsonObject loaded = []()
	{
		QFile file(":/tooltips.json");
		if(!file.open(QIODevice::ReadOnly))
			return QJsonObject();
		return QJsonDocument::fromJson(file.readAll()).object();
	}();
	return loaded;
}
}

void LauncherTooltips::apply(QWidget * root, const QString & uiFile)
{
	const QString prefix = uiFile + "/";
	const QJsonObject & texts = tooltips();
	for(auto it = texts.begin(); it != texts.end(); ++it)
	{
		if(!it.key().startsWith(prefix))
			continue;
		const QString name = it.key().mid(prefix.size());
		for(QWidget * widget : root->findChildren<QWidget *>(name))
			if(widget->toolTip().isEmpty())
			{
				widget->setToolTip(it.value().toString());
				break;
			}
	}
}
