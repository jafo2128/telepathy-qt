/* StreamedMedia channel client-side proxy
 *
 * Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2009 Nokia Corporation
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

#include <TelepathyQt4/StreamedMediaChannel>
#include "TelepathyQt4/streamed-media-channel-internal.h"

#include "TelepathyQt4/_gen/streamed-media-channel.moc.hpp"
#include "TelepathyQt4/_gen/streamed-media-channel-internal.moc.hpp"

#include "TelepathyQt4/debug-internal.h"

#include <TelepathyQt4/Connection>
#include <TelepathyQt4/ContactManager>
#include <TelepathyQt4/PendingComposite>
#include <TelepathyQt4/PendingContacts>
#include <TelepathyQt4/PendingReady>
#include <TelepathyQt4/PendingVoid>

#include <QHash>

namespace Tp
{

/* ====== PendingMediaStreams ====== */

/**
 * \class PendingMediaStreams
 * \ingroup clientchannel
 * \headerfile TelepathyQt4/streamed-media-channel.h <TelepathyQt4/StreamedMediaChannel>
 *
 * \brief Class containing the result of an asynchronous media stream creation
 * request.
 *
 * Instances of this class cannot be constructed directly; the only way to get
 * one is via StreamedMediaChannel.
 *
 * See \ref async_model
 */

/**
 * Construct a new PendingMediaStreams object.
 *
 * \param channel StreamedMediaChannel to use.
 * \param contact The contact who the media stream is with.
 * \param types A list of stream types to request.
 */
PendingMediaStreams::PendingMediaStreams(const StreamedMediaChannelPtr &channel,
        const ContactPtr &contact,
        const QList<MediaStreamType> &types)
    : PendingOperation(0),
      mPriv(new Private(this, channel))
{
    mPriv->numContents = types.size();

    UIntList l;
    foreach (MediaStreamType type, types) {
        l << type;
    }
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(
            channel->streamedMediaInterface()->RequestStreams(
                contact->handle()[0], l), this);
    connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(gotSMStreams(QDBusPendingCallWatcher*)));
}

/**
 * Construct a new PendingMediaStreams object.
 *
 * \param channel StreamedMediaChannel to use.
 * \param types A list of stream types to request.
 */
PendingMediaStreams::PendingMediaStreams(const StreamedMediaChannelPtr &channel,
        const QList<MediaStreamType> &types)
    : PendingOperation(0),
      mPriv(new Private(this, channel))
{
    mPriv->numContents = types.size();

    foreach (MediaStreamType type, types) {
        QDBusPendingCallWatcher *watcher =
            new QDBusPendingCallWatcher(
                mPriv->callInterface()->AddContent(
                    QString(QLatin1String("%1 %2 %3"))
                        .arg(type == MediaStreamTypeAudio ?
                            QLatin1String("audio") : QLatin1String("video"))
                        .arg((qulonglong) this)
                        .arg(mPriv->contents.size()),
                    type), this);
        connect(watcher,
                SIGNAL(finished(QDBusPendingCallWatcher*)),
                SLOT(gotCallContent(QDBusPendingCallWatcher*)));
    }
}

/**
 * Class destructor.
 */
PendingMediaStreams::~PendingMediaStreams()
{
    delete mPriv;
}

/**
 * Return a list of the newly created MediaStreamPtr objects.
 *
 * \return A list of the MediaStreamPtr objects pointing to the newly created
 *         MediaStream objects, or an empty list if an error occurred.
 */
MediaStreams PendingMediaStreams::streams() const
{
    if (!isFinished()) {
        warning() << "PendingMediaStreams::streams called before finished, "
            "returning empty list";
        return MediaStreams();
    } else if (!isValid()) {
        warning() << "PendingMediaStreams::streams called when not valid, "
            "returning empty list";
        return MediaStreams();
    }

    MediaStreams ret;
    foreach (const MediaContentPtr &content, mPriv->contents) {
        ret << content->streams();
    }
    return ret;
}

void PendingMediaStreams::gotSMStreams(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<MediaStreamInfoList> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "StreamedMedia::RequestStreams()"
            " failed with " << reply.error().name() << ": " <<
            reply.error().message();
        setFinishedWithError(reply.error());
        watcher->deleteLater();
        return;
    }

    debug() << "Got reply to StreamedMedia::RequestStreams()";

    MediaStreamInfoList list = reply.value();
    StreamedMediaChannelPtr channel(mPriv->channel);
    foreach (const MediaStreamInfo &streamInfo, list) {
        MediaContentPtr content = channel->lookupContentBySMStreamId(
                streamInfo.identifier);
        if (!content) {
            content = channel->addContentForSMStream(streamInfo);
        } else {
            channel->onSMStreamDirectionChanged(streamInfo.identifier,
                    streamInfo.direction, streamInfo.pendingSendFlags);
            channel->onSMStreamStateChanged(streamInfo.identifier,
                    streamInfo.state);
        }
        mPriv->contents.append(content);
        connect(channel.data(),
                SIGNAL(contentRemoved(Tp::MediaContentPtr)),
                SLOT(onContentRemoved(Tp::MediaContentPtr)));
        connect(content->becomeReady(),
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(onContentReady(Tp::PendingOperation*)));
    }

    watcher->deleteLater();
}

void PendingMediaStreams::gotCallContent(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "StreamedMedia.AddContent failed with" <<
            reply.error().name() << ": " << reply.error().message();
        setFinishedWithError(reply.error());
        watcher->deleteLater();
        return;
    }


    QDBusObjectPath contentPath = reply.value();
    StreamedMediaChannelPtr channel(mPriv->channel);
    MediaContentPtr content = channel->lookupContentByCallObjectPath(
            contentPath);
    if (!content) {
        content = channel->addContentForCallObjectPath(contentPath);
    }

    connect(content->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onContentReady(Tp::PendingOperation*)));
    connect(channel.data(),
            SIGNAL(contentRemoved(Tp::MediaContentPtr)),
            SLOT(onContentRemoved(Tp::MediaContentPtr)));

    mPriv->contents.append(content);

    watcher->deleteLater();
}

void PendingMediaStreams::onContentRemoved(const MediaContentPtr &content)
{
    if (isFinished()) {
        return;
    }

    if (mPriv->contents.contains(content)) {
        // the content was removed before becoming ready
        setFinishedWithError(QLatin1String(TELEPATHY_ERROR_CANCELLED),
                QLatin1String("Content removed before ready"));
    }
}

void PendingMediaStreams::onContentReady(PendingOperation *op)
{
    if (isFinished()) {
        return;
    }

    if (op->isError()) {
        setFinishedWithError(op->errorName(), op->errorMessage());
        return;
    }

    mPriv->contentsReady++;
    if (mPriv->contentsReady == mPriv->numContents) {
        setFinished();
    }
}

/* ====== MediaStream ====== */
/**
 * \class MediaStream
 * \ingroup clientchannel
 * \headerfile TelepathyQt4/streamed-media-channel.h <TelepathyQt4/StreamedMediaChannel>
 *
 * \brief The MediaStream class provides an object representing a Telepathy
 * media stream.
 *
 * Instances of this class cannot be constructed directly; the only way to get
 * one is via StreamedMediaChannel.
 *
 * See \ref async_model
 */

MediaStream::Private::Private(MediaStream *parent,
        const MediaContentPtr &content,
        const MediaStreamInfo &streamInfo)
    : ifaceType(IfaceTypeStreamedMedia),
      parent(parent),
      readinessHelper(parent->readinessHelper()),
      content(content),
      SMId(streamInfo.identifier),
      SMContactHandle(streamInfo.contact),
      SMDirection(MediaStreamDirectionNone),
      SMPendingSend(0),
      SMState(MediaStreamStateDisconnected)
{
    ReadinessHelper::Introspectables introspectables;

    ReadinessHelper::Introspectable introspectableCore(
        QSet<uint>() << 0,                                                      // makesSenseForStatuses
        Features(),                                                             // dependsOnFeatures
        QStringList(),                                                          // dependsOnInterfaces
        (ReadinessHelper::IntrospectFunc) &Private::introspectSMContact,
        this);
    introspectables[FeatureCore] = introspectableCore;

    readinessHelper->addIntrospectables(introspectables);
    readinessHelper->becomeReady(FeatureCore);
}

MediaStream::Private::Private(MediaStream *parent,
        const MediaContentPtr &content,
        const QDBusObjectPath &objectPath)
    : ifaceType(IfaceTypeCall),
      parent(parent),
      readinessHelper(parent->readinessHelper()),
      content(content),
      callBaseInterface(0),
      callPropertiesInterface(0),
      callProxy(new CallProxy(this, parent)),
      callObjectPath(objectPath),
      buildingCallSenders(false),
      currentCallSendersChangedInfo(0)
{
    ReadinessHelper::Introspectables introspectables;

    ReadinessHelper::Introspectable introspectableCore(
        QSet<uint>() << 0,                                                      // makesSenseForStatuses
        Features(),                                                             // dependsOnFeatures
        QStringList(),                                                          // dependsOnInterfaces
        (ReadinessHelper::IntrospectFunc) &Private::introspectCallMainProperties,
        this);
    introspectables[FeatureCore] = introspectableCore;

    readinessHelper->addIntrospectables(introspectables);
    readinessHelper->becomeReady(FeatureCore);
}

void MediaStream::Private::introspectSMContact(MediaStream::Private *self)
{
    if (self->SMContactHandle == 0) {
        self->readinessHelper->setIntrospectCompleted(FeatureCore, true);
        return;
    }

    ContactManager *contactManager =
        self->parent->channel()->connection()->contactManager();
    self->parent->connect(
            contactManager->contactsForHandles(
                UIntList() << self->SMContactHandle),
            SIGNAL(finished(Tp::PendingOperation *)),
            SLOT(gotSMContact(Tp::PendingOperation *)));
}

PendingOperation *MediaStream::Private::updateSMDirection(
        bool send, bool receive)
{
    uint newSMDirection = 0;

    if (send) {
        newSMDirection |= MediaStreamDirectionSend;
    }

    if (receive) {
        newSMDirection |= MediaStreamDirectionReceive;
    }

    StreamedMediaChannelPtr chan(MediaContentPtr(content)->channel());
    return new PendingVoid(
            chan->streamedMediaInterface()->RequestStreamDirection(
                SMId, newSMDirection),
            parent);
}

MediaStream::SendingState MediaStream::Private::localSendingStateFromSMDirection()
{
    if (SMPendingSend & MediaStreamPendingLocalSend) {
        return SendingStatePendingSend;
    }
    if (SMDirection & MediaStreamDirectionSend) {
        return SendingStateSending;
    }
    return SendingStateNone;
}

MediaStream::SendingState MediaStream::Private::remoteSendingStateFromSMDirection()
{
    if (SMPendingSend & MediaStreamPendingRemoteSend) {
        return SendingStatePendingSend;
    }
    if (SMDirection & MediaStreamDirectionReceive) {
        return SendingStateSending;
    }
    return SendingStateNone;
}

