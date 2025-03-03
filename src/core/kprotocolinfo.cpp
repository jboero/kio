/*
    This file is part of the KDE libraries
    SPDX-FileCopyrightText: 1999 Torben Weis <weis@kde.org>
    SPDX-FileCopyrightText: 2003 Waldo Bastian <bastian@kde.org>
    SPDX-FileCopyrightText: 2012 David Faure <faure@kde.org>
    SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include "kprotocolinfo.h"
#include "kprotocolinfo_p.h"
#include "kprotocolinfofactory_p.h"

#include "kiocoredebug.h"

#include <KApplicationTrader>
#include <KConfig>
#include <KConfigGroup>
#include <KJsonUtils>
#include <KPluginMetaData>
#include <KSharedConfig>
#include <QUrl>

KProtocolInfoPrivate::KProtocolInfoPrivate(const QString &name, const QString &exec, const QJsonObject &json)
    : m_name(name)
    , m_exec(exec)
{
    // source has fallback true if not set
    m_isSourceProtocol = json.value(QStringLiteral("source")).toBool(true);
    // true if not set for backwards compatibility
    m_supportsPermissions = json.value(QStringLiteral("permissions")).toBool(true);

    // other bools are fine with default false by toBool
    m_isHelperProtocol = json.value(QStringLiteral("helper")).toBool();
    m_supportsReading = json.value(QStringLiteral("reading")).toBool();
    m_supportsWriting = json.value(QStringLiteral("writing")).toBool();
    m_supportsMakeDir = json.value(QStringLiteral("makedir")).toBool();
    m_supportsDeleting = json.value(QStringLiteral("deleting")).toBool();
    m_supportsLinking = json.value(QStringLiteral("linking")).toBool();
    m_supportsMoving = json.value(QStringLiteral("moving")).toBool();
    m_supportsOpening = json.value(QStringLiteral("opening")).toBool();
    m_supportsTruncating = json.value(QStringLiteral("truncating")).toBool();
    m_canCopyFromFile = json.value(QStringLiteral("copyFromFile")).toBool();
    m_canCopyToFile = json.value(QStringLiteral("copyToFile")).toBool();
    m_canRenameFromFile = json.value(QStringLiteral("renameFromFile")).toBool();
    m_canRenameToFile = json.value(QStringLiteral("renameToFile")).toBool();
    m_canDeleteRecursive = json.value(QStringLiteral("deleteRecursive")).toBool();

    // default is "FromURL"
    const QString fnu = json.value(QStringLiteral("fileNameUsedForCopying")).toString();
    m_fileNameUsedForCopying = KProtocolInfo::FromUrl;
    if (fnu == QLatin1String("Name")) {
        m_fileNameUsedForCopying = KProtocolInfo::Name;
    } else if (fnu == QLatin1String("DisplayName")) {
        m_fileNameUsedForCopying = KProtocolInfo::DisplayName;
    }

    m_listing = json.value(QStringLiteral("listing")).toVariant().toStringList();
    // Many .protocol files say "Listing=false" when they really mean "Listing=" (i.e. unsupported)
    if (m_listing.count() == 1 && m_listing.first() == QLatin1String("false")) {
        m_listing.clear();
    }
    m_supportsListing = (m_listing.count() > 0);

    m_defaultMimetype = json.value(QStringLiteral("defaultMimetype")).toString();

    // determineMimetypeFromExtension has fallback true if not set
    m_determineMimetypeFromExtension = json.value(QStringLiteral("determineMimetypeFromExtension")).toBool(true);

    m_archiveMimeTypes = json.value(QStringLiteral("archiveMimetype")).toVariant().toStringList();

    m_icon = json.value(QStringLiteral("Icon")).toString();

    // config has fallback to name if not set
    m_config = json.value(QStringLiteral("config")).toString(m_name);

    // max workers has fallback to 1 if not set
    m_maxWorkers = json.value(QStringLiteral("maxInstances")).toInt(1);

    m_maxWorkersPerHost = json.value(QStringLiteral("maxInstancesPerHost")).toInt();

    QString tmp = json.value(QStringLiteral("input")).toString();
    if (tmp == QLatin1String("filesystem")) {
        m_inputType = KProtocolInfo::T_FILESYSTEM;
    } else if (tmp == QLatin1String("stream")) {
        m_inputType = KProtocolInfo::T_STREAM;
    } else {
        m_inputType = KProtocolInfo::T_NONE;
    }

    tmp = json.value(QStringLiteral("output")).toString();
    if (tmp == QLatin1String("filesystem")) {
        m_outputType = KProtocolInfo::T_FILESYSTEM;
    } else if (tmp == QLatin1String("stream")) {
        m_outputType = KProtocolInfo::T_STREAM;
    } else {
        m_outputType = KProtocolInfo::T_NONE;
    }

    m_docPath = json.value(QStringLiteral("X-DocPath")).toString();
    if (m_docPath.isEmpty()) {
        m_docPath = json.value(QStringLiteral("DocPath")).toString();
    }

    m_protClass = json.value(QStringLiteral("Class")).toString().toLower();
    if (!m_protClass.startsWith(QLatin1Char(':'))) {
        m_protClass.prepend(QLatin1Char(':'));
    }

    // ExtraNames is a translated value, use the KCoreAddons helper to read it
    const QStringList extraNames = KJsonUtils::readTranslatedValue(json, QStringLiteral("ExtraNames")).toVariant().toStringList();
    const QStringList extraTypes = json.value(QStringLiteral("ExtraTypes")).toVariant().toStringList();
    if (extraNames.size() == extraTypes.size()) {
        auto func = [](const QString &name, const QString &type) {
            const int metaType = QMetaType::fromName(type.toLatin1().constData()).id();
            // currently QMetaType::Type and ExtraField::Type use the same subset of values, so we can just cast.
            return KProtocolInfo::ExtraField(name, static_cast<KProtocolInfo::ExtraField::Type>(metaType));
        };

        std::transform(extraNames.cbegin(), extraNames.cend(), extraTypes.cbegin(), std::back_inserter(m_extraFields), func);
    } else {
        qCWarning(KIO_CORE) << "Malformed JSON protocol file for protocol:" << name
                            << ", number of the ExtraNames fields should match the number of ExtraTypes fields";
    }

    // fallback based on class
    m_showPreviews = json.value(QStringLiteral("ShowPreviews")).toBool(m_protClass == QLatin1String(":local"));

    m_capabilities = json.value(QStringLiteral("Capabilities")).toVariant().toStringList();

    m_proxyProtocol = json.value(QStringLiteral("ProxiedBy")).toString();
}

//
// Static functions:
//

QStringList KProtocolInfo::protocols()
{
    return KProtocolInfoFactory::self()->protocols();
}

bool KProtocolInfo::isFilterProtocol(const QString &_protocol)
{
    // We call the findProtocol directly (not via KProtocolManager) to bypass any proxy settings.
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return false;
    }

    return !prot->m_isSourceProtocol;
}

QString KProtocolInfo::icon(const QString &_protocol)
{
    // We call the findProtocol directly (not via KProtocolManager) to bypass any proxy settings.
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        if (auto service = KApplicationTrader::preferredService(QLatin1String("x-scheme-handler/") + _protocol)) {
            return service->icon();
        } else {
            return QString();
        }
    }

    return prot->m_icon;
}

QString KProtocolInfo::config(const QString &_protocol)
{
    // We call the findProtocol directly (not via KProtocolManager) to bypass any proxy settings.
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return QString();
    }

    return QStringLiteral("kio_%1rc").arg(prot->m_config);
}

int KProtocolInfo::maxWorkers(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return 1;
    }

    return prot->m_maxWorkers;
}

int KProtocolInfo::maxWorkersPerHost(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return 0;
    }

    return prot->m_maxWorkersPerHost;
}

bool KProtocolInfo::determineMimetypeFromExtension(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return true;
    }

    return prot->m_determineMimetypeFromExtension;
}

QString KProtocolInfo::exec(const QString &protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(protocol);
    if (!prot) {
        return QString();
    }
    return prot->m_exec;
}

KProtocolInfo::ExtraFieldList KProtocolInfo::extraFields(const QUrl &url)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(url.scheme());
    if (!prot) {
        return ExtraFieldList();
    }

    return prot->m_extraFields;
}

QString KProtocolInfo::defaultMimetype(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return QString();
    }

    return prot->m_defaultMimetype;
}

QString KProtocolInfo::docPath(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return QString();
    }

    return prot->m_docPath;
}

QString KProtocolInfo::protocolClass(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return QString();
    }

    return prot->m_protClass;
}

bool KProtocolInfo::showFilePreview(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    const bool defaultSetting = prot ? prot->m_showPreviews : false;

    KConfigGroup group(KSharedConfig::openConfig(), "PreviewSettings");
    return group.readEntry(_protocol, defaultSetting);
}

QStringList KProtocolInfo::capabilities(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return QStringList();
    }

    return prot->m_capabilities;
}

QStringList KProtocolInfo::archiveMimetypes(const QString &protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(protocol);
    if (!prot) {
        return QStringList();
    }

    return prot->m_archiveMimeTypes;
}

QString KProtocolInfo::proxiedBy(const QString &_protocol)
{
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(_protocol);
    if (!prot) {
        return QString();
    }

    return prot->m_proxyProtocol;
}

bool KProtocolInfo::isFilterProtocol(const QUrl &url)
{
    return isFilterProtocol(url.scheme());
}

bool KProtocolInfo::isHelperProtocol(const QUrl &url)
{
    return isHelperProtocol(url.scheme());
}

bool KProtocolInfo::isHelperProtocol(const QString &protocol)
{
    // We call the findProtocol directly (not via KProtocolManager) to bypass any proxy settings.
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(protocol);
    if (prot) {
        return prot->m_isHelperProtocol;
    }
    return false;
}

bool KProtocolInfo::isKnownProtocol(const QUrl &url)
{
    return isKnownProtocol(url.scheme());
}

bool KProtocolInfo::isKnownProtocol(const QString &protocol)
{
    // We call the findProtocol (const QString&) to bypass any proxy settings.
    KProtocolInfoPrivate *prot = KProtocolInfoFactory::self()->findProtocol(protocol);
    return prot;
}
