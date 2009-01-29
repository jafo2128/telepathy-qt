/*
 * This file is part of TelepathyQt4
 *
 * Copyright (C) 2008-2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _TelepathyQt4_cli_contact_manager_h_HEADER_GUARD_
#define _TelepathyQt4_cli_contact_manager_h_HEADER_GUARD_

#ifndef IN_TELEPATHY_QT4_HEADER
#error IN_TELEPATHY_QT4_HEADER
#endif

#include <QSet>

#include <TelepathyQt4/Types>
#include <TelepathyQt4/Client/Contact>

namespace Telepathy
{
namespace Client
{

class Connection;
class Contact;
class PendingContacts;

class ContactManager
{
    public:

        Connection *connection() const;

        bool isSupported() const;

        PendingContacts *contactsForHandles(const UIntList &handles,
                const QSet<Contact::Feature> &features = QSet<Contact::Feature>());
        PendingContacts *contactsForHandles(const ReferencedHandles &handles,
                const QSet<Contact::Feature> &features = QSet<Contact::Feature>());

        PendingContacts *contactsForIdentifiers(const QStringList &identifiers,
                const QSet<Contact::Feature> &features = QSet<Contact::Feature>());

    private:
        ContactManager(Connection *parent);
        ~ContactManager();

        struct Private;
        friend struct Private;
        friend class Connection;
        Private *mPriv;
};

} // Telepathy::Client
} // Telepathy

#endif