void MediaStream::Private::introspectCallMainProperties(
        MediaStream::Private *self)
{
    MediaStream *parent = self->parent;
    StreamedMediaChannelPtr channel = parent->channel();

    self->callBaseInterface = new CallStreamInterface(
            channel->dbusConnection(), channel->busName(),
            self->callObjectPath.path(), parent);
    self->callProxy->connect(self->callBaseInterface,
            SIGNAL(SendersChanged(TpFuture::ContactSendingStateMap,TpFuture::UIntList)),
            SLOT(onCallSendersChanged(TpFuture::ContactSendingStateMap,TpFuture::UIntList)));

    self->callPropertiesInterface = new Client::DBus::PropertiesInterface(
            channel->dbusConnection(), channel->busName(),
            self->callObjectPath.path(), parent);
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(
                self->callPropertiesInterface->GetAll(
                    QLatin1String(TP_FUTURE_INTERFACE_CALL_STREAM)),
                parent);
    parent->connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(gotCallMainProperties(QDBusPendingCallWatcher*)));
}

void MediaStream::Private::processCallSendersChanged()
{
    if (buildingCallSenders) {
        return;
    }

    if (callSendersChangedQueue.isEmpty()) {
        if (!parent->isReady(FeatureCore)) {
            readinessHelper->setIntrospectCompleted(FeatureCore, true);
        }
        return;
    }

    currentCallSendersChangedInfo = callSendersChangedQueue.dequeue();

    QSet<uint> pendingSenders;
    for (TpFuture::ContactSendingStateMap::const_iterator i =
            currentCallSendersChangedInfo->updates.constBegin();
            i != currentCallSendersChangedInfo->updates.constEnd();
            ++i) {
        pendingSenders.insert(i.key());
    }

    if (!pendingSenders.isEmpty()) {
        buildingCallSenders = true;

        ContactManager *contactManager =
            parent->channel()->connection()->contactManager();
        PendingContacts *contacts = contactManager->contactsForHandles(
                pendingSenders.toList());
        parent->connect(contacts,
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(gotCallSendersContacts(Tp::PendingOperation*)));
    } else {
        if (callSendersChangedQueue.isEmpty()) {
            if (!parent->isReady(FeatureCore)) {
                readinessHelper->setIntrospectCompleted(FeatureCore, true);
            }
            return;
        } else {
            processCallSendersChanged();
        }
    }
}

void MediaStream::Private::CallProxy::onCallSendersChanged(
        const TpFuture::ContactSendingStateMap &updates,
        const TpFuture::UIntList &removed)
{
    if (updates.isEmpty() && removed.isEmpty()) {
        debug() << "Received Call.Content.SendersChanged with 0 changes and "
            "updates, skipping it";
        return;
    }

    debug() << "Received Call.Content.SendersChanged with" << updates.size() <<
        "and " << removed.size() << "removed";
    mPriv->callSendersChangedQueue.enqueue(
            new Private::CallSendersChangedInfo(
                updates, removed));
    mPriv->processCallSendersChanged();
}

/**
 * Feature representing the core that needs to become ready to make the
 * MediaStream object usable.
 *
 * Note that this feature must be enabled in order to use most MediaStream
 * methods. See specific methods documentation for more details.
 *
 * When calling isReady(), becomeReady(), this feature is implicitly added
 * to the requested features.
 */
const Feature MediaStream::FeatureCore = Feature(QLatin1String(MediaStream::staticMetaObject.className()), 0);

/**
 * Construct a new MediaStream object.
 *
 * \param content The content ownding this media stream.
 * \param streamInfo The stream info of this media stream.
 */
MediaStream::MediaStream(const MediaContentPtr &content,
        const MediaStreamInfo &streamInfo)
    : QObject(),
      ReadyObject(this, FeatureCore),
      mPriv(new Private(this, content, streamInfo))
{
    gotSMDirection(streamInfo.direction, streamInfo.pendingSendFlags);
    gotSMStreamState(streamInfo.state);
}

/**
 * Construct a new MediaStream object.
 *
 * \param content The content owning this media stream.
 * \param objectPath The object path of this media stream.
 */
MediaStream::MediaStream(const MediaContentPtr &content,
        const QDBusObjectPath &objectPath)
    : QObject(),
      ReadyObject(this, FeatureCore),
      mPriv(new Private(this, content, objectPath))
{
}

/**
 * Class destructor.
 */
MediaStream::~MediaStream()
{
    delete mPriv;
}

/**
 * Return the channel owning this media stream.
 *
 * \return The channel owning this media stream.
 */
StreamedMediaChannelPtr MediaStream::channel() const
{
    MediaContentPtr content(mPriv->content);
    return content->channel();
}

/**
 * Return the id of this media stream.
 *
 * \return An integer representing the media stream id.
 */
uint MediaStream::id() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->SMId;
    } else {
        return 0;
    }
}

/**
 * Return the contact who this media stream is with.
 *
 * \return The contact who this media stream is with.
 */
ContactPtr MediaStream::contact() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->SMContact;
    } else {
        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();

            if (handle != chanSelfHandle) {
                Q_ASSERT(mPriv->sendersContacts.contains(handle));
                return mPriv->sendersContacts[handle];
            }
        }
    }
    return ContactPtr();
}

/**
 * Return the state of this media stream.
 *
 * \return The state of this media stream.
 */
MediaStreamState MediaStream::state() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return (MediaStreamState) mPriv->SMState;
    } else {
        return MediaStreamStateConnected;
    }
}

/**
 * Return the type of this media stream.
 *
 * \return The type of this media stream.
 */
MediaStreamType MediaStream::type() const
{
    MediaContentPtr content(mPriv->content);
    return content->type();
}

/**
 * Return whether media is being sent on this media stream.
 *
 * \return \c true if media being sent on this media stream, \c false otherwise.
 * \sa localSendingStateChanged()
 */
bool MediaStream::sending() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->SMDirection & MediaStreamDirectionSend;
    } else {
        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle == chanSelfHandle) {
                return sendingState & SendingStateSending;
            }
        }
        return false;
    }
}

/**
 * Return whether media is being received on this media stream.
 *
 * \return \c true if media is being received on this media stream, \c false
 *         otherwise.
 * \sa remoteSendingStateChanged()
 */
bool MediaStream::receiving() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->SMDirection & MediaStreamDirectionReceive;
    } else {
        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle != chanSelfHandle &&
                sendingState & SendingStateSending) {
                return true;
            }
        }
        return false;
    }
}

/**
 * Return whether the local user has been asked to send media by the
 * remote user on this media stream.
 *
 * \return \c true if the local user has been asked to send media by the
 *         remote user on this media stream, \c false otherwise.
 * \sa localSendingStateChanged()
 */
bool MediaStream::localSendingRequested() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->SMPendingSend & MediaStreamPendingLocalSend;
    } else {
        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle == chanSelfHandle) {
                return sendingState & SendingStatePendingSend;
            }
        }
        return false;
    }
}

/**
 * Return whether the remote user has been asked to send media by the local
 * user on this media stream.
 *
 * \return \c true if the remote user has been asked to send media by the
 *         local user on this media stream, \c false otherwise.
 * \sa remoteSendingStateChanged()
 */
bool MediaStream::remoteSendingRequested() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->SMPendingSend & MediaStreamPendingRemoteSend;
    } else {
        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle != chanSelfHandle &&
                sendingState & SendingStatePendingSend) {
                return true;
            }
        }
        return false;
    }
}

/**
 * Return the direction of this media stream.
 *
 * \return The direction of this media stream.
 * \sa localSendingStateChanged(), remoteSendingStateChanged()
 */
MediaStreamDirection MediaStream::direction() const
{
   if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
       return (MediaStreamDirection) mPriv->SMDirection;
   } else {
       uint dir = MediaStreamDirectionNone;
       if (sending()) {
           dir |= MediaStreamDirectionSend;
       }
       if (receiving()) {
           dir |= MediaStreamDirectionReceive;
       }
       return (MediaStreamDirection) dir;
   }
}

/**
 * Return the pending send flags of this media stream.
 *
 * \return The pending send flags of this media stream.
 * \sa localSendingStateChanged()
 */
MediaStreamPendingSend MediaStream::pendingSend() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return (MediaStreamPendingSend) mPriv->SMPendingSend;
    }
    else {
        uint pending = 0;
        if (localSendingRequested()) {
            pending |= MediaStreamPendingLocalSend;
        }
        if (remoteSendingRequested()) {
            pending |= MediaStreamPendingRemoteSend;
        }
        return (MediaStreamPendingSend) pending;
    }
}

/**
 * Request a change in the direction of this media stream. In particular, this
 * might be useful to stop sending media of a particular type, or inform the
 * peer that you are no longer using media that is being sent to you.
 *
 * \param direction The new direction.
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa localSendingStateChanged(), remoteSendingStateChanged()
 */
PendingOperation *MediaStream::requestDirection(
        MediaStreamDirection direction)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return new PendingVoid(
                channel()->streamedMediaInterface()->RequestStreamDirection(
                    mPriv->SMId, direction),
                this);
    } else {
        QList<PendingOperation*> operations;

        operations.append(new PendingVoid(
                mPriv->callBaseInterface->SetSending(direction & MediaStreamDirectionSend),
                this));

        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            if (handle == chanSelfHandle) {
                continue;
            }
            operations.append(new PendingVoid(
                    mPriv->callBaseInterface->RequestReceiving(handle,
                            direction & MediaStreamDirectionReceive),
                    this));
        }

        return new PendingComposite(operations, this);
    }
}

/**
 * Start sending a DTMF tone on this media stream. Where possible, the tone will
 * continue until stopDTMFTone() is called. On certain protocols, it may only be
 * possible to send events with a predetermined length. In this case, the
 * implementation may emit a fixed-length tone, and the stopDTMFTone() method
 * call should return #TELEPATHY_ERROR_NOT_AVAILABLE.
 *
 * If the channel() does not support the #TELEPATHY_INTERFACE_CHANNEL_INTERFACE_DTMF
 * interface, the resulting PendingOperation will fail with error code
 * #TELEPATHY_ERROR_NOT_IMPLEMENTED.

 * \param event A numeric event code from the DTMFEvent enum.
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the request finishes.
 * \sa stopDTMFTone()
 */
PendingOperation *MediaStream::startDTMFTone(DTMFEvent event)
{
    if (mPriv->ifaceType != IfaceTypeStreamedMedia) {
        // TODO add Call iface support
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_IMPLEMENTED),
                QLatin1String("MediaStream does not have DTMF support"), this);
    }

    StreamedMediaChannelPtr chan(channel());
    if (!chan->interfaces().contains(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_INTERFACE_DTMF))) {
        warning() << "MediaStream::startDTMFTone() used with no dtmf interface";
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_IMPLEMENTED),
                QLatin1String("StreamedMediaChannel does not support dtmf interface"),
                this);
    }
    return new PendingVoid(
            chan->DTMFInterface()->StartTone(mPriv->SMId, event),
            this);
}

/**
 * Stop sending any DTMF tone which has been started using the startDTMFTone()
 * method. If there is no current tone, the resulting PendingOperation will
 * finish successfully.
 *
 * If continuous tones are not supported by this media stream, the resulting
 * PendingOperation will fail with error code #TELEPATHY_ERROR_NOT_AVAILABLE.
 *
 * If the channel() does not support the #TELEPATHY_INTERFACE_CHANNEL_INTERFACE_DTMF
 * interface, the resulting PendingOperation will fail with error code
 * #TELEPATHY_ERROR_NOT_IMPLEMENTED.
 *
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the request finishes.
 * \sa startDTMFTone()
 */
