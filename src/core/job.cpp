/* This file is part of the KDE libraries
    Copyright (C) 2000 Stephan Kulow <coolo@kde.org>
                  2000-2009 David Faure <faure@kde.org>
                       Waldo Bastian <bastian@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "job.h"
#include "job_p.h"

#include <time.h>

#include <QFile>
#include <QLinkedList>

#include <klocalizedstring.h>
#include <kstringhandler.h>

#include <kio/jobuidelegateextension.h>
#include "slave.h"
#include "scheduler.h"
#include "slavebase.h"

using namespace KIO;

Job::Job() : KCompositeJob(nullptr)
    , d_ptr(new JobPrivate)
{
    d_ptr->q_ptr = this;
    setCapabilities(KJob::Killable | KJob::Suspendable);
}

Job::Job(JobPrivate &dd) : KCompositeJob(nullptr)
    , d_ptr(&dd)
{
    d_ptr->q_ptr = this;
    setCapabilities(KJob::Killable | KJob::Suspendable);
}

Job::~Job()
{
    delete d_ptr;
}

// Exists for historical reasons only
KJobUiDelegate *Job::ui() const
{
    return uiDelegate();
}

JobUiDelegateExtension *Job::uiDelegateExtension() const
{
    Q_D(const Job);
    return d->m_uiDelegateExtension;
}

void Job::setUiDelegateExtension(JobUiDelegateExtension *extension)
{
    Q_D(Job);
    d->m_uiDelegateExtension = extension;
}

bool Job::addSubjob(KJob *jobBase)
{
    //qDebug() << "addSubjob(" << jobBase << ") this=" << this;

    bool ok = KCompositeJob::addSubjob(jobBase);
    KIO::Job *job = dynamic_cast<KIO::Job *>(jobBase);
    if (ok && job) {
        // Copy metadata into subjob (e.g. window-id, user-timestamp etc.)
        Q_D(Job);
        job->mergeMetaData(d->m_outgoingMetaData);

        // Forward information from that subjob.
        connect(job, SIGNAL(speed(KJob*,ulong)),
                SLOT(slotSpeed(KJob*,ulong)));

        job->setProperty("window", property("window")); // see KJobWidgets
        job->setProperty("userTimestamp", property("userTimestamp")); // see KJobWidgets
        job->setUiDelegateExtension(d->m_uiDelegateExtension);
    }
    return ok;
}

bool Job::removeSubjob(KJob *jobBase)
{
    //qDebug() << "removeSubjob(" << jobBase << ") this=" << this << "subjobs=" << subjobs().count();
    return KCompositeJob::removeSubjob(jobBase);
}

static QString url_description_string(const QUrl& url)
{
    return url.scheme() == "data" ? QStringLiteral("data:[...]") :
                                    KStringHandler::csqueeze(url.toDisplayString(QUrl::PreferLocalFile), 100);
}

KIO::JobPrivate::~JobPrivate()
{
}

void JobPrivate::emitMoving(KIO::Job *job, const QUrl &src, const QUrl &dest)
{
    emit job->description(job, i18nc("@title job", "Moving"),
                          qMakePair(i18nc("The source of a file operation", "Source"), url_description_string(src)),
                          qMakePair(i18nc("The destination of a file operation", "Destination"), url_description_string(dest)));
}

void JobPrivate::emitCopying(KIO::Job *job, const QUrl &src, const QUrl &dest)
{
    emit job->description(job, i18nc("@title job", "Copying"),
                          qMakePair(i18nc("The source of a file operation", "Source"), url_description_string(src)),
                          qMakePair(i18nc("The destination of a file operation", "Destination"), url_description_string(dest)));
}

void JobPrivate::emitCreatingDir(KIO::Job *job, const QUrl &dir)
{
    emit job->description(job, i18nc("@title job", "Creating directory"),
                          qMakePair(i18n("Directory"), url_description_string(dir)));
}

void JobPrivate::emitDeleting(KIO::Job *job, const QUrl &url)
{
    emit job->description(job, i18nc("@title job", "Deleting"),
                          qMakePair(i18n("File"), url_description_string(url)));
}

void JobPrivate::emitStating(KIO::Job *job, const QUrl &url)
{
    emit job->description(job, i18nc("@title job", "Examining"),
                          qMakePair(i18n("File"), url_description_string(url)));
}

void JobPrivate::emitTransferring(KIO::Job *job, const QUrl &url)
{
    emit job->description(job, i18nc("@title job", "Transferring"),
                          qMakePair(i18nc("The source of a file operation", "Source"), url_description_string(url)));
}

void JobPrivate::emitMounting(KIO::Job *job, const QString &dev, const QString &point)
{
    emit job->description(job, i18nc("@title job", "Mounting"),
                          qMakePair(i18n("Device"), dev),
                          qMakePair(i18n("Mountpoint"), point));
}

void JobPrivate::emitUnmounting(KIO::Job *job, const QString &point)
{
    emit job->description(job, i18nc("@title job", "Unmounting"),
                          qMakePair(i18n("Mountpoint"), point));
}

bool Job::doKill()
{
    // kill all subjobs, without triggering their result slot
    Q_FOREACH (KJob *it, subjobs()) {
        it->kill(KJob::Quietly);
    }
    clearSubjobs();

    return true;
}

bool Job::doSuspend()
{
    Q_FOREACH (KJob *it, subjobs()) {
        if (!it->suspend()) {
            return false;
        }
    }

    return true;
}

bool Job::doResume()
{
    Q_FOREACH (KJob *it, subjobs()) {
        if (!it->resume()) {
            return false;
        }
    }

    return true;
}

void JobPrivate::slotSpeed(KJob *, unsigned long speed)
{
    //qDebug() << speed;
    q_func()->emitSpeed(speed);
}

//Job::errorString is implemented in job_error.cpp

void Job::setParentJob(Job *job)
{
    Q_D(Job);
    Q_ASSERT(d->m_parentJob == nullptr);
    Q_ASSERT(job);
    d->m_parentJob = job;
}

Job *Job::parentJob() const
{
    return d_func()->m_parentJob;
}

MetaData Job::metaData() const
{
    return d_func()->m_incomingMetaData;
}

QString Job::queryMetaData(const QString &key)
{
    return d_func()->m_incomingMetaData.value(key, QString());
}

void Job::setMetaData(const KIO::MetaData &_metaData)
{
    Q_D(Job);
    d->m_outgoingMetaData = _metaData;
}

void Job::addMetaData(const QString &key, const QString &value)
{
    d_func()->m_outgoingMetaData.insert(key, value);
}

void Job::addMetaData(const QMap<QString, QString> &values)
{
    Q_D(Job);
    QMap<QString, QString>::const_iterator it = values.begin();
    for (; it != values.end(); ++it) {
        d->m_outgoingMetaData.insert(it.key(), it.value());
    }
}

void Job::mergeMetaData(const QMap<QString, QString> &values)
{
    Q_D(Job);
    QMap<QString, QString>::const_iterator it = values.begin();
    for (; it != values.end(); ++it)
        // there's probably a faster way
        if (!d->m_outgoingMetaData.contains(it.key())) {
            d->m_outgoingMetaData.insert(it.key(), it.value());
        }
}

MetaData Job::outgoingMetaData() const
{
    return d_func()->m_outgoingMetaData;
}

PrivilegeOperationStatus JobPrivate::tryAskPrivilegeOpConfirmation()
{
    if (m_confirmationAsked) {
        return OperationAllowed;
    }

    if (m_parentJob) {
        if (!m_parentJob->d_func()->m_privilegeExecutionEnabled) {
            return OperationNotAllowed;
        }

        if (!m_parentJob->d_func()->m_confirmationAsked) {
            PrivilegeOperationStatus opStatus = m_parentJob->d_func()->tryAskPrivilegeOpConfirmation();
            if (opStatus == OperationAllowed) {
                // Copy meta-data from parent job
                m_incomingMetaData.insert(QStringLiteral("TestData"), m_parentJob->queryMetaData(QStringLiteral("TestData")));
                m_confirmationAsked = true;
            }
            return opStatus;
        } else {
            return OperationAllowed;
        }
    } else {
        // In case of SimpleJob like chmod, chown, etc. which don't accept JobFlags
        if (!m_privilegeExecutionEnabled) {
            return OperationNotAllowed;
        }
    }

    switch (m_operationType) {
    case ChangeAttr:
        m_caption = i18n("Change Attribute");
        m_message = i18n("Root privileges are required to change file attributes. "
                         "Do you want to continue?");
        break;
    case Copy:
        m_caption = i18n("Copy Files");
        m_message = i18n("Root privileges are required to complete the copy operation. "
                         "Do you want to continue?");
        break;
    case Delete:
        m_caption = i18n("Delete Files");
        m_message = i18n("Root privileges are required to complete the delete operation. "
                         "However, doing so may damage your system. Do you want to continue?");
        break;
    case MkDir:
        m_caption = i18n("Create Folder");
        m_message = i18n("Root privileges are required to create this folder. "
                         "Do you want to continue?");
        break;
    case Move:
        m_caption = i18n("Move Items");
        m_message = i18n("Root privileges are required to complete the move operation. "
                         "Do you want to continue?");
        break;
    case Rename:
        m_caption = i18n("Rename");
        m_message = i18n("Root privileges are required to complete renaming. "
                         "Do you want to continue?");
        break;
    case Symlink:
        m_caption = i18n("Create Symlink");
        m_message = i18n("Root privileges are required to create a symlink. "
                         "Do you want to continue?");
        break;
    case Transfer:
        m_caption = i18n("Transfer data");
        m_message = i18n("Root privileges are required to complete transferring data. "
                         "Do you want to continue?");
    default:
        break;
    }

    if (m_outgoingMetaData.value(QStringLiteral("UnitTesting")) == QLatin1String("true")) {
        // Set meta-data for the top-level job
        m_incomingMetaData.insert(QStringLiteral("TestData"), QStringLiteral("PrivilegeOperationAllowed"));
        return OperationAllowed;
    }

    if (!m_uiDelegateExtension) {
        return OperationNotAllowed;
    }

    int status = m_uiDelegateExtension->requestMessageBox(JobUiDelegateExtension::WarningContinueCancel,
                                                          m_message, m_caption, i18n("Continue"), i18n("Cancel"));
    m_confirmationAsked = true;

    if (status == SlaveBase::Cancel) {
        return OperationCanceled;
    }
    return OperationAllowed;
}

//////////////////////////

class KIO::DirectCopyJobPrivate: public KIO::SimpleJobPrivate
{
public:
    DirectCopyJobPrivate(const QUrl &url, int command, const QByteArray &packedArgs)
        : SimpleJobPrivate(url, command, packedArgs)
    {}

    /**
     * @internal
     * Called by the scheduler when a @p slave gets to
     * work on this job.
     * @param slave the slave that starts working on this job
     */
    void start(Slave *slave) Q_DECL_OVERRIDE;

    Q_DECLARE_PUBLIC(DirectCopyJob)
};

