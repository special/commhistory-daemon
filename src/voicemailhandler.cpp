/******************************************************************************
**
** This file is part of commhistory-daemon.
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Reto Zingg <reto.zingg@nokia.com>
**
** This library is free software; you can redistribute it and/or modify it
** under the terms of the GNU Lesser General Public License version 2.1 as
** published by the Free Software Foundation.
**
** This library is distributed in the hope that it will be useful, but
** WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
** or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
** License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this library; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
**
******************************************************************************/

// Qt includes
#include <QCoreApplication>
#include <QDir>
#include <QFileSystemWatcher>

#include <CommHistory/commonutils.h>

// contacts
#include <QContactManager>
#include <QContactFetchRequest>
#include <QContactDetailFilter>
#include <QContactPhoneNumber>
#include <QContactGuid>

// Our includes
#include "voicemailhandler.h"
#include "notificationmanager.h"
#include "locstrings.h"
#include "constants.h"
#include "debug.h"

using namespace RTComLogger;

// P U B L I C  M E T H O D S

VoiceMailHandler* VoiceMailHandler::instance()
{
    DEBUG() << Q_FUNC_INFO;

    static QPointer<VoiceMailHandler> instance;

    if (instance.isNull()) {
        instance = new VoiceMailHandler();
        instance.data()->init();
    }

    return instance.data();
}

bool VoiceMailHandler::isVoiceMailNumber(QString phoneNumber)
{
    bool match = false;

    foreach (QString numberInStore, m_voiceMailPhoneNumbers) {
        if (CommHistory::remoteAddressMatch(numberInStore, phoneNumber)) {
            DEBUG() << Q_FUNC_INFO << "MATCH: " << numberInStore << " : " << phoneNumber;
            match = true;
            break;
        }
    }
    return match;
}

bool VoiceMailHandler::isVoiceMailContact(const QContactId &id)
{
    return (m_contactId == id);
}

void VoiceMailHandler::fetchVoiceMailContact()
{
    DEBUG() << Q_FUNC_INFO;

    QContactDetailFilter filter;
    filter.setDetailType(QContactGuid::Type, QContactGuid::FieldGuid);
    filter.setValue(VOICEMAIL_CONTACT_GUID);

    QList<QContactDetail::DetailType> details;
    details << QContactPhoneNumber::Type;

    startContactRequest(filter, details, SLOT(slotVoiceMailContactsAvailable()));
}

void VoiceMailHandler::clear()
{
    DEBUG() << Q_FUNC_INFO;

    m_contactId = QContactId();
    m_voiceMailPhoneNumbers.clear();
}

QContactId VoiceMailHandler::voiceMailContactId()
{
    DEBUG() << Q_FUNC_INFO;

    return m_contactId;
}

// P R I V A T E  M E T H O D S

// constructor
//
VoiceMailHandler::VoiceMailHandler()
        : QObject(QCoreApplication::instance())
        , m_pVoiceMailDirWatcher(0)
{
    DEBUG() << Q_FUNC_INFO;
}

// destructor
//
VoiceMailHandler::~VoiceMailHandler()
{
    DEBUG() << Q_FUNC_INFO;
    delete m_pVoiceMailDirWatcher;
}

void VoiceMailHandler::init()
{
    DEBUG() << Q_FUNC_INFO;

    QDir mainDir = QDir(VOICEMAIL_CONTACT_VMID_MAIN);
    if (!mainDir.exists(VOICEMAIL_CONTACT_VMID_DIR)) {
        // If contacts directory does not exist then create it so that we can monitor its changes:
        if (!mainDir.mkdir(VOICEMAIL_CONTACT_VMID_DIR)) {
            qWarning() << "Creation of " << VOICEMAIL_CONTACT_VMID_MAIN << "/" << VOICEMAIL_CONTACT_VMID_DIR << " failed!";
        }
    }

    QString voiceMailDir = QString("%1/%2").arg(VOICEMAIL_CONTACT_VMID_MAIN).arg(VOICEMAIL_CONTACT_VMID_DIR);
    QString voiceMailFile = QString("%1/%2").arg(voiceMailDir).arg(VOICEMAIL_CONTACT_VMID_FILE);

    // Create watcher for /dev/shm/contacts directory:
    QStringList watchedPaths;
    watchedPaths << voiceMailDir;
    m_pVoiceMailDirWatcher = new QFileSystemWatcher(watchedPaths);
    startObservingVmcFile();

    // If vmid file already exists in /dev/shm/contacts directory then add it to watcher:
    if (QFile::exists(voiceMailFile)) {
        DEBUG() << Q_FUNC_INFO << "Voice mail file " << voiceMailFile << " exists. Start monitoring it.";
        m_pVoiceMailDirWatcher->addPath(voiceMailFile);
    }

    m_pContactManager = NotificationManager::instance()->contactManager();

    fetchVoiceMailContact();
}