PendingOperation *MediaStream::stopDTMFTone()
{
    if (mPriv->ifaceType != IfaceTypeStreamedMedia) {
        // TODO add Call iface support
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_IMPLEMENTED),
                QLatin1String("MediaStream does not have DTMF support"), this);
    }

    StreamedMediaChannelPtr chan(channel());
    if (!chan->interfaces().contains(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_INTERFACE_DTMF))) {
        warning() << "MediaStream::stopDTMFTone() used with no dtmf interface";
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_IMPLEMENTED),
                QLatin1String("StreamedMediaChannel does not support dtmf interface"),
                this);
    }
    return new PendingVoid(
            chan->DTMFInterface()->StopTone(mPriv->SMId),
            this);
}

/**
 * Request a change in the direction of this media stream. In particular, this
 * might be useful to stop sending media of a particular type, or inform the
 * peer that you are no longer using media that is being sent to you.
 *
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa requestDirection(Tp::MediaStreamDirection direction)
 * \sa localSendingStateChanged(), remoteSendingStateChanged()
 */
PendingOperation *MediaStream::requestDirection(bool send, bool receive)
{
    uint dir = MediaStreamDirectionNone;
    if (send) {
        dir |= MediaStreamDirectionSend;
    }
    if (receive) {
        dir |= MediaStreamDirectionReceive;
    }

    return requestDirection((MediaStreamDirection) dir);
}

/**
 * Return the content owning this media stream.
 *
 * \deprecated
 *
 * \return The content owning this media stream.
 */
MediaContentPtr MediaStream::content() const
{
    return _deprecated_content();
}

MediaContentPtr MediaStream::_deprecated_content() const
{
    return MediaContentPtr(mPriv->content);
}

/**
 * Return the contacts whose the media stream is with.
 *
 * \deprecated Use contact() instead.
 *
 * \return The contacts whose the media stream is with.
 * \sa membersRemoved()
 */
Contacts MediaStream::members() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return Contacts() << mPriv->SMContact;
    } else {
        return mPriv->sendersContacts.values().toSet();
    }
}

/**
 * Return the media stream local sending state.
 *
 * \return The media stream local sending state.
 * \sa localSendingStateChanged()
 */
MediaStream::SendingState MediaStream::localSendingState() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->localSendingStateFromSMDirection();
    } else {
        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle == chanSelfHandle) {
                return sendingState;
            }
        }
    }
    return SendingStateNone;
}

/**
 * Return the media stream remote sending state for a given \a contact.
 *
 * \deprecated Use remoteSendingState() instead.
 *
 * \return The media stream remote sending state for a contact.
 * \sa remoteSendingStateChanged()
 */
MediaStream::SendingState MediaStream::remoteSendingState(
        const ContactPtr &contact) const
{
    if (!contact) {
        return SendingStateNone;
    }

    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        if (mPriv->SMContact == contact) {
            return mPriv->remoteSendingStateFromSMDirection();
        }
    } else {
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle == contact->handle()[0]) {
                return sendingState;
            }
        }
    }

    return SendingStateNone;
}

/**
 * Return the media stream remote sending state.
 *
 * \return The media stream remote sending state.
 * \sa remoteSendingStateChanged()
 */
MediaStream::SendingState MediaStream::remoteSendingState() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->remoteSendingStateFromSMDirection();
    } else {
        uint chanSelfHandle = channel()->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle != chanSelfHandle) {
                return sendingState;
            }
        }
    }

    return SendingStateNone;
}

/**
 * Request that media starts or stops being sent on this media stream.
 *
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa localSendingStateChanged()
 */
PendingOperation *MediaStream::requestSending(bool send)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->updateSMDirection(
                send,
                mPriv->SMDirection & MediaStreamDirectionReceive);
    } else {
        return new PendingVoid(
                mPriv->callBaseInterface->SetSending(send),
                this);
    }
}

/**
 * Request that a remote \a contact stops or starts sending on this media stream.
 *
 * \deprecated Use requestReceiving(bool receive) instead.
 *
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa remoteSendingStateChanged()
 */
PendingOperation *MediaStream::requestReceiving(const ContactPtr &contact,
        bool receive)
{
    if (!contact) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_INVALID_ARGUMENT),
                QLatin1String("Invalid contact"), this);
    }

    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        if (mPriv->SMContact != contact) {
            return new PendingFailure(QLatin1String(TELEPATHY_ERROR_INVALID_ARGUMENT),
                    QLatin1String("Contact is not a member of the stream"),
                    this);
        }

        return mPriv->updateSMDirection(
                mPriv->SMDirection & MediaStreamDirectionSend,
                receive);
    }
    else {
        return new PendingVoid(mPriv->callBaseInterface->RequestReceiving(
                    contact->handle()[0], receive), this);
    }
}

/**
 * Request that the remote contact stops or starts sending on this media stream.
 *
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa remoteSendingStateChanged()
 */
PendingOperation *MediaStream::requestReceiving(bool receive)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return mPriv->updateSMDirection(
                mPriv->SMDirection & MediaStreamDirectionSend,
                receive);
    }
    else {
        uint chanSelfHandle = channel()->groupSelfContact()->handle()[0];
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->senders.constBegin();
                i != mPriv->senders.constEnd();
                ++i) {
            uint handle = i.key();

            if (handle != chanSelfHandle) {
                return new PendingVoid(mPriv->callBaseInterface->RequestReceiving(
                        handle, receive), this);
            }
        }

        return new PendingFailure(TP_QT4_ERROR_NOT_AVAILABLE,
                QLatin1String("No remote contact"), this);
    }
}

/**
 * \fn void MediaStream::localSendingStateChanged(Tp::MediaStream::SendingState localSendingState);
 *
 * This signal is emitted when the local sending state of this media stream
 * changes.
 *
 * \param localSendingState The new local sending state of this media stream.
 * \sa localSendingState()
 */

/**
 * \fn void MediaStream::remoteSendingStateChanged(const QHash<Tp::ContactPtr, Tp::MediaStream::SendingState> &remoteSendingStates);
 *
 * This signal is emitted when any remote sending state of this media stream
 * changes.
 *
 * \deprecated Use remoteSendingStateChanged(Tp::MediaStream::SendingState) instead.
 *
 * \param remoteSendingStates The new remote sending states of this media stream.
 * \sa remoteSendingState()
 */

/**
 * \fn void MediaStream::membersRemoved(const Tp::Contacts &members);
 *
 * This signal is emitted when one or more members of this stream are removed.
 *
 * \deprecated
 *
 * \param members The members that were removed from this media stream.
 * \sa members()
 */

void MediaStream::gotSMContact(PendingOperation *op)
{
    Q_ASSERT(mPriv->ifaceType == IfaceTypeStreamedMedia);

    PendingContacts *pc = qobject_cast<PendingContacts *>(op);
    Q_ASSERT(pc->isForHandles());

    if (op->isError()) {
        warning().nospace() << "Gathering media stream contact failed: "
            << op->errorName() << ": " << op->errorMessage();
        mPriv->readinessHelper->setIntrospectCompleted(FeatureCore, false,
                op->errorName(), op->errorMessage());
        return;
    }

    QList<ContactPtr> contacts = pc->contacts();
    UIntList invalidHandles = pc->invalidHandles();
    if (contacts.size()) {
        Q_ASSERT(contacts.size() == 1);
        Q_ASSERT(invalidHandles.size() == 0);
        mPriv->SMContact = contacts.first();
        mPriv->readinessHelper->setIntrospectCompleted(FeatureCore, true);
    } else {
        Q_ASSERT(invalidHandles.size() == 1);
        warning().nospace() << "Error retrieving media stream contact (invalid handle)";
        mPriv->readinessHelper->setIntrospectCompleted(FeatureCore, false,
                QLatin1String(TELEPATHY_ERROR_INVALID_ARGUMENT),
                QLatin1String("Invalid contact handle"));
    }
}

void MediaStream::gotSMDirection(uint direction, uint pendingSend)
{
    Q_ASSERT(mPriv->ifaceType == IfaceTypeStreamedMedia);

    if (direction == mPriv->SMDirection &&
        pendingSend == mPriv->SMPendingSend) {
        return;
    }

    mPriv->SMDirection = direction;
    mPriv->SMPendingSend = pendingSend;

    if (!isReady()) {
        return;
    }

    SendingState localSendingState =
        mPriv->localSendingStateFromSMDirection();
    emit localSendingStateChanged(localSendingState);

    SendingState remoteSendingState =
        mPriv->remoteSendingStateFromSMDirection();
    QHash<ContactPtr, SendingState> remoteSendingStates;
    remoteSendingStates.insert(mPriv->SMContact, remoteSendingState);
    emit remoteSendingStateChanged(remoteSendingStates);
    emit remoteSendingStateChanged(remoteSendingState);
}

void MediaStream::gotSMStreamState(uint state)
{
    Q_ASSERT(mPriv->ifaceType == IfaceTypeStreamedMedia);

    if (state == mPriv->SMState) {
        return;
    }

    mPriv->SMState = state;
}

void MediaStream::gotCallMainProperties(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "Properties.GetAll(Call.Stream) failed with" <<
            reply.error().name() << ": " << reply.error().message();

        mPriv->readinessHelper->setIntrospectCompleted(FeatureCore,
                false, reply.error());
        watcher->deleteLater();
        return;
    }

    debug() << "Got reply to Properties.GetAll(Call.Stream)";

    QVariantMap props = reply.value();
    TpFuture::ContactSendingStateMap senders =
        qdbus_cast<TpFuture::ContactSendingStateMap>(props[QLatin1String("Senders")]);

    mPriv->callSendersChangedQueue.enqueue(
            new Private::CallSendersChangedInfo(
                senders, UIntList()));
    mPriv->processCallSendersChanged();

    watcher->deleteLater();
}

