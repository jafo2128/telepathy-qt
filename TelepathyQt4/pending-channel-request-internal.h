/*
 * This file is part of TelepathyQt4
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

#ifndef _TelepathyQt4_cli_pending_channel_request_internal_h_HEADER_GUARD_
#define _TelepathyQt4_cli_pending_channel_request_internal_h_HEADER_GUARD_

#include <TelepathyQt4/ChannelRequest>
#include <TelepathyQt4/PendingOperation>
#include <TelepathyQt4/Types>

namespace Tp
{

class PendingChannelRequestCancelOperation : public PendingOperation
{
    Q_OBJECT
    Q_DISABLE_COPY(PendingChannelRequestCancelOperation)

public:
    PendingChannelRequestCancelOperation(
            const ChannelRequestPtr &channelRequest)
        : PendingOperation(channelRequest.data())
    {
        mPendingOperation = channelRequest->cancel();
        connect(mPendingOperation,
                SIGNAL(finished(Tp::PendingOperation*)),
                SLOT(onCancelOperationFinished(Tp::PendingOperation*)));
    }

    ~PendingChannelRequestCancelOperation()
    {
    }

private Q_SLOTS:
    void onCancelOperationFinished(Tp::PendingOperation *op)
    {
        if (op->isError()) {
            setFinishedWithError(op->errorName(), op->errorMessage());
            return;
        }
        setFinished();
    }

private:
    PendingOperation *mPendingOperation;
};

} // Tp

#endif
