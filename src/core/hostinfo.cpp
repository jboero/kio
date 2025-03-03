/*
    SPDX-FileCopyrightText: 2008 Roland Harnau <tau@gmx.eu>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "hostinfo.h"

#include <QCache>
#include <QFutureWatcher>
#include <QHash>
#include <QHostInfo>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QSemaphore>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QtConcurrentRun>

#ifdef Q_OS_UNIX
#include <QFileInfo>
#include <arpa/nameser.h>
#include <netinet/in.h>
#include <resolv.h> // for _PATH_RESCONF
#ifndef _PATH_RESCONF
#define _PATH_RESCONF "/etc/resolv.conf"
#endif
#endif

static constexpr int TTL = 300;

namespace KIO
{
class HostInfoAgentPrivate : public QObject
{
    Q_OBJECT

    class Query;

public:
    explicit HostInfoAgentPrivate(int cacheSize = 100);
    ~HostInfoAgentPrivate() override
    {
    }
    void lookupHost(const QString &hostName, QObject *receiver, const char *member);
    QHostInfo lookupCachedHostInfoFor(const QString &hostName);
    void cacheLookup(const QHostInfo &);
private Q_SLOTS:
    void queryFinished(const QHostInfo &info, Query *sender);

private:
    class Result;

    QHash<QString, Query *> openQueries;
    struct HostCacheInfo {
        QHostInfo hostInfo;
        QTime time;
    };
    QCache<QString, HostCacheInfo> dnsCache;
    QDateTime resolvConfMTime;
};

class HostInfoAgentPrivate::Result : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void result(const QHostInfo &);

private:
    friend class HostInfoAgentPrivate;
};

class HostInfoAgentPrivate::Query : public QObject
{
    Q_OBJECT
public:
    Query()
        : m_watcher()
        , m_hostName()
    {
        connect(&m_watcher, &QFutureWatcher<QHostInfo>::finished, this, &Query::relayFinished);
    }
    void start(const QString &hostName)
    {
        m_hostName = hostName;
        QFuture<QHostInfo> future = QtConcurrent::run(&QHostInfo::fromName, hostName);
        m_watcher.setFuture(future);
    }
    QString hostName() const
    {
        return m_hostName;
    }
Q_SIGNALS:
    void result(const QHostInfo &);
private Q_SLOTS:
    void relayFinished()
    {
        Q_EMIT result(m_watcher.result());
    }

private:
    QFutureWatcher<QHostInfo> m_watcher;
    QString m_hostName;
};

class NameLookupThreadRequest
{
public:
    NameLookupThreadRequest(const QString &hostName)
        : m_hostName(hostName)
    {
    }

    QSemaphore *semaphore()
    {
        return &m_semaphore;
    }

    QHostInfo result() const
    {
        return m_hostInfo;
    }

    void setResult(const QHostInfo &hostInfo)
    {
        m_hostInfo = hostInfo;
    }

    QString hostName() const
    {
        return m_hostName;
    }

    int lookupId() const
    {
        return m_lookupId;
    }

    void setLookupId(int id)
    {
        m_lookupId = id;
    }

private:
    Q_DISABLE_COPY(NameLookupThreadRequest)
    QString m_hostName;
    QSemaphore m_semaphore;
    QHostInfo m_hostInfo;
    int m_lookupId;
};
}

Q_DECLARE_METATYPE(std::shared_ptr<KIO::NameLookupThreadRequest>)

namespace KIO
{
class NameLookUpThreadWorker : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    void lookupHost(const std::shared_ptr<KIO::NameLookupThreadRequest> &request)
    {
        const QString hostName = request->hostName();
        const int lookupId = QHostInfo::lookupHost(hostName, this, SLOT(lookupFinished(QHostInfo)));
        request->setLookupId(lookupId);
        m_lookups.insert(lookupId, request);
    }

    void abortLookup(const std::shared_ptr<KIO::NameLookupThreadRequest> &request)
    {
        QHostInfo::abortHostLookup(request->lookupId());
        m_lookups.remove(request->lookupId());
    }

    void lookupFinished(const QHostInfo &hostInfo)
    {
        auto it = m_lookups.find(hostInfo.lookupId());
        if (it != m_lookups.end()) {
            (*it)->setResult(hostInfo);
            (*it)->semaphore()->release();
            m_lookups.erase(it);
        }
    }

private:
    QMap<int, std::shared_ptr<NameLookupThreadRequest>> m_lookups;
};

class NameLookUpThread : public QThread
{
    Q_OBJECT
public:
    NameLookUpThread()
        : m_worker(nullptr)
    {
        qRegisterMetaType<std::shared_ptr<NameLookupThreadRequest>>();
        start();
    }

    ~NameLookUpThread() override
    {
        quit();
        wait();
    }

    NameLookUpThreadWorker *worker()
    {
        return m_worker;
    }

    QSemaphore *semaphore()
    {
        return &m_semaphore;
    }

    void run() override
    {
        NameLookUpThreadWorker worker;
        m_worker = &worker;
        m_semaphore.release();
        exec();
    }

private:
    NameLookUpThreadWorker *m_worker;
    QSemaphore m_semaphore;
};
}

using namespace KIO;

Q_GLOBAL_STATIC(HostInfoAgentPrivate, hostInfoAgentPrivate)
Q_GLOBAL_STATIC(NameLookUpThread, nameLookUpThread)

void HostInfo::lookupHost(const QString &hostName, QObject *receiver, const char *member)
{
    hostInfoAgentPrivate()->lookupHost(hostName, receiver, member);
}

QHostInfo HostInfo::lookupHost(const QString &hostName, unsigned long timeout)
{
    // Do not perform a reverse lookup here...
    QHostAddress address(hostName);
    QHostInfo hostInfo;
    if (!address.isNull()) {
        QList<QHostAddress> addressList;
        addressList << address;
        hostInfo.setAddresses(addressList);
        return hostInfo;
    }

    // Look up the name in the KIO/KHTML DNS cache...
    hostInfo = HostInfo::lookupCachedHostInfoFor(hostName);
    if (!hostInfo.hostName().isEmpty() && hostInfo.error() == QHostInfo::NoError) {
        return hostInfo;
    }

    // Failing all of the above, do the lookup...
    std::shared_ptr<NameLookupThreadRequest> request = std::make_shared<NameLookupThreadRequest>(hostName);
    nameLookUpThread()->semaphore()->acquire();
    nameLookUpThread()->semaphore()->release();
    NameLookUpThreadWorker *worker = nameLookUpThread()->worker();
    auto lookupFunc = [worker, request]() {
        worker->lookupHost(request);
    };
    QMetaObject::invokeMethod(worker, lookupFunc, Qt::QueuedConnection);
    if (request->semaphore()->tryAcquire(1, timeout)) {
        hostInfo = request->result();
        if (!hostInfo.hostName().isEmpty() && hostInfo.error() == QHostInfo::NoError) {
            HostInfo::cacheLookup(hostInfo); // cache the look up...
        }
    } else {
        auto abortFunc = [worker, request]() {
            worker->abortLookup(request);
        };
        QMetaObject::invokeMethod(worker, abortFunc, Qt::QueuedConnection);
    }

    // qDebug() << "Name look up succeeded for" << hostName;
    return hostInfo;
}

QHostInfo HostInfo::lookupCachedHostInfoFor(const QString &hostName)
{
    return hostInfoAgentPrivate()->lookupCachedHostInfoFor(hostName);
}

void HostInfo::cacheLookup(const QHostInfo &info)
{
    hostInfoAgentPrivate()->cacheLookup(info);
}

HostInfoAgentPrivate::HostInfoAgentPrivate(int cacheSize)
    : openQueries()
    , dnsCache(cacheSize)
{
    qRegisterMetaType<QHostInfo>();
}

void HostInfoAgentPrivate::lookupHost(const QString &hostName, QObject *receiver, const char *member)
{
#ifdef _PATH_RESCONF
    QFileInfo resolvConf(QFile::decodeName(_PATH_RESCONF));
    QDateTime currentMTime = resolvConf.lastModified();
    if (resolvConf.exists() && currentMTime != resolvConfMTime) {
        // /etc/resolv.conf has been modified
        // clear our cache
        resolvConfMTime = currentMTime;
        dnsCache.clear();
    }
#endif

    if (HostCacheInfo *info = dnsCache.object(hostName)) {
        if (QTime::currentTime() <= info->time.addSecs(TTL)) {
            Result result;
            if (receiver) {
                QObject::connect(&result, SIGNAL(result(QHostInfo)), receiver, member);
                Q_EMIT result.result(info->hostInfo);
            }
            return;
        }
        dnsCache.remove(hostName);
    }

    if (Query *query = openQueries.value(hostName)) {
        if (receiver) {
            connect(query, SIGNAL(result(QHostInfo)), receiver, member);
        }
        return;
    }

    Query *query = new Query();
    openQueries.insert(hostName, query);
    connect(query, &Query::result, this, [this, query](const QHostInfo &info) {
        queryFinished(info, query);
    });
    if (receiver) {
        connect(query, SIGNAL(result(QHostInfo)), receiver, member);
    }
    query->start(hostName);
}

QHostInfo HostInfoAgentPrivate::lookupCachedHostInfoFor(const QString &hostName)
{
    HostCacheInfo *info = dnsCache.object(hostName);
    if (info && info->time.addSecs(TTL) >= QTime::currentTime()) {
        return info->hostInfo;
    }

    // not found in dnsCache
    QHostInfo hostInfo;
    hostInfo.setError(QHostInfo::HostNotFound);
    return hostInfo;
}

void HostInfoAgentPrivate::cacheLookup(const QHostInfo &info)
{
    if (info.hostName().isEmpty()) {
        return;
    }

    if (info.error() != QHostInfo::NoError) {
        return;
    }

    dnsCache.insert(info.hostName(), new HostCacheInfo{info, QTime::currentTime()});
}

void HostInfoAgentPrivate::queryFinished(const QHostInfo &info, Query *sender)
{
    const auto host = sender->hostName();
    openQueries.remove(host);
    if (info.error() == QHostInfo::NoError) {
        dnsCache.insert(host, new HostCacheInfo{info, QTime::currentTime()});
    }
    sender->deleteLater();
}

#include "hostinfo.moc"