void MediaStream::gotCallSendersContacts(PendingOperation *op)
{
    PendingContacts *pending = qobject_cast<PendingContacts *>(op);

    mPriv->buildingCallSenders = false;

    if (!pending->isValid()) {
        warning().nospace() << "Getting contacts failed with " <<
            pending->errorName() << ":" << pending->errorMessage() <<
            ", ignoring";
        mPriv->processCallSendersChanged();
        return;
    }

    QMap<uint, ContactPtr> removed;

    for (TpFuture::ContactSendingStateMap::const_iterator i =
            mPriv->currentCallSendersChangedInfo->updates.constBegin();
            i !=
            mPriv->currentCallSendersChangedInfo->updates.constEnd();
            ++i) {
        mPriv->senders.insert(i.key(), i.value());
    }

    foreach (const ContactPtr &contact, pending->contacts()) {
        mPriv->sendersContacts.insert(contact->handle()[0], contact);
    }

    foreach (uint handle, mPriv->currentCallSendersChangedInfo->removed) {
        mPriv->senders.remove(handle);
        if (isReady(FeatureCore) && mPriv->sendersContacts.contains(handle)) {
            removed.insert(handle, mPriv->sendersContacts[handle]);

            // make sure we don't have updates for removed contacts
            mPriv->currentCallSendersChangedInfo->updates.remove(handle);
        }
        mPriv->sendersContacts.remove(handle);
    }

    foreach (uint handle, pending->invalidHandles()) {
        mPriv->senders.remove(handle);
        if (isReady(FeatureCore) && mPriv->sendersContacts.contains(handle)) {
            removed.insert(handle, mPriv->sendersContacts[handle]);

            // make sure we don't have updates for invalid handles
            mPriv->currentCallSendersChangedInfo->updates.remove(handle);
        }
        mPriv->sendersContacts.remove(handle);
    }

    if (isReady(FeatureCore)) {
        StreamedMediaChannelPtr chan(channel());
        uint chanSelfHandle = chan->groupSelfContact()->handle()[0];
        QHash<ContactPtr, SendingState> remoteSendingStates;
        for (TpFuture::ContactSendingStateMap::const_iterator i =
                mPriv->currentCallSendersChangedInfo->updates.constBegin();
                i !=
                mPriv->currentCallSendersChangedInfo->updates.constEnd();
                ++i) {
            uint handle = i.key();
            SendingState sendingState = (SendingState) i.value();

            if (handle == chanSelfHandle) {
                emit localSendingStateChanged(sendingState);
            } else {
                Q_ASSERT(mPriv->sendersContacts.contains(handle));
                remoteSendingStates.insert(mPriv->sendersContacts[handle],
                        sendingState);
            }

            mPriv->senders.insert(i.key(), i.value());
        }

        if (!remoteSendingStates.isEmpty()) {
            emit remoteSendingStateChanged(remoteSendingStates);
            emit remoteSendingStateChanged(remoteSendingStates.constBegin().value());
        }

        if (!removed.isEmpty()) {
            emit membersRemoved(removed.values().toSet());
        }
    }

    mPriv->processCallSendersChanged();
}

QDBusObjectPath MediaStream::callObjectPath() const
{
    return mPriv->callObjectPath;
}

/* ====== PendingMediaContent ====== */
PendingMediaContent::PendingMediaContent(const StreamedMediaChannelPtr &channel,
        const ContactPtr &contact,
        const QString &name,
        MediaStreamType type)
    : PendingOperation(0),
      mPriv(new Private(this, channel))
{
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(
            channel->streamedMediaInterface()->RequestStreams(
                contact->handle()[0], UIntList() << type), this);
    connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(gotSMStream(QDBusPendingCallWatcher*)));
}

PendingMediaContent::PendingMediaContent(const StreamedMediaChannelPtr &channel,
        const QString &name,
        MediaStreamType type)
    : PendingOperation(0),
      mPriv(new Private(this, channel))
{
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(
                mPriv->callInterface()->AddContent(name, type), this);
    connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(gotCallContent(QDBusPendingCallWatcher*)));
}

PendingMediaContent::PendingMediaContent(const StreamedMediaChannelPtr &channel,
        const QString &errorName, const QString &errorMessage)
    : PendingOperation(0),
      mPriv(0)
{
    setFinishedWithError(errorName, errorMessage);
}

PendingMediaContent::~PendingMediaContent()
{
    delete mPriv;
}

MediaContentPtr PendingMediaContent::content() const
{
    if (!isFinished() || !isValid()) {
        return MediaContentPtr();
    }

    return mPriv->content;
}

void PendingMediaContent::gotSMStream(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<MediaStreamInfoList> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "StreamedMedia.RequestStreams failed with" <<
            reply.error().name() << ": " << reply.error().message();
        setFinishedWithError(reply.error());
        watcher->deleteLater();
        return;
    }

    MediaStreamInfoList streamInfoList = reply.value();
    Q_ASSERT(streamInfoList.size() == 1);
    MediaStreamInfo streamInfo = streamInfoList.first();
    StreamedMediaChannelPtr channel(mPriv->channel);
    MediaContentPtr content = channel->lookupContentBySMStreamId(
            streamInfo.identifier);
    if (!content) {
        content = channel->addContentForSMStream(streamInfo);
    } else {
        channel->onSMStreamDirectionChanged(streamInfo.identifier,
                streamInfo.direction, streamInfo.pendingSendFlags);
        channel->onSMStreamStateChanged(streamInfo.identifier,
                streamInfo.state);
    }

    connect(content->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onContentReady(Tp::PendingOperation*)));
    connect(channel.data(),
            SIGNAL(contentRemoved(Tp::MediaContentPtr)),
            SLOT(onContentRemoved(Tp::MediaContentPtr)));

    mPriv->content = content;

    watcher->deleteLater();
}

void PendingMediaContent::gotCallContent(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QDBusObjectPath> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "StreamedMedia.AddContent failed with" <<
            reply.error().name() << ": " << reply.error().message();
        setFinishedWithError(reply.error());
        watcher->deleteLater();
        return;
    }

    QDBusObjectPath contentPath = reply.value();
    StreamedMediaChannelPtr channel(mPriv->channel);
    MediaContentPtr content = channel->lookupContentByCallObjectPath(
            contentPath);
    if (!content) {
        content = channel->addContentForCallObjectPath(contentPath);
    }

    connect(content->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onContentReady(Tp::PendingOperation*)));
    connect(channel.data(),
            SIGNAL(contentRemoved(Tp::MediaContentPtr)),
            SLOT(onContentRemoved(Tp::MediaContentPtr)));

    mPriv->content = content;

    watcher->deleteLater();
}

void PendingMediaContent::onContentReady(PendingOperation *op)
{
    if (op->isError()) {
        setFinishedWithError(op->errorName(), op->errorMessage());
        return;
    }

    setFinished();
}

void PendingMediaContent::onContentRemoved(const MediaContentPtr &content)
{
    if (isFinished()) {
        return;
    }

    if (mPriv->content == content) {
        // the content was removed before becoming ready
        setFinishedWithError(QLatin1String(TELEPATHY_ERROR_CANCELLED),
                QLatin1String("Content removed before ready"));
    }
}

/* ====== MediaContent ====== */
/**
 * \class MediaContent
 * \ingroup clientchannel
 * \headerfile TelepathyQt4/streamed-media-channel.h <TelepathyQt4/StreamedMediaChannel>
 *
 * \brief The MediaContent class provides an object representing a Telepathy
 * media content.
 *
 * Instances of this class cannot be constructed directly; the only way to get
 * one is via StreamedMediaChannel.
 *
 * See \ref async_model
 */

MediaContent::Private::Private(MediaContent *parent,
        const StreamedMediaChannelPtr &channel,
        const QString &name,
        const MediaStreamInfo &streamInfo)
    : ifaceType(IfaceTypeStreamedMedia),
      parent(parent),
      readinessHelper(parent->readinessHelper()),
      channel(channel),
      name(name),
      type(streamInfo.type),
      creatorHandle(0),
      SMStreamInfo(streamInfo)
{
    ReadinessHelper::Introspectables introspectables;

    ReadinessHelper::Introspectable introspectableCore(
        QSet<uint>() << 0,                                                      // makesSenseForStatuses
        Features(),                                                             // dependsOnFeatures
        QStringList(),                                                          // dependsOnInterfaces
        (ReadinessHelper::IntrospectFunc) &Private::introspectSMStream,
        this);
    introspectables[FeatureCore] = introspectableCore;

    readinessHelper->addIntrospectables(introspectables);
    readinessHelper->becomeReady(FeatureCore);
}

MediaContent::Private::Private(MediaContent *parent,
        const StreamedMediaChannelPtr &channel,
        const QDBusObjectPath &objectPath)
    : ifaceType(IfaceTypeCall),
      parent(parent),
      readinessHelper(parent->readinessHelper()),
      channel(channel),
      callBaseInterface(0),
      callPropertiesInterface(0),
      callObjectPath(objectPath)
{
    ReadinessHelper::Introspectables introspectables;

    ReadinessHelper::Introspectable introspectableCore(
        QSet<uint>() << 0,                                                      // makesSenseForStatuses
        Features(),                                                             // dependsOnFeatures
        QStringList(),                                                          // dependsOnInterfaces
        (ReadinessHelper::IntrospectFunc) &Private::introspectCallMainProperties,
        this);
    introspectables[FeatureCore] = introspectableCore;

    readinessHelper->addIntrospectables(introspectables);
    readinessHelper->becomeReady(FeatureCore);
}

void MediaContent::Private::introspectSMStream(MediaContent::Private *self)
{
    Q_ASSERT(self->SMStream);
    self->checkIntrospectionCompleted();
}

void MediaContent::Private::introspectCallMainProperties(
        MediaContent::Private *self)
{
    MediaContent *parent = self->parent;
    StreamedMediaChannelPtr channel = parent->channel();

    self->callBaseInterface = new CallContentInterface(
            channel->dbusConnection(), channel->busName(),
            self->callObjectPath.path(), parent);
    parent->connect(self->callBaseInterface,
            SIGNAL(StreamAdded(QDBusObjectPath)),
            SLOT(onCallStreamAdded(QDBusObjectPath)));
    parent->connect(self->callBaseInterface,
            SIGNAL(StreamRemoved(QDBusObjectPath)),
            SLOT(onCallStreamRemoved(QDBusObjectPath)));

    self->callPropertiesInterface = new Client::DBus::PropertiesInterface(
            channel->dbusConnection(), channel->busName(),
            self->callObjectPath.path(), parent);
    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(
                self->callPropertiesInterface->GetAll(
                    QLatin1String(TP_FUTURE_INTERFACE_CALL_CONTENT)),
                parent);
    parent->connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(gotCallMainProperties(QDBusPendingCallWatcher*)));
}

MediaStreamPtr MediaContent::Private::lookupStreamByCallObjectPath(
        const QDBusObjectPath &streamPath)
{
    foreach (const MediaStreamPtr &stream, streams) {
        if (stream->callObjectPath() == streamPath) {
            return stream;
        }
    }
    foreach (const MediaStreamPtr &stream, incompleteStreams) {
        if (stream->callObjectPath() == streamPath) {
            return stream;
        }
    }
    return MediaStreamPtr();
}

void MediaContent::Private::checkIntrospectionCompleted()
{
    if (!parent->isReady(FeatureCore) &&
        incompleteStreams.size() == 0 &&
        ((creatorHandle != 0 && creator) || (creatorHandle == 0))) {
        readinessHelper->setIntrospectCompleted(FeatureCore, true);
    }
}

void MediaContent::Private::addStream(const MediaStreamPtr &stream)
{
    incompleteStreams.append(stream);
    parent->connect(stream->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onStreamReady(Tp::PendingOperation*)));
}

/**
 * Feature representing the core that needs to become ready to make the
 * MediaContent object usable.
 *
 * Note that this feature must be enabled in order to use most MediaContent
 * methods. See specific methods documentation for more details.
 *
 * When calling isReady(), becomeReady(), this feature is implicitly added
 * to the requested features.
 */
const Feature MediaContent::FeatureCore = Feature(QLatin1String(MediaStream::staticMetaObject.className()), 0);

