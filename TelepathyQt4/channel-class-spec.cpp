/*
 * This file is part of TelepathyQt4
 *
 * Copyright (C) 2010 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2010 Nokia Corporation
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

#include <TelepathyQt4/ChannelClassSpec>

#include "TelepathyQt4/debug-internal.h"

namespace Tp
{

struct TELEPATHY_QT4_NO_EXPORT ChannelClassSpec::Private : public QSharedData
{
    Private(const QVariantMap &props = QVariantMap())
        : props(props) {}

    QVariantMap props;
};

ChannelClassSpec::ChannelClassSpec()
{
}

ChannelClassSpec::ChannelClassSpec(const ChannelClass &cc)
    : mPriv(new Private)
{
    foreach (QString key, cc.keys()) {
        setProperty(key, cc.value(key).variant());
    }
}

ChannelClassSpec::ChannelClassSpec(const QString &channelType, uint targetHandleType,
        const QVariantMap &otherProperties)
    : mPriv(new Private(otherProperties))
{
    setChannelType(channelType);
    setTargetHandleType(targetHandleType);
}

ChannelClassSpec::ChannelClassSpec(const QString &channelType, uint targetHandleType, bool requested,
        const QVariantMap &otherProperties)
    : mPriv(new Private(otherProperties))
{
    setChannelType(channelType);
    setTargetHandleType(targetHandleType);
    setRequested(requested);
}

ChannelClassSpec::ChannelClassSpec(const ChannelClassSpec &other,
        const QVariantMap &additionalProperties)
    : mPriv(other.mPriv)
{
    foreach (QString key, additionalProperties.keys()) {
        setProperty(key, additionalProperties.value(key));
    }
}

ChannelClassSpec::~ChannelClassSpec()
{
}

bool ChannelClassSpec::isValid() const
{
    return mPriv.constData() != 0 && 
        !(qdbus_cast<QString>(
                    mPriv->props.value(QLatin1String(TELEPATHY_INTERFACE_CHANNEL ".ChannelType")))
                .isEmpty()) &&
        mPriv->props.contains(QLatin1String(TELEPATHY_INTERFACE_CHANNEL ".TargetHandleType"));
}

ChannelClassSpec &ChannelClassSpec::operator=(const ChannelClassSpec &other)
{
    if (this == &other) {
        return *this;
    }

    this->mPriv = other.mPriv;
    return *this;
}

bool ChannelClassSpec::isSubsetOf(const ChannelClassSpec &other) const
{
    if (!isValid() || !other.isValid()) {
        warning() << "ChannelClassSpec comparison attempted for an invalid ChannelClassSpec";
        return false;
    }

    Q_ASSERT(QVariant::fromValue(ushort(5)) == QVariant::fromValue(int(5)));

    foreach (QString propName, mPriv->props.keys()) {
        if (!other.hasProperty(propName)) {
            return false;
        } else if (property(propName) != other.property(propName)) {
            return false;
        }
    }

    // other had all of the properties we have and they all had the same values

    return true;
}

bool ChannelClassSpec::matches(const QVariantMap &immutableProperties) const
{
    // We construct a ChannelClassSpec for comparison so the StreamedMedia props are normalized
    // consistently etc
    ChannelClassSpec other;
    foreach (QString propName, immutableProperties.keys()) {
        other.setProperty(propName, immutableProperties.value(propName));
    }
    return this->isSubsetOf(other);
}

bool ChannelClassSpec::hasProperty(const QString &qualifiedName) const
{
    return mPriv.constData() != 0 ? mPriv->props.contains(qualifiedName) : false;
}

QVariant ChannelClassSpec::property(const QString &qualifiedName) const
{
    return mPriv.constData() != 0 ? mPriv->props.value(qualifiedName) : QVariant();
}

void ChannelClassSpec::setProperty(const QString &qualifiedName, const QVariant &value)
{
    if (mPriv.constData() == 0) {
        mPriv = new Private;
    }

    // Flatten the InitialAudio/Video properties from the different media interfaces to one
    // namespace - we convert back to the correct interface when this is converted back to a
    // ChannelClass for use in e.g. client channel filters

    QString propName = qualifiedName;

    if (propName ==
            QLatin1String("org.freedesktop.Telepathy.Channel.Type.Call.DRAFT.InitialAudio")) {
        propName.replace(QLatin1String("Call.DRAFT"), QLatin1String("StreamedMedia"));
    } else if (propName ==
            QLatin1String("org.freedesktop.Telepathy.Channel.Type.Call.DRAFT.InitialVideo")) {
        propName.replace(QLatin1String("Call.DRAFT"), QLatin1String("StreamedMedia"));
    }
    // TODO add the corresponding non-draft Call properties when the interface is undrafted

    mPriv->props.insert(propName, value);
}

void ChannelClassSpec::unsetProperty(const QString &qualifiedName)
{
    if (mPriv.constData() == 0) {
        // No properties set for sure, so don't have to unset any
        return;
    }

    QString propName = qualifiedName;

    if (propName ==
            QLatin1String("org.freedesktop.Telepathy.Channel.Type.Call.DRAFT.InitialAudio")) {
        propName.replace(QLatin1String("Call.DRAFT"), QLatin1String("StreamedMedia"));
    } else if (propName ==
            QLatin1String("org.freedesktop.Telepathy.Channel.Type.Call.DRAFT.InitialVideo")) {
        propName.replace(QLatin1String("Call.DRAFT"), QLatin1String("StreamedMedia"));
    }
    // TODO add the corresponding non-draft Call properties when the interface is undrafted

    mPriv->props.remove(propName);
}

QVariantMap ChannelClassSpec::allProperties() const
{
    return mPriv.constData() != 0 ? mPriv->props : QVariantMap();
}

ChannelClass ChannelClassSpec::bareClass() const
{
    ChannelClass cc;

    if (!isValid()) {
        warning() << "Tried to convert an invalid ChannelClassSpec to a ChannelClass";
        return ChannelClass();
    }

    QVariantMap props = mPriv->props;
    foreach (QString propName, props.keys()) {
        QVariant value = props.value(propName);

        if (channelType() == QLatin1String("org.freedesktop.Telepathy.Channel.Type.Call.DRAFT")) {
            if (propName.endsWith(QLatin1String(".InitialAudio"))
                    || propName.endsWith(QLatin1String(".InitialVideo"))) {
                propName.replace(QLatin1String("StreamedMedia"), QLatin1String("Call.DRAFT"));
            }
        }
        // TODO add the corresponding non-draft Call properties when the interface is undrafted

        cc.insert(propName, QDBusVariant(value));
    }

    return cc;
}

ChannelClassSpec ChannelClassSpec::text(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_TEXT),
                static_cast<uint>(HandleTypeContact));
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

ChannelClassSpec ChannelClassSpec::textChatroom(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_TEXT),
                static_cast<uint>(HandleTypeRoom));
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

ChannelClassSpec ChannelClassSpec::media(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_STREAMED_MEDIA),
                static_cast<uint>(HandleTypeContact));
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

ChannelClassSpec ChannelClassSpec::mediaWithInitialAudio(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_STREAMED_MEDIA),
                static_cast<uint>(HandleTypeContact));
        spec.setInitialAudio();
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

ChannelClassSpec ChannelClassSpec::mediaWithInitialVideo(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_STREAMED_MEDIA),
                static_cast<uint>(HandleTypeContact));
        spec.setInitialVideo();
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

ChannelClassSpec ChannelClassSpec::roomList(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_ROOM_LIST),
                static_cast<uint>(HandleTypeNone));
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

ChannelClassSpec ChannelClassSpec::sendFile(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_FILE_TRANSFER),
                static_cast<uint>(HandleTypeContact));
        spec.setRequested(true);
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

ChannelClassSpec ChannelClassSpec::receiveFile(const QVariantMap &additionalProperties)
{
    static ChannelClassSpec spec;

    if (!spec.isValid()) {
        spec = ChannelClassSpec(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_FILE_TRANSFER),
                static_cast<uint>(HandleTypeContact));
        spec.setRequested(false);
    }

    if (additionalProperties.isEmpty()) {
        return spec;
    } else {
        return ChannelClassSpec(spec, additionalProperties);
    }
}

} // Tp
