/****************************************************************************
** Copyright (C) 2010-2016 Klaralvdalens Datakonsult AB, a KDAB Group company, info@kdab.com.
** Author: David Faure <david.faure@kdab.com>
** All rights reserved.
**
** This file initially comes from the KD Soap library.
**
** This file may be distributed and/or modified under the terms of the
** GNU Lesser General Public License version 2.1 and version 3 as published by the
** Free Software Foundation and appearing in the file COPYING.LIB included.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** Contact info@kdab.com if any conditions of this licensing are not
** clear to you.
**
**********************************************************************/

#ifndef HTTPSERVER_P_H
#define HTTPSERVER_P_H

#include <QThread>
#include <QMutex>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QStringList>
#include <QSslError>

class BlockingHttpServer;

class HttpServerThread : public QThread
{
    Q_OBJECT
public:
    enum Feature {
        Public = 0,    // HTTP with no ssl and no authentication needed
        Ssl = 1,       // HTTPS
        BasicAuth = 2,  // Requires authentication
        Error404 = 4   // Return "404 not found"
                   // bitfield, next item is 8
    };
    Q_DECLARE_FLAGS(Features, Feature)

    HttpServerThread(const QByteArray &dataToSend, Features features)
        : m_dataToSend(dataToSend), m_features(features)
    {
        start();
        m_ready.acquire();

    }
    ~HttpServerThread()
    {
        finish();
        wait();
    }

    void setContentType(const QByteArray &mime)
    {
        QMutexLocker lock(&m_mutex);
        m_contentType = mime;
    }

    void setResponseData(const QByteArray &data)
    {
        QMutexLocker lock(&m_mutex);
        m_dataToSend = data;
    }

    void setFeatures(Features features)
    {
        QMutexLocker lock(&m_mutex);
        m_features = features;
    }

    void disableSsl();
    inline int serverPort() const
    {
        QMutexLocker lock(&m_mutex);
        return m_port;
    }
    QString endPoint() const
    {
        return QString::fromLatin1("%1://127.0.0.1:%2/path")
               .arg(QString::fromLatin1((m_features & Ssl) ? "https" : "http"))
               .arg(serverPort());
    }

    void finish();

    QByteArray receivedData() const
    {
        QMutexLocker lock(&m_mutex);
        return m_receivedData;
    }
    QByteArray receivedHeaders() const
    {
        QMutexLocker lock(&m_mutex);
        return m_receivedHeaders;
    }
    void resetReceivedBuffers()
    {
        QMutexLocker lock(&m_mutex);
        m_receivedData.clear();
        m_receivedHeaders.clear();
    }

    QByteArray header(const QByteArray &value) const
    {
        QMutexLocker lock(&m_mutex);
        return m_headers.value(value);
    }

protected:
    /* \reimp */ void run() Q_DECL_OVERRIDE;

private:
    QByteArray makeHttpResponse(const QByteArray &responseData) const;

private:
    QByteArray m_partialRequest;
    QSemaphore m_ready;
    QByteArray m_dataToSend;
    QByteArray m_contentType;

    mutable QMutex m_mutex; // protects the 4 vars below
    QByteArray m_receivedData;
    QByteArray m_receivedHeaders;
    QMap<QByteArray, QByteArray> m_headers;
    int m_port;

    Features m_features;
    BlockingHttpServer *m_server;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(HttpServerThread::Features)

// A blocking http server (must be used in a thread) which supports SSL.
class BlockingHttpServer : public QTcpServer
{
    Q_OBJECT
public:
    BlockingHttpServer(bool ssl) : doSsl(ssl), sslSocket(0) {}
    ~BlockingHttpServer() {}

    QTcpSocket *waitForNextConnectionSocket()
    {
        if (!waitForNewConnection(20000)) { // 2000 would be enough, except in valgrind
            return 0;
        }
        if (doSsl) {
            Q_ASSERT(sslSocket);
            return sslSocket;
        } else {
            //qDebug() << "returning nextPendingConnection";
            return nextPendingConnection();
        }
    }

    void incomingConnection(qintptr socketDescriptor) Q_DECL_OVERRIDE;

    void disableSsl()
    {
        doSsl = false;
    }

private Q_SLOTS:
    void slotSslErrors(const QList<QSslError> &errors)
    {
        qDebug() << "server-side: slotSslErrors" << sslSocket->errorString() << errors;
    }
private:
    bool doSsl;
    QTcpSocket *sslSocket;
};

#endif /* HTTPSERVER_P_H */