/**
 * Construct a new MediaContent object.
 *
 * \param channel The channel owning this media content.
 * \param name The name of this media content.
 * \param name The streamInfo used by this media content.
 */
MediaContent::MediaContent(const StreamedMediaChannelPtr &channel,
        const QString &name,
        const MediaStreamInfo &streamInfo)
    : QObject(),
      ReadyObject(this, FeatureCore),
      mPriv(new Private(this, channel, name, streamInfo))
{
}

/**
 * Construct a new MediaContent object.
 *
 * \param channel The channel owning this media content.
 * \param name The object path of this media content.
 */
MediaContent::MediaContent(const StreamedMediaChannelPtr &channel,
        const QDBusObjectPath &objectPath)
    : QObject(),
      ReadyObject(this, FeatureCore),
      mPriv(new Private(this, channel, objectPath))
{
}

/**
 * Class destructor.
 */
MediaContent::~MediaContent()
{
    delete mPriv;
}

/**
 * Return the channel owning this media content.
 *
 * \return The channel owning this media content.
 */
StreamedMediaChannelPtr MediaContent::channel() const
{
    return StreamedMediaChannelPtr(mPriv->channel);
}

/**
 * Return the name of this media content.
 *
 * \return The name of this media content.
 */
QString MediaContent::name() const
{
    return mPriv->name;
}

/**
 * Return the type of this media content.
 *
 * \return The type of this media content.
 */
MediaStreamType MediaContent::type() const
{
    return (MediaStreamType) mPriv->type;
}

/**
 * Return the contact who created this media content.
 *
 * \return The contact who created this media content.
 */
ContactPtr MediaContent::creator() const
{
    // For SM Streams creator will always return ContactPtr(0)
    return mPriv->creator;
}

/**
 * Return the media streams of this media content.
 *
 * \return A list of media streams of this media content.
 * \sa streamAdded(), streamRemoved()
 */
MediaStreams MediaContent::streams() const
{
    return mPriv->streams;
}

/**
 * \fn void MediaContent::streamAdded(const Tp::MediaStreamPtr &stream);
 *
 * This signal is emitted when a new media stream is added to this media
 * content.
 *
 * \param stream The media stream that was added.
 * \sa streams()
 */

/**
 * \fn void MediaContent::streamRemoved(const Tp::MediaStreamPtr &stream);
 *
 * This signal is emitted when a new media stream is removed from this media
 * content.
 *
 * \param stream The media stream that was removed.
 * \sa streams()
 */

void MediaContent::onStreamReady(Tp::PendingOperation *op)
{
    PendingReady *pr = qobject_cast<PendingReady*>(op);
    MediaStreamPtr stream = MediaStreamPtr(
            qobject_cast<MediaStream*>(pr->object()));

    if (op->isError() || !mPriv->incompleteStreams.contains(stream)) {
        mPriv->incompleteStreams.removeOne(stream);
        mPriv->checkIntrospectionCompleted();
        return;
    }

    mPriv->incompleteStreams.removeOne(stream);
    mPriv->streams.append(stream);

    if (isReady(FeatureCore)) {
        emit streamAdded(stream);
    }

    mPriv->checkIntrospectionCompleted();
}

void MediaContent::gotCreator(PendingOperation *op)
{
    PendingContacts *pending = qobject_cast<PendingContacts *>(op);

    if (pending->isValid()) {
        Q_ASSERT(pending->contacts().size() == 1);
        mPriv->creator = pending->contacts()[0];

    } else {
        warning().nospace() << "Getting creator failed with " <<
            pending->errorName() << ":" << pending->errorMessage() <<
            ", ignoring";
        mPriv->creatorHandle = 0;
    }

    mPriv->checkIntrospectionCompleted();
}

void MediaContent::gotCallMainProperties(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "Properties.GetAll(Call.Content) failed with" <<
            reply.error().name() << ": " << reply.error().message();

        mPriv->readinessHelper->setIntrospectCompleted(FeatureCore,
                false, reply.error());
        watcher->deleteLater();
        return;
    }

    debug() << "Got reply to Properties.GetAll(Call.Content)";

    QVariantMap props = reply.value();
    mPriv->name = qdbus_cast<QString>(props[QLatin1String("Name")]);
    mPriv->type = qdbus_cast<uint>(props[QLatin1String("Type")]);
    mPriv->creatorHandle = qdbus_cast<uint>(props[QLatin1String("Creator")]);
    ObjectPathList streamsPaths = qdbus_cast<ObjectPathList>(props[QLatin1String("Streams")]);

    if (streamsPaths.size() == 0 && mPriv->creatorHandle == 0) {
        mPriv->readinessHelper->setIntrospectCompleted(FeatureCore, true);
    }

    foreach (const QDBusObjectPath &streamPath, streamsPaths) {
        MediaStreamPtr stream = mPriv->lookupStreamByCallObjectPath(streamPath);
        if (!stream) {
            MediaStreamPtr stream = MediaStreamPtr(
                    new MediaStream(MediaContentPtr(this), streamPath));
            mPriv->addStream(stream);
        }
    }

    if (mPriv->creatorHandle != 0) {
        ContactManager *contactManager =
            channel()->connection()->contactManager();
        PendingContacts *contacts = contactManager->contactsForHandles(
                UIntList() << mPriv->creatorHandle);
        connect(contacts,
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(gotCreator(Tp::PendingOperation*)));
    }

    watcher->deleteLater();
}

void MediaContent::onCallStreamAdded(const QDBusObjectPath &streamPath)
{
    if (mPriv->lookupStreamByCallObjectPath(streamPath)) {
        debug() << "Received Call.Content.StreamAdded for an existing "
            "stream, ignoring";
        return;
    }

    MediaStreamPtr stream = MediaStreamPtr(
        new MediaStream(MediaContentPtr(this), streamPath));
    mPriv->addStream(stream);
}

void MediaContent::onCallStreamRemoved(const QDBusObjectPath &streamPath)
{
    debug() << "Received Call.Content.StreamRemoved for stream" <<
        streamPath.path();

    MediaStreamPtr stream = mPriv->lookupStreamByCallObjectPath(streamPath);
    if (!stream) {
        return;
    }
    bool incomplete = mPriv->incompleteStreams.contains(stream);
    if (incomplete) {
        mPriv->incompleteStreams.removeOne(stream);
    } else {
        mPriv->streams.removeOne(stream);
    }

    if (isReady(FeatureCore) && !incomplete) {
        emit streamRemoved(stream);
    }

    mPriv->checkIntrospectionCompleted();
}

void MediaContent::setSMStream(const MediaStreamPtr &stream)
{
    Q_ASSERT(mPriv->ifaceType == IfaceTypeStreamedMedia);
    Q_ASSERT(mPriv->incompleteStreams.size() == 0 && mPriv->streams.size() == 0);
    mPriv->SMStream = stream;
    mPriv->incompleteStreams.append(stream);
    connect(stream->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onStreamReady(Tp::PendingOperation*)));
}

MediaStreamPtr MediaContent::SMStream() const
{
    Q_ASSERT(mPriv->ifaceType == IfaceTypeStreamedMedia);
    return mPriv->SMStream;
}

void MediaContent::removeSMStream()
{
    Q_ASSERT(mPriv->ifaceType == IfaceTypeStreamedMedia);
    Q_ASSERT(mPriv->SMStream);

    MediaStreamPtr stream = mPriv->SMStream;
    if (mPriv->streams.contains(stream)) {
        mPriv->streams.removeOne(stream);
        emit streamRemoved(stream);
    } else if (mPriv->incompleteStreams.contains(stream)) {
        mPriv->incompleteStreams.removeOne(stream);
    }
}

QDBusObjectPath MediaContent::callObjectPath() const
{
    return mPriv->callObjectPath;
}

PendingOperation *MediaContent::callRemove()
{
    return new PendingVoid(
            mPriv->callBaseInterface->Remove(),
            this);
}

/* ====== StreamedMediaChannel ====== */
StreamedMediaChannel::Private::Private(StreamedMediaChannel *parent)
    : parent(parent),
      readinessHelper(parent->readinessHelper()),
      localHoldState(LocalHoldStateUnheld),
      localHoldStateReason(LocalHoldStateReasonNone),
      callHardwareStreaming(false),
      numContents(0)
{
    QString channelType = parent->immutableProperties().value(QLatin1String(
                TELEPATHY_INTERFACE_CHANNEL ".ChannelType")).toString();
    if (channelType == QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_STREAMED_MEDIA)) {
        ifaceType = IfaceTypeStreamedMedia;
    } else {
        ifaceType = IfaceTypeCall;
    }

    ReadinessHelper::Introspectables introspectables;

    ReadinessHelper::Introspectable introspectableContents(
        QSet<uint>() << 0,                                                      // makesSenseForStatuses
        Features() << Channel::FeatureCore,                                     // dependsOnFeatures (core)
        QStringList(),                                                          // dependsOnInterfaces
        (ReadinessHelper::IntrospectFunc) &Private::introspectContents,
        this);
    introspectables[FeatureContents] = introspectableContents;

    ReadinessHelper::Introspectable introspectableLocalHoldState(
        QSet<uint>() << 0,                                                          // makesSenseForStatuses
        Features() << Channel::FeatureCore,                                         // dependsOnFeatures (core)
        QStringList() << QLatin1String(TELEPATHY_INTERFACE_CHANNEL_INTERFACE_HOLD), // dependsOnInterfaces
        (ReadinessHelper::IntrospectFunc) &Private::introspectLocalHoldState,
        this);
    introspectables[FeatureLocalHoldState] = introspectableLocalHoldState;

    readinessHelper->addIntrospectables(introspectables);
}

StreamedMediaChannel::Private::~Private()
{
}

void StreamedMediaChannel::Private::introspectContents(StreamedMediaChannel::Private *self)
{
    if (self->ifaceType == IfaceTypeStreamedMedia) {
        self->introspectSMStreams();
    } else {
        self->introspectCallContents();
        return;
    }
}

void StreamedMediaChannel::Private::introspectSMStreams()
{
    Client::ChannelTypeStreamedMediaInterface *streamedMediaInterface =
        parent->streamedMediaInterface();

    parent->connect(streamedMediaInterface,
            SIGNAL(StreamAdded(uint,uint,uint)),
            SLOT(onSMStreamAdded(uint,uint,uint)));
    parent->connect(streamedMediaInterface,
            SIGNAL(StreamRemoved(uint)),
            SLOT(onSMStreamRemoved(uint)));
    parent->connect(streamedMediaInterface,
            SIGNAL(StreamDirectionChanged(uint,uint,uint)),
            SLOT(onSMStreamDirectionChanged(uint,uint,uint)));
    parent->connect(streamedMediaInterface,
            SIGNAL(StreamStateChanged(uint,uint)),
            SLOT(onSMStreamStateChanged(uint,uint)));
    parent->connect(streamedMediaInterface,
            SIGNAL(StreamError(uint,uint,QString)),
            SLOT(onSMStreamError(uint,uint,QString)));

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            streamedMediaInterface->ListStreams(), parent);
    parent->connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher *)),
            SLOT(gotSMStreams(QDBusPendingCallWatcher *)));
}

