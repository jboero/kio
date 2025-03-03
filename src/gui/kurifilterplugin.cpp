/*
    SPDX-FileCopyrightText: 2000-2001, 2003, 2010 Dawit Alemayehu <adawit at kde.org>
    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "kurifilterplugin_p.h"

#include "kurifilterdata_p.h"
#include <QHostInfo>
#include <hostinfo.h>

KUriFilterPlugin::KUriFilterPlugin(const QString &name, QObject *parent)
    : QObject(parent)
    , d(nullptr)
{
    setObjectName(name);
}

KUriFilterPlugin::~KUriFilterPlugin() = default;

void KUriFilterPlugin::setFilteredUri(KUriFilterData &data, const QUrl &uri) const
{
    data.d->url = uri.adjusted(QUrl::NormalizePathSegments);
    data.d->wasModified = true;
    // qDebug() << "Got filtered to:" << uri;
}

void KUriFilterPlugin::setErrorMsg(KUriFilterData &data, const QString &errmsg) const
{
    data.d->errMsg = errmsg;
}

void KUriFilterPlugin::setUriType(KUriFilterData &data, KUriFilterData::UriTypes type) const
{
    data.d->uriType = type;
    data.d->wasModified = true;
}

void KUriFilterPlugin::setArguments(KUriFilterData &data, const QString &args) const
{
    data.d->args = args;
}

void KUriFilterPlugin::setSearchProvider(KUriFilterData &data, const QString &provider, const QString &term, const QChar &separator) const
{
    data.d->searchProvider = provider;
    data.d->searchTerm = term;
    data.d->searchTermSeparator = separator;
}

void KUriFilterPlugin::setSearchProviders(KUriFilterData &data, const QList<KUriFilterSearchProvider *> &providers) const
{
    data.d->searchProviderList.reserve(data.d->searchProviderList.size() + providers.size());
    for (KUriFilterSearchProvider *searchProvider : providers) {
        data.d->searchProviderList << searchProvider->name();
        data.d->searchProviderMap.insert(searchProvider->name(), searchProvider);
    }
}

QString KUriFilterPlugin::iconNameFor(const QUrl &url, KUriFilterData::UriTypes type) const
{
    return KUriFilterDataPrivate::lookupIconNameFor(url, type);
}

QHostInfo KUriFilterPlugin::resolveName(const QString &hostname, unsigned long timeout) const
{
    return KIO::HostInfo::lookupHost(hostname, timeout);
}
