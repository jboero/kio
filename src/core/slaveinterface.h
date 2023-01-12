/*
    This file is part of the KDE project
    SPDX-FileCopyrightText: 2000 David Faure <faure@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef __kio_slaveinterface_h
#define __kio_slaveinterface_h

#include <qplatformdefs.h>

#include <QHostInfo>
#include <QObject>
#include <QTimer>

#include <kio/authinfo.h>
#include <kio/global.h>
#include <kio/udsentry.h>

#include "connection_p.h"

class QUrl;

namespace KIO
{

// Definition of enum Command has been moved to global.h

/**
 * Identifiers for KIO informational messages.
 */
enum Info {
    INF_TOTAL_SIZE = 10,
    INF_PROCESSED_SIZE = 11,
    INF_SPEED,
    INF_REDIRECTION = 20,
    INF_MIME_TYPE = 21,
    INF_ERROR_PAGE = 22,
    INF_WARNING = 23,
    INF_UNUSED = 25, ///< now unused
    INF_INFOMESSAGE,
    INF_META_DATA,
    INF_MESSAGEBOX,
    INF_POSITION,
    INF_TRUNCATED,
    // add new ones here once a release is done, to avoid breaking binary compatibility
};

/**
 * Identifiers for KIO data messages.
 */
enum Message {
    MSG_DATA = 100,
    MSG_DATA_REQ,
    MSG_ERROR,
    MSG_CONNECTED,
    MSG_FINISHED,
    MSG_STAT_ENTRY, // 105
    MSG_LIST_ENTRIES,
    MSG_RENAMED, ///< unused
    MSG_RESUME,
    MSG_SLAVE_ACK,
    MSG_NEED_SUBURL_DATA,
    MSG_CANRESUME,
    MSG_OPENED,
    MSG_WRITTEN,
    MSG_HOST_INFO_REQ,
    MSG_PRIVILEGE_EXEC,
    MSG_SLAVE_STATUS_V2,
    // add new ones here once a release is done, to avoid breaking binary compatibility
};

/**
 * @class KIO::SlaveInterface slaveinterface.h <KIO/SlaveInterface>
 *
 * There are two classes that specifies the protocol between application
 * ( KIO::Job) and kioslave. SlaveInterface is the class to use on the application
 * end, SlaveBase is the one to use on the slave end.
 *
 * A call to foo() results in a call to slotFoo() on the other end.
 */
class SlaveInterface : public QObject
{
    Q_OBJECT

protected:
    explicit SlaveInterface(QObject *parent = nullptr);

public:
    ~SlaveInterface() override;

    // Send our answer to the MSG_RESUME (canResume) request
    // (to tell the "put" job whether to resume or not)
    void sendResumeAnswer(bool resume);

    /**
     * Sends our answer for the INF_MESSAGEBOX request.
     *
     * @since 4.11
     */
    void sendMessageBoxAnswer(int result);

    void setOffset(KIO::filesize_t offset);
    KIO::filesize_t offset() const;

Q_SIGNALS:
    ///////////
    // Messages sent by the slave
    ///////////

    void data(const QByteArray &);
    void dataReq();
    void error(int, const QString &);
    void connected();
    void finished();
    void slaveStatus(qint64, const QByteArray &, const QString &, bool);
    void listEntries(const KIO::UDSEntryList &);
    void statEntry(const KIO::UDSEntry &);
    void needSubUrlData();

    void canResume(KIO::filesize_t);

    void open();
    void written(KIO::filesize_t);
    void close();

    void privilegeOperationRequested();

    ///////////
    // Info sent by the slave
    //////////
    void metaData(const KIO::MetaData &);
    void totalSize(KIO::filesize_t);
    void processedSize(KIO::filesize_t);
    void redirection(const QUrl &);
    void position(KIO::filesize_t);
    void truncated(KIO::filesize_t);

    void speed(unsigned long);
    void errorPage();
    void mimeType(const QString &);
    void warning(const QString &);
    void infoMessage(const QString &);
    // void connectFinished(); //it does not get emitted anywhere

protected:
    /////////////////
    // Dispatching
    ////////////////

    virtual bool dispatch();
    virtual bool dispatch(int _cmd, const QByteArray &data);

    void messageBox(int type, const QString &text, const QString &title, const QString &primaryActionText, const QString &secondaryActionText);

    void messageBox(int type,
                    const QString &text,
                    const QString &title,
                    const QString &primaryActionText,
                    const QString &secondaryActionText,
                    const QString &dontAskAgainName);

protected Q_SLOTS:
    void calcSpeed();

private Q_SLOTS:
    void slotHostInfo(const QHostInfo &info);

protected:
    Connection *m_connection = nullptr;

    // We need some metadata here for our SSL code in messageBox() and for sslMetaData().
    MetaData m_sslMetaData;

private:
    QTimer m_speed_timer;

    // Used to cache privilege operation details passed from the worker by the metadata hack
    // See WORKER_MESSAGEBOX_DETAILS_HACK
    QString m_messageBoxDetails;

    static const unsigned int max_nums = 8;
    KIO::filesize_t m_sizes[max_nums];
    qint64 m_times[max_nums];

    KIO::filesize_t m_filesize = 0;
    KIO::filesize_t m_offset = 0;
    size_t m_last_time = 0;
    qint64 m_start_time = 0;
    uint m_nums = 0;
    bool m_slave_calcs_speed = false;
};

}

#endif