void StreamedMediaChannel::Private::introspectCallContents()
{
    parent->connect(callInterface(),
            SIGNAL(ContentAdded(QDBusObjectPath,uint)),
            SLOT(onCallContentAdded(QDBusObjectPath,uint)));
    parent->connect(callInterface(),
            SIGNAL(ContentRemoved(QDBusObjectPath)),
            SLOT(onCallContentRemoved(QDBusObjectPath)));

    QDBusPendingCallWatcher *watcher =
        new QDBusPendingCallWatcher(
                parent->propertiesInterface()->GetAll(
                    QLatin1String(TP_FUTURE_INTERFACE_CHANNEL_TYPE_CALL)),
                parent);
    parent->connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(gotCallMainProperties(QDBusPendingCallWatcher*)));
}

void StreamedMediaChannel::Private::introspectLocalHoldState(StreamedMediaChannel::Private *self)
{
    StreamedMediaChannel *parent = self->parent;
    Client::ChannelInterfaceHoldInterface *holdInterface =
        parent->holdInterface();

    parent->connect(holdInterface,
            SIGNAL(HoldStateChanged(uint,uint)),
            SLOT(onLocalHoldStateChanged(uint,uint)));

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            holdInterface->GetHoldState(), parent);
    parent->connect(watcher,
            SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(gotLocalHoldState(QDBusPendingCallWatcher*)));
}

/**
 * \class StreamedMediaChannel
 * \ingroup clientchannel
 * \headerfile TelepathyQt4/streamed-media-channel.h <TelepathyQt4/StreamedMediaChannel>
 *
 * \brief The StreamedMediaChannel class provides an object representing a
 * Telepathy channel of type StreamedMedia or Call.
 */

/**
 * Feature used in order to access media content specific methods.
 *
 * See media content specific methods' documentation for more details.
 */
const Feature StreamedMediaChannel::FeatureContents = Feature(QLatin1String(StreamedMediaChannel::staticMetaObject.className()), 0);

/**
 * Feature used in order to access local hold state info.
 *
 * See local hold state specific methods' documentation for more details.
 */
const Feature StreamedMediaChannel::FeatureLocalHoldState = Feature(QLatin1String(StreamedMediaChannel::staticMetaObject.className()), 1);

/**
 * Feature used in order to access media stream specific methods.
 *
 * See media stream specific methods' documentation for more details.
 */
const Feature StreamedMediaChannel::FeatureStreams = FeatureContents;

/**
 * Create a new StreamedMediaChannel object.
 *
 * \param connection Connection owning this channel, and specifying the
 *                   service.
 * \param objectPath The object path of this channel.
 * \param immutableProperties The immutable properties of this channel.
 * \return A StreamedMediaChannelPtr object pointing to the newly created
 *         StreamedMediaChannel object.
 */
StreamedMediaChannelPtr StreamedMediaChannel::create(const ConnectionPtr &connection,
        const QString &objectPath, const QVariantMap &immutableProperties)
{
    return StreamedMediaChannelPtr(new StreamedMediaChannel(connection,
                objectPath, immutableProperties));
}

/**
 * Construct a new StreamedMediaChannel associated with the given object on the same
 * service as the given connection.
 *
 * \param connection Connection owning this channel, and specifying the
 *                   service.
 * \param objectPath The object path of this channel.
 * \param immutableProperties The immutable properties of this channel.
 */
StreamedMediaChannel::StreamedMediaChannel(const ConnectionPtr &connection,
        const QString &objectPath,
        const QVariantMap &immutableProperties)
    : Channel(connection, objectPath, immutableProperties),
      mPriv(new Private(this))
{
}

/**
 * Class destructor.
 */
StreamedMediaChannel::~StreamedMediaChannel()
{
    delete mPriv;
}

/**
 * Return a list of media streams in this channel.
 *
 * This methods requires StreamedMediaChannel::FeatureStreams to be enabled.
 *
 * \return The media streams in this channel.
 * \sa streamAdded(), streamRemoved(), streamsForType(), requestStreams()
 */
MediaStreams StreamedMediaChannel::streams() const
{
    MediaStreams ret;
    foreach (const MediaContentPtr &content, mPriv->contents) {
        ret << content->streams();
    }
    return ret;
}

/**
 * Return a list of media streams in this channel for the given type \a type.
 *
 * This methods requires StreamedMediaChannel::FeatureStreams to be enabled.
 *
 * \param type The interested type.
 * \return A list of media streams in this channel for the given type \a type.
 * \sa streamAdded(), streamRemoved(), streams(), requestStreams()
 */
MediaStreams StreamedMediaChannel::streamsForType(MediaStreamType type) const
{
    MediaStreams ret;
    foreach (const MediaContentPtr &content, mPriv->contents) {
        if (content->type() == type) {
            ret << content->streams();
        }
    }
    return ret;
}

/**
 * Return whether this channel is awaiting local answer.
 *
 * \return \c true if awaiting local answer, \c false otherwise.
 * \sa awaitingRemoteAnswer()
 */
bool StreamedMediaChannel::awaitingLocalAnswer() const
{
    return groupSelfHandleIsLocalPending();
}

/**
 * Return whether this channel is awaiting remote answer.
 *
 * \return \c true if awaiting remote answer, \c false otherwise.
 * \sa awaitingLocalAnswer()
 */
bool StreamedMediaChannel::awaitingRemoteAnswer() const
{
    return !groupRemotePendingContacts().isEmpty();
}

/**
 * Accept an incoming call.
 *
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 */
PendingOperation *StreamedMediaChannel::acceptCall()
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return groupAddSelfHandle();
    } else {
        return new PendingVoid(mPriv->callInterface()->Accept(), this);
    }
}

/**
 * Remove the specified media stream from this channel.
 *
 * Note that removing a stream from a call will also remove the content the
 * stream belongs to.
 *
 * This methods requires StreamedMediaChannel::FeatureStreams to be enabled.
 *
 * \param stream Media stream to remove.
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa streamRemoved(), streams(), streamsForType()
 */
PendingOperation *StreamedMediaChannel::removeStream(const MediaStreamPtr &stream)
{
    if (!stream) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_INVALID_ARGUMENT),
                QLatin1String("Unable to remove a null stream"), this);
    }

    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        // StreamedMedia.RemoveStreams will trigger StreamedMedia.StreamRemoved
        // that will proper remove the content
        return new PendingVoid(
                streamedMediaInterface()->RemoveStreams(
                    UIntList() << stream->id()),
                this);
    } else {
        return stream->_deprecated_content()->callRemove();
    }
}

/**
 * Remove the specified media streams from this channel.
 *
 * Note that removing a stream from a call will also remove the content the
 * stream belongs to.
 *
 * This methods requires StreamedMediaChannel::FeatureStreams to be enabled.
 *
 * \param streams List of media streams to remove.
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa streamRemoved(), streams(), streamsForType()
 */
PendingOperation *StreamedMediaChannel::removeStreams(const MediaStreams &streams)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        UIntList ids;
        foreach (const MediaStreamPtr &stream, streams) {
            if (!stream) {
                continue;
            }
            ids << stream->id();
        }

        if (ids.isEmpty()) {
            return new PendingFailure(QLatin1String(TELEPATHY_ERROR_INVALID_ARGUMENT),
                    QLatin1String("Unable to remove invalid streams"), this);
        }

        return new PendingVoid(
                streamedMediaInterface()->RemoveStreams(ids),
                this);
    } else {
        // make sure we don't call remove on the same content
        MediaContents contents;
        foreach (const MediaStreamPtr &stream, streams) {
            if (!contents.contains(stream->_deprecated_content())) {
                contents.append(stream->_deprecated_content());
            }
        }

        QList<PendingOperation*> ops;
        foreach (const MediaContentPtr &content, contents) {
            ops << content->callRemove();
        }

        return new PendingComposite(ops, this);
    }
}

/**
 * Request that media streams be established to exchange the given type \a type
 * of media with the given contact \a contact.
 *
 * This methods requires StreamedMediaChannel::FeatureStreams to be enabled.
 *
 * \return A PendingMediaStreams which will emit PendingMediaStreams::finished
 *         when the call has finished.
 * \sa streamAdded(), streams(), streamsForType()
 */
PendingMediaStreams *StreamedMediaChannel::requestStream(
        const ContactPtr &contact,
        MediaStreamType type)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return new PendingMediaStreams(StreamedMediaChannelPtr(this),
                contact,
                QList<MediaStreamType>() << type);
    } else {
        return new PendingMediaStreams(StreamedMediaChannelPtr(this),
                QList<MediaStreamType>() << type);
    }
}

/**
 * Request that media streams be established to exchange the given types \a
 * types of media with the given contact \a contact.
 *
 * This methods requires StreamedMediaChannel::FeatureStreams to be enabled.
 *
 * \return A PendingMediaStreams which will emit PendingMediaStreams::finished
 *         when the call has finished.
 * \sa streamAdded(), streams(), streamsForType()
 */
PendingMediaStreams *StreamedMediaChannel::requestStreams(
        const ContactPtr &contact,
        QList<MediaStreamType> types)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return new PendingMediaStreams(StreamedMediaChannelPtr(this),
                contact, types);
    } else {
        return new PendingMediaStreams(StreamedMediaChannelPtr(this),
                types);
    }
}

/**
 * Request that the call is ended.
 *
 * \param reason A generic hangup reason.
 * \param detailedReason A more specific reason for the call hangup, if one is
 *                       available, or an empty string otherwise.
 * \param message A human-readable message to be sent to the remote contact(s).
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 */
PendingOperation *StreamedMediaChannel::hangupCall(StateChangeReason reason,
        const QString &detailedReason, const QString &message)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return requestClose();
    } else {
        return new PendingVoid(mPriv->callInterface()->Hangup(
                    reason, detailedReason, message), this);
    }
}

/**
 * Return a list of media contents in this channel.
 *
 * This methods requires StreamedMediaChannel::FeatureContents to be enabled.
 *
 * \return The contents in this channel.
 * \sa contentAdded(), contentRemoved(), contentsForType(), requestContent()
 */
MediaContents StreamedMediaChannel::contents() const
{
    return mPriv->contents;
}

/**
 * Return a list of media contents in this channel for the given type \a type.
 *
 * This methods requires StreamedMediaChannel::FeatureContents to be enabled.
 *
 * \param type The interested type.
 * \return A list of media contents in this channel for the given type \a type.
 * \sa contentAdded(), contentRemoved(), contents(), requestContent()
 */
MediaContents StreamedMediaChannel::contentsForType(MediaStreamType type) const
{
    MediaContents contents;
    foreach (const MediaContentPtr &content, mPriv->contents) {
        if (content->type() == type) {
            contents.append(content);
        }
    }
    return contents;
}

/**
 * Request that media content be established to exchange the given type \a type
 * of media.
 *
 * This methods requires StreamedMediaChannel::FeatureContents to be enabled.
 *
 * \return A PendingMediaContent which will emit PendingMediaContent::finished
 *         when the call has finished.
 * \sa contentAdded(), contents(), contentsForType()
 */