QContactFetchRequest* VoiceMailHandler::startContactRequest(QContactFilter &filter,
                                                            const QList<QContactDetail::DetailType> &details,
                                                            const char *resultSlot)
{
    DEBUG() << Q_FUNC_INFO;

    QContactFetchRequest* request = 0;

    if (!m_pContactManager.isNull()) {
        request = new QContactFetchRequest();
        request->setManager(m_pContactManager.data());
        request->setParent(this);

        connect(request,
                SIGNAL(resultsAvailable()),
                resultSlot);

        request->setFilter(filter);

        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        hint.setDetailTypesHint(details);

        request->setFetchHint(hint);
        request->start();
    }

    return request;
}

void VoiceMailHandler::startObservingVmcFile()
{
    DEBUG() << Q_FUNC_INFO;

    connect(m_pVoiceMailDirWatcher, SIGNAL(directoryChanged(QString)), SLOT(slotVoiceMailDirectoryChanged(QString)), Qt::UniqueConnection);
    connect(m_pVoiceMailDirWatcher, SIGNAL(fileChanged(QString)), SLOT(slotVoiceMailFileChanged(QString)), Qt::UniqueConnection);
}

// P R I V A T E  S L O T S

void VoiceMailHandler::slotVoiceMailContactsAvailable()
{
    DEBUG() << Q_FUNC_INFO;

    QContactFetchRequest *request = qobject_cast<QContactFetchRequest *>(sender());

    if(!request || !request->isFinished()) {
        return;
    }

    QList<QContact> contacts = request->contacts();

    DEBUG() << __PRETTY_FUNCTION__ << "Number of voice mail contacts returned: " << contacts.size();

    if (!contacts.isEmpty()) {
        // There should be just one voice mail contact (that can have multiple numbers).
        QContact voiceMailContact = contacts.first();
        if (!voiceMailContact.isEmpty()) {
            m_contactId = voiceMailContact.id();
            QList<QContactPhoneNumber> phoneNumbers = voiceMailContact.details<QContactPhoneNumber>();

            m_voiceMailPhoneNumbers.clear();

            foreach (QContactPhoneNumber phoneNumber, phoneNumbers) {
                m_voiceMailPhoneNumbers << phoneNumber.number();
            }
            DEBUG() << __PRETTY_FUNCTION__ << "Voice mail phone numbers are: " << m_voiceMailPhoneNumbers;

            // We have voice mail contact data now, we can disconnect listening vmc file and dir changes until vmc is removed.
            disconnect(m_pVoiceMailDirWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(slotVoiceMailDirectoryChanged(QString)));
            disconnect(m_pVoiceMailDirWatcher, SIGNAL(fileChanged(QString)), this, SLOT(slotVoiceMailFileChanged(QString)));
        }
    }

    request->deleteLater();
}

void VoiceMailHandler::slotVoiceMailFileChanged(QString path)
{
    Q_UNUSED(path)
    // File can change for example when new voice mail contact is added. File can exist also when there is no voice mail contact.
    DEBUG() << Q_FUNC_INFO << path;

    fetchVoiceMailContact();
}

void VoiceMailHandler::slotVoiceMailDirectoryChanged(QString path)
{
    Q_UNUSED(path)
    DEBUG() << Q_FUNC_INFO << path;

    QString voiceMailFile  = QString("%1/%2/%3").arg(VOICEMAIL_CONTACT_VMID_MAIN).arg(VOICEMAIL_CONTACT_VMID_DIR).arg(VOICEMAIL_CONTACT_VMID_FILE);

    bool vmIdFileFound = QFile::exists(voiceMailFile);

    if (vmIdFileFound) {
        DEBUG() << Q_FUNC_INFO << "Voicemail file exists.";
        // If voice mail file is not yet monitored:
        if (m_pVoiceMailDirWatcher->files().isEmpty()) {
            DEBUG() << Q_FUNC_INFO << "Start monitoring voicemail file.";
            m_pVoiceMailDirWatcher->addPath(voiceMailFile);
            fetchVoiceMailContact();
        }
        // else something else was changed in dir
    } else {
        // Voicemail file either removed or something else done with dir (other file added etc. although this should not happen AFAIK)
        DEBUG() << Q_FUNC_INFO << "Voicemail file not found, could have been removed manually, remove from file watcher.";
        m_pVoiceMailDirWatcher->removePath(voiceMailFile);
    }
}