DirectCopyJob::DirectCopyJob(const QUrl &url, const QByteArray &packedArgs)
    : SimpleJob(*new DirectCopyJobPrivate(url, CMD_COPY, packedArgs))
{
    setUiDelegate(KIO::createDefaultJobUiDelegate());
}

DirectCopyJob::~DirectCopyJob()
{
}

void DirectCopyJobPrivate::start(Slave *slave)
{
    Q_Q(DirectCopyJob);
    q->connect(slave, SIGNAL(canResume(KIO::filesize_t)),
               SLOT(slotCanResume(KIO::filesize_t)));
    SimpleJobPrivate::start(slave);
}

void DirectCopyJob::slotCanResume(KIO::filesize_t offset)
{
    emit canResume(this, offset);
}

//////////////////////////

SimpleJob *KIO::file_delete(const QUrl &src, JobFlags flags)
{
    KIO_ARGS << src << qint8(true); // isFile
    SimpleJob *job = SimpleJobPrivate::newJob(src, CMD_DEL, packedArgs, flags);
    if (job->uiDelegateExtension()) {
        job->uiDelegateExtension()->createClipboardUpdater(job, JobUiDelegateExtension::RemoveContent);
    }
    return job;
}

//////////
////

#include "moc_job_base.cpp"
#include "moc_job_p.cpp"