PendingMediaContent *StreamedMediaChannel::requestContent(
        const QString &name,
        MediaStreamType type)
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        // get the first contact whose this channel is with. The contact is
        // either in groupContacts or groupRemotePendingContacts
        ContactPtr otherContact;
        foreach (const ContactPtr &contact, groupContacts()) {
            if (contact != groupSelfContact()) {
                otherContact = contact;
                break;
            }
        }

        if (!otherContact) {
            otherContact = *(groupRemotePendingContacts().begin());
        }

        return new PendingMediaContent(StreamedMediaChannelPtr(this),
            otherContact, name, type);
    } else {
        return new PendingMediaContent(StreamedMediaChannelPtr(this),
                name, type);
    }
}

/**
 * Remove the specified media content from this channel.
 *
 * \param content Media content to remove.
 * \return A PendingOperation which will emit PendingOperation::finished
 *         when the call has finished.
 * \sa contentRemoved(), contents(), contentsForType()
 */
PendingOperation *StreamedMediaChannel::removeContent(const MediaContentPtr &content)
{
    if (!content) {
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_INVALID_ARGUMENT),
                QLatin1String("Unable to remove a null content"), this);
    }

    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        // StreamedMedia.RemoveStreams will trigger StreamedMedia.StreamRemoved
        // that will proper remove the content
        MediaStreamPtr stream = content->SMStream();
        return new PendingVoid(
                streamedMediaInterface()->RemoveStreams(
                    UIntList() << stream->id()),
            this);
    } else {
        return content->callRemove();
    }
}

/**
 * Check whether media streaming by the handler is required for this channel.
 *
 * For channels with the MediaSignalling interface, the main handler of the
 * channel is responsible for doing the actual streaming, for instance by
 * calling createFarsightChannel(channel) from TelepathyQt4-Farsight library
 * and using the telepathy-farsight API on the resulting TfChannel.
 *
 * \return \c true if required, \c false otherwise.
 */
bool StreamedMediaChannel::handlerStreamingRequired() const
{
    if (mPriv->ifaceType == IfaceTypeStreamedMedia) {
        return interfaces().contains(
                QLatin1String(TELEPATHY_INTERFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING));
    } else {
        return !mPriv->callHardwareStreaming;
    }
}

/**
 * Return whether the local user has placed this channel on hold.
 *
 * This method requires StreamedMediaChannel::FeatureHoldState to be enabled.
 *
 * \return The channel local hold state.
 * \sa requestHold(), localHoldStateChanged()
 */
LocalHoldState StreamedMediaChannel::localHoldState() const
{
    if (!isReady(FeatureLocalHoldState)) {
        warning() << "StreamedMediaChannel::localHoldState() used with FeatureLocalHoldState not ready";
    } else if (!interfaces().contains(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_INTERFACE_HOLD))) {
        warning() << "StreamedMediaChannel::localHoldStateReason() used with no hold interface";
    }

    return mPriv->localHoldState;
}

/**
 * Return the reason why localHoldState() changed to its current value.
 *
 * This method requires StreamedMediaChannel::FeatureLocalHoldState to be enabled.
 *
 * \return The channel local hold state reason.
 * \sa requestHold(), localHoldStateChanged()
 */
LocalHoldStateReason StreamedMediaChannel::localHoldStateReason() const
{
    if (!isReady(FeatureLocalHoldState)) {
        warning() << "StreamedMediaChannel::localHoldStateReason() used with FeatureLocalHoldState not ready";
    } else if (!interfaces().contains(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_INTERFACE_HOLD))) {
        warning() << "StreamedMediaChannel::localHoldStateReason() used with no hold interface";
    }

    return mPriv->localHoldStateReason;
}

/**
 * Request that the channel be put on hold (be instructed not to send any media
 * streams to you) or be taken off hold.
 *
 * If the connection manager can immediately tell that the requested state
 * change could not possibly succeed, the resulting PendingOperation will fail
 * with error code #TELEPATHY_ERROR_NOT_AVAILABLE.
 * If the requested state is the same as the current state, the resulting
 * PendingOperation will finish successfully.
 *
 * Otherwise, the channel's local hold state will change to
 * Tp::LocalHoldStatePendingHold or Tp::LocalHoldStatePendingUnhold (as
 * appropriate), then the resulting PendingOperation will finish successfully.
 *
 * The eventual success or failure of the request is indicated by a subsequent
 * localHoldStateChanged() signal, changing the local hold state to
 * Tp::LocalHoldStateHeld or Tp::LocalHoldStateUnheld.
 *
 * If the channel has multiple streams, and the connection manager succeeds in
 * changing the hold state of one stream but fails to change the hold state of
 * another, it will attempt to revert all streams to their previous hold
 * states.
 *
 * If the channel does not support the #TELEPATHY_INTERFACE_CHANNEL_INTERFACE_HOLD
 * interface, the PendingOperation will fail with error code
 * #TELEPATHY_ERROR_NOT_IMPLEMENTED.
 *
 * \param hold A boolean indicating whether or not the channel should be on hold
 * \return A %PendingOperation, which will emit PendingOperation::finished
 *         when the request finishes.
 * \sa localHoldState(), localHoldStateReason(), localHoldStateChanged()
 */
PendingOperation *StreamedMediaChannel::requestHold(bool hold)
{
    if (!interfaces().contains(QLatin1String(TELEPATHY_INTERFACE_CHANNEL_INTERFACE_HOLD))) {
        warning() << "StreamedMediaChannel::requestHold() used with no hold interface";
        return new PendingFailure(QLatin1String(TELEPATHY_ERROR_NOT_IMPLEMENTED),
                QLatin1String("StreamedMediaChannel does not support hold interface"),
                this);
    }
    return new PendingVoid(holdInterface()->RequestHold(hold), this);
}

/**
 * \fn void StreamedMediaChannel::contentAdded(const Tp::MediaContentPtr &content);
 *
 * This signal is emitted when a media content is added to this channel.
 *
 * \deprecated Use streamAdded() instead.
 *
 * \param content The media content that was added.
 * \sa contents(), contentsForType()
 */

/**
 * \fn void StreamedMediaChannel::contentRemoved(const Tp::MediaContentPtr &content);
 *
 * This signal is emitted when a media content is removed from this channel.
 *
 * \deprecated Use streamRemoved() instead.
 *
 * \param content The media content that was removed.
 * \sa contents(), contentsForType()
 */

/**
 * \fn void StreamedMediaChannel::streamAdded(const Tp::MediaStreamPtr &stream);
 *
 * This signal is emitted when a media stream is added to this channel.
 *
 * \param stream The media stream that was added.
 * \sa streams(), streamsForType()
 */

/**
 * \fn void StreamedMediaChannel::streamRemoved(const Tp::MediaStreamPtr &stream);
 *
 * This signal is emitted when a media stream is removed from this channel.
 *
 * \param content The media stream that was removed.
 * \sa streams(), streamsForType()
 */

/**
 * \fn void StreamedMediaChannel::streamDirectionChanged(const Tp::MediaStreamPtr &stream, Tp::MediaStreamDirection direction, Tp::MediaStreamPendingSend pendingSend);
 *
 * This signal is emitted when a media stream direction changes.
 *
 * \param stream The media stream which the direction changed.
 * \param direction The new direction of the stream that changed.
 * \param pendingSend the new pending send flags of the stream that changed.
 */

/**
 * \fn void StreamedMediaChannel::streamStateChanged(const Tp::MediaStreamPtr &stream, Tp::MediaStreamState state);
 *
 * This signal is emitted when a media stream state changes.
 *
 * \param stream The media stream which the state changed.
 * \param state The new state of the stream that changed.
 */

/**
 * \fn void StreamedMediaChannel::streamError(const Tp::MediaStreamPtr &stream, Tp::MediaStreamError errorCode, const QString &errorMessage);
 *
 * This signal is emitted when an error occurs on a media stream.
 *
 * \param stream The media stream which the error occurred.
 * \param errorCode The error code.
 * \param errorMessage The error message.
 */

/**
 * \fn void StreamedMediaChannel::localHoldStateChanged(Tp::LocalHoldState state, Tp::LocalHoldStateReason reason);
 *
 * This signal is emitted when the local hold state of this channel changes.
 *
 * \param state The new local hold state of this channel.
 * \param reason The reason why the change occurred.
 * \sa localHoldState(), localHoldStateReason()
 */

void StreamedMediaChannel::onContentReady(PendingOperation *op)
{
    PendingReady *pr = qobject_cast<PendingReady*>(op);
    MediaContentPtr content = MediaContentPtr(
            qobject_cast<MediaContent*>(pr->object()));

    if (op->isError()) {
        mPriv->incompleteContents.removeOne(content);
        if (!isReady(FeatureContents) && mPriv->incompleteContents.size() == 0) {
            // let's not fail because a content could not become ready
            mPriv->readinessHelper->setIntrospectCompleted(
                    FeatureContents, true);
        }
        return;
    }

    // the content was removed before become ready
    if (!mPriv->incompleteContents.contains(content)) {
        if (!isReady(FeatureContents) &&
            mPriv->incompleteContents.size() == 0) {
            mPriv->readinessHelper->setIntrospectCompleted(
                    FeatureContents, true);
        }
        return;
    }

    mPriv->incompleteContents.removeOne(content);
    mPriv->contents.append(content);

    if (isReady(FeatureContents)) {
        emit contentAdded(content);
        foreach (const MediaStreamPtr &stream, content->streams()) {
            emit streamAdded(stream);
        }
    }

    if (!isReady(FeatureContents) && mPriv->incompleteContents.size() == 0) {
        mPriv->readinessHelper->setIntrospectCompleted(FeatureContents, true);
    }
}

void StreamedMediaChannel::gotSMStreams(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<MediaStreamInfoList> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "StreamedMedia.ListStreams failed with" <<
            reply.error().name() << ": " << reply.error().message();

        mPriv->readinessHelper->setIntrospectCompleted(FeatureContents,
                false, reply.error());
        watcher->deleteLater();
        return;
    }

    debug() << "Got reply to StreamedMedia::ListStreams()";

    MediaStreamInfoList streamInfoList = reply.value();
    if (streamInfoList.size() > 0) {
        foreach (const MediaStreamInfo &streamInfo, streamInfoList) {
            MediaContentPtr content = lookupContentBySMStreamId(
                    streamInfo.identifier);
            if (!content) {
                addContentForSMStream(streamInfo);
            } else {
                onSMStreamDirectionChanged(streamInfo.identifier,
                        streamInfo.direction, streamInfo.pendingSendFlags);
                onSMStreamStateChanged(streamInfo.identifier,
                        streamInfo.state);
            }
        }
    } else {
        mPriv->readinessHelper->setIntrospectCompleted(FeatureContents, true);
    }

    watcher->deleteLater();
}

void StreamedMediaChannel::onSMStreamAdded(uint streamId,
        uint contactHandle, uint streamType)
{
    if (lookupContentBySMStreamId(streamId)) {
        debug() << "Received StreamedMedia.StreamAdded for an existing "
            "stream, ignoring";
        return;
    }

    MediaStreamInfo streamInfo = {
        streamId,
        contactHandle,
        streamType,
        MediaStreamStateDisconnected,
        MediaStreamDirectionNone,
        0
    };
    addContentForSMStream(streamInfo);
}

void StreamedMediaChannel::onSMStreamRemoved(uint streamId)
{
    debug() << "Received StreamedMedia.StreamRemoved for stream" <<
        streamId;

    MediaContentPtr content = lookupContentBySMStreamId(streamId);
    if (!content) {
        return;
    }
    bool incomplete = mPriv->incompleteContents.contains(content);
    if (incomplete) {
        mPriv->incompleteContents.removeOne(content);
    } else {
        mPriv->contents.removeOne(content);
    }

    if (isReady(FeatureContents) && !incomplete) {
        // fake stream removal then content removal
        content->removeSMStream();
        emit contentRemoved(content);
    }

    // the content was added/removed before become ready
    if (!isReady(FeatureContents) &&
        mPriv->contents.size() == 0 &&
        mPriv->incompleteContents.size() == 0) {
        mPriv->readinessHelper->setIntrospectCompleted(FeatureContents, true);
    }
}

void StreamedMediaChannel::onSMStreamDirectionChanged(uint streamId,
        uint streamDirection, uint streamPendingFlags)
{
    debug() << "Received StreamedMedia.StreamDirectionChanged for stream" <<
        streamId << "with direction changed to" << streamDirection;

    MediaContentPtr content = lookupContentBySMStreamId(streamId);
    if (!content) {
        return;
    }

    MediaStreamPtr stream = content->SMStream();

    uint oldDirection = stream->direction();
    uint oldPendingFlags = stream->pendingSend();

    stream->gotSMDirection(streamDirection, streamPendingFlags);

    if (oldDirection != streamDirection ||
        oldPendingFlags != streamPendingFlags) {
        emit streamDirectionChanged(stream,
                (MediaStreamDirection) streamDirection,
                (MediaStreamPendingSend) streamPendingFlags);
    }
}

void StreamedMediaChannel::onSMStreamStateChanged(uint streamId,
        uint streamState)
{
    debug() << "Received StreamedMedia.StreamStateChanged for stream" <<
        streamId << "with state changed to" << streamState;

    MediaContentPtr content = lookupContentBySMStreamId(streamId);
    if (!content) {
        return;
    }

    MediaStreamPtr stream = content->SMStream();

    uint oldState = stream->state();

    stream->gotSMStreamState(streamState);

    if (oldState != streamState) {
        emit streamStateChanged(stream, (MediaStreamState) streamState);
    }
}

void StreamedMediaChannel::onSMStreamError(uint streamId,
        uint errorCode, const QString &errorMessage)
{
    debug() << "Received StreamedMedia.StreamError for stream" <<
        streamId << "with error code" << errorCode <<
        "and message:" << errorMessage;

    MediaContentPtr content = lookupContentBySMStreamId(streamId);
    if (!content) {
        return;
    }

    MediaStreamPtr stream = content->SMStream();
    emit streamError(stream, (MediaStreamError) errorCode, errorMessage);
}

void StreamedMediaChannel::gotCallMainProperties(
        QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<QVariantMap> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "Properties.GetAll(Call) failed with" <<
            reply.error().name() << ": " << reply.error().message();

        mPriv->readinessHelper->setIntrospectCompleted(FeatureContents,
                false, reply.error());
        watcher->deleteLater();
        return;
    }

    debug() << "Got reply to Properties.GetAll(Call)";

    QVariantMap props = reply.value();
    mPriv->callHardwareStreaming = qdbus_cast<bool>(props[QLatin1String("HardwareStreaming")]);
    ObjectPathList contentsPaths = qdbus_cast<ObjectPathList>(props[QLatin1String("Contents")]);
    if (contentsPaths.size() > 0) {
        foreach (const QDBusObjectPath &contentPath, contentsPaths) {
            MediaContentPtr content = lookupContentByCallObjectPath(
                    contentPath);
            if (!content) {
                addContentForCallObjectPath(contentPath);
            }
        }
    } else {
        mPriv->readinessHelper->setIntrospectCompleted(FeatureContents, true);
    }

    watcher->deleteLater();
}

void StreamedMediaChannel::onCallContentAdded(
        const QDBusObjectPath &contentPath,
        uint contentType)
{
    if (lookupContentByCallObjectPath(contentPath)) {
        debug() << "Received Call.ContentAdded for an existing "
            "content, ignoring";
        return;
    }

    addContentForCallObjectPath(contentPath);
}

void StreamedMediaChannel::onCallContentRemoved(
        const QDBusObjectPath &contentPath)
{
    debug() << "Received Call.ContentRemoved for content" <<
        contentPath.path();

    MediaContentPtr content = lookupContentByCallObjectPath(contentPath);
    if (!content) {
        return;
    }
    bool incomplete = mPriv->incompleteContents.contains(content);
    if (incomplete) {
        mPriv->incompleteContents.removeOne(content);
    } else {
        mPriv->contents.removeOne(content);
    }

    if (isReady(FeatureContents) && !incomplete) {
        emit contentRemoved(content);
    }

    // the content was added/removed before become ready
    if (!isReady(FeatureContents) &&
        mPriv->contents.size() == 0 &&
        mPriv->incompleteContents.size() == 0) {
        mPriv->readinessHelper->setIntrospectCompleted(FeatureContents, true);
    }
}

void StreamedMediaChannel::gotLocalHoldState(QDBusPendingCallWatcher *watcher)
{
    QDBusPendingReply<uint, uint> reply = *watcher;
    if (reply.isError()) {
        warning().nospace() << "StreamedMedia::Hold::GetHoldState()"
            " failed with " << reply.error().name() << ": " <<
            reply.error().message();

        debug() << "Ignoring error getting hold state and assuming we're not "
            "on hold";
        onLocalHoldStateChanged(mPriv->localHoldState,
                mPriv->localHoldStateReason);
        watcher->deleteLater();
        return;
    }

    debug() << "Got reply to StreamedMedia::Hold::GetHoldState()";
    onLocalHoldStateChanged(reply.argumentAt<0>(), reply.argumentAt<1>());
    watcher->deleteLater();
}

void StreamedMediaChannel::onLocalHoldStateChanged(uint localHoldState,
        uint localHoldStateReason)
{
    bool changed = false;
    if (mPriv->localHoldState != static_cast<LocalHoldState>(localHoldState) ||
        mPriv->localHoldStateReason != static_cast<LocalHoldStateReason>(localHoldStateReason)) {
        changed = true;
    }

    mPriv->localHoldState = static_cast<LocalHoldState>(localHoldState);
    mPriv->localHoldStateReason = static_cast<LocalHoldStateReason>(localHoldStateReason);

    if (!isReady(FeatureLocalHoldState)) {
        mPriv->readinessHelper->setIntrospectCompleted(FeatureLocalHoldState, true);
    } else {
        if (changed) {
            emit localHoldStateChanged(mPriv->localHoldState,
                    mPriv->localHoldStateReason);
        }
    }
}

MediaContentPtr StreamedMediaChannel::addContentForSMStream(
        const MediaStreamInfo &streamInfo)
{
    /* Simulate content creation. For SM channels each stream will have one
     * fake content */
    MediaContentPtr content = MediaContentPtr(
            new MediaContent(StreamedMediaChannelPtr(this),
                QString(QLatin1String("%1 %2 %3"))
                    .arg(streamInfo.type == MediaStreamTypeAudio ?
                        QLatin1String("audio") : QLatin1String("video"))
                    .arg((qulonglong) this)
                    .arg(mPriv->numContents++),
                streamInfo));
    MediaStreamPtr stream = MediaStreamPtr(new MediaStream(content, streamInfo));
    content->setSMStream(stream);

    /* Forward MediaContent::streamAdded/Removed signals */
    connect(content.data(),
            SIGNAL(streamAdded(Tp::MediaStreamPtr)),
            SIGNAL(streamAdded(Tp::MediaStreamPtr)));
    connect(content.data(),
            SIGNAL(streamRemoved(Tp::MediaStreamPtr)),
            SIGNAL(streamRemoved(Tp::MediaStreamPtr)));

    mPriv->incompleteContents.append(content);
    connect(content->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onContentReady(Tp::PendingOperation*)));
    return content;
}

MediaContentPtr StreamedMediaChannel::lookupContentBySMStreamId(uint streamId)
{
    foreach (const MediaContentPtr &content, mPriv->contents) {
        if (content->SMStream()->id() == streamId) {
            return content;
        }
    }
    foreach (const MediaContentPtr &content, mPriv->incompleteContents) {
        if (content->SMStream() && content->SMStream()->id() == streamId) {
            return content;
        }
    }
    return MediaContentPtr();
}

MediaContentPtr StreamedMediaChannel::addContentForCallObjectPath(
        const QDBusObjectPath &contentPath)
{
    /* Simulate content creation. For SM channels each stream will have one
     * fake content */
    MediaContentPtr content = MediaContentPtr(
            new MediaContent(StreamedMediaChannelPtr(this),
                contentPath));

    /* Forward MediaContent::streamAdded/Removed signals */
    connect(content.data(),
            SIGNAL(streamAdded(Tp::MediaStreamPtr)),
            SIGNAL(streamAdded(Tp::MediaStreamPtr)));
    connect(content.data(),
            SIGNAL(streamRemoved(Tp::MediaStreamPtr)),
            SIGNAL(streamRemoved(Tp::MediaStreamPtr)));

    mPriv->incompleteContents.append(content);
    connect(content->becomeReady(),
            SIGNAL(finished(Tp::PendingOperation*)),
            SLOT(onContentReady(Tp::PendingOperation*)));
    return content;
}

MediaContentPtr StreamedMediaChannel::lookupContentByCallObjectPath(
        const QDBusObjectPath &contentPath)
{
    foreach (const MediaContentPtr &content, mPriv->contents) {
        if (content->callObjectPath() == contentPath) {
            return content;
        }
    }
    foreach (const MediaContentPtr &content, mPriv->incompleteContents) {
        if (content->callObjectPath() == contentPath) {
            return content;
        }
    }
    return MediaContentPtr();
}

void MediaStream::connectNotify(const char *signalName)
{
    if (qstrcmp(signalName, SIGNAL(remoteSendingStateChanged(QHash<Tp::ContactPtr,Tp::MediaStream::SendingState>))) == 0) {
        warning() << "Connecting to deprecated signal remoteSendingStateChanged(QHash<Tp::ContactPtr,Tp::MediaStream::SendingState>)";
    } else if (qstrcmp(signalName, SIGNAL(membersRemoved(Tp::Contacts))) == 0) {
        warning() << "Connecting to deprecated signal membersRemoved(Tp::Contacts)";
    }
}

void StreamedMediaChannel::connectNotify(const char *signalName)
{
    if (qstrcmp(signalName, SIGNAL(contentAdded(Tp::MediaContentPtr))) == 0) {
        warning() << "Connecting to deprecated signal contentAdded(Tp::MediaContentPtr)";
    } else if (qstrcmp(signalName, SIGNAL(contentRemoved(Tp::MediaContentPtr))) == 0) {
        warning() << "Connecting to deprecated signal contentRemoved(Tp::MediaContentPtr)";
    }
}

} // Tp
