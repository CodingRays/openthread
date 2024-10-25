/*
 *  Copyright (c) 2019, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file includes definitions for handling indirect transmission.
 */

#include "indirect_sender.hpp"

#if OPENTHREAD_FTD

#include "instance/instance.hpp"

namespace ot {

const Mac::Address &IndirectSender::NeighborInfo::GetMacAddress(Mac::Address &aMacAddress) const
{
    if (mUseShortAddress)
    {
        aMacAddress.SetShort(static_cast<const IndirectNeighbor *>(this)->GetRloc16());
    }
    else
    {
        aMacAddress.SetExtended(static_cast<const IndirectNeighbor *>(this)->GetExtAddress());
    }

    return aMacAddress;
}

IndirectSender::IndirectSender(Instance &aInstance)
    : InstanceLocator(aInstance)
    , mEnabled(false)
    , mSourceMatchController(aInstance)
    , mDataPollHandler(aInstance)
#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    , mCslTxScheduler(aInstance)
#endif
{
}

void IndirectSender::Stop(void)
{
    VerifyOrExit(mEnabled);

    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateAnyExceptInvalid))
    {
        child.SetIndirectMessage(nullptr);
        mSourceMatchController.ResetMessageCount(child);
    }

    mDataPollHandler.Clear();
#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    mCslTxScheduler.Clear();
#endif

exit:
    mEnabled = false;
}

void IndirectSender::AddIndirectMessageForNeighbor(Message &aMessage, IndirectNeighbor &aNeighbor)
{
    OT_ASSERT(!aNeighbor.IsRxOnWhenIdle());

    if (Get<ChildTable>().IsChild(aNeighbor))
    {
        uint16_t childIndex = Get<ChildTable>().GetChildIndex(static_cast<Child &>(aNeighbor));

        VerifyOrExit(!aMessage.GetIndirectTxChildMask().Has(childIndex));
        aMessage.GetIndirectTxChildMask().Add(childIndex);
    }
    else
    {
        OT_ASSERT(false);
    }

    mSourceMatchController.IncrementMessageCount(aNeighbor);

    if ((aMessage.GetType() != Message::kTypeSupervision) && (aNeighbor.GetIndirectMessageCount() > 1))
    {
        Message *supervisionMessage = FindQueuedIndirectMessageForNeighbor(aNeighbor, AcceptSupervisionMessage);

        if (supervisionMessage != nullptr)
        {
            IgnoreError(RemoveIndirectMessageFromNeighbor(*supervisionMessage, aNeighbor));
            Get<MeshForwarder>().RemoveMessageIfNoPendingTx(*supervisionMessage);
        }
    }

    RequestMessageUpdate(aNeighbor);

exit:
    return;
}

Error IndirectSender::RemoveIndirectMessageFromNeighbor(Message &aMessage, IndirectNeighbor &aNeighbor)
{
    Error error = kErrorNone;

    if (Get<ChildTable>().IsChild(aNeighbor))
    {
        uint16_t childIndex = Get<ChildTable>().GetChildIndex(static_cast<Child &>(aNeighbor));

        VerifyOrExit(aMessage.GetIndirectTxChildMask().Has(childIndex), error = kErrorNotFound);
        aMessage.GetIndirectTxChildMask().Remove(childIndex);
    }
    else
    {
        OT_ASSERT(false);
    }

    mSourceMatchController.DecrementMessageCount(aNeighbor);

    RequestMessageUpdate(aNeighbor);

exit:
    return error;
}

void IndirectSender::ClearAllIndirectMessagesForNeighbor(IndirectNeighbor &aNeighbor)
{
    VerifyOrExit(aNeighbor.GetIndirectMessageCount() > 0);

    if (Get<ChildTable>().IsChild(aNeighbor))
    {
        uint16_t childIndex = Get<ChildTable>().GetChildIndex(static_cast<Child &>(aNeighbor));

        for (Message &message : Get<MeshForwarder>().mSendQueue)
        {
            message.GetIndirectTxChildMask().Remove(childIndex);

            Get<MeshForwarder>().RemoveMessageIfNoPendingTx(message);
        }
    }
    else
    {
        OT_ASSERT(false);
    }

    aNeighbor.SetIndirectMessage(nullptr);
    mSourceMatchController.ResetMessageCount(aNeighbor);

    mDataPollHandler.RequestFrameChange(DataPollHandler::kPurgeFrame, aNeighbor);
#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    mCslTxScheduler.Update();
#endif

exit:
    return;
}

const Message *IndirectSender::FindQueuedIndirectMessageForNeighbor(const IndirectNeighbor &aNeighbor,
                                                                    MessageChecker          aChecker) const
{
    const Message *match = nullptr;

    if (Get<ChildTable>().IsChild(aNeighbor))
    {
        uint16_t childIndex = Get<ChildTable>().GetChildIndex(static_cast<const Child &>(aNeighbor));

        for (const Message &message : Get<MeshForwarder>().mSendQueue)
        {
            if (message.GetIndirectTxChildMask().Has(childIndex) && aChecker(message))
            {
                match = &message;
                break;
            }
        }
    }
    else
    {
        OT_ASSERT(false);
    }

    return match;
}

void IndirectSender::SetNeighborUseShortAddress(IndirectNeighbor &aNeighbor, bool aUseShortAddress)
{
    VerifyOrExit(aNeighbor.IsIndirectSourceMatchShort() != aUseShortAddress);

    mSourceMatchController.SetSrcMatchAsShort(aNeighbor, aUseShortAddress);

exit:
    return;
}

void IndirectSender::HandleChildModeChange(Child &aChild, Mle::DeviceMode aOldMode)
{
    if (!aChild.IsRxOnWhenIdle() && (aChild.IsStateValid()))
    {
        SetNeighborUseShortAddress(aChild, true);
    }

    // On sleepy to non-sleepy mode change, convert indirect messages in
    // the send queue destined to the child to direct.

    if (!aOldMode.IsRxOnWhenIdle() && aChild.IsRxOnWhenIdle() && (aChild.GetIndirectMessageCount() > 0))
    {
        uint16_t childIndex = Get<ChildTable>().GetChildIndex(aChild);

        for (Message &message : Get<MeshForwarder>().mSendQueue)
        {
            if (message.GetIndirectTxChildMask().Has(childIndex))
            {
                message.GetIndirectTxChildMask().Remove(childIndex);
                message.SetDirectTransmission();
            }
        }

        aChild.SetIndirectMessage(nullptr);
        mSourceMatchController.ResetMessageCount(aChild);

        mDataPollHandler.RequestFrameChange(DataPollHandler::kPurgeFrame, aChild);
#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
        mCslTxScheduler.Update();
#endif
    }

    // Since the queuing delays for direct transmissions are expected to
    // be relatively small especially when compared to indirect, for a
    // non-sleepy to sleepy mode change, we allow any direct message
    // (for the child) already in the send queue to remain as is. This
    // is equivalent to dropping the already queued messages in this
    // case.
}

void IndirectSender::RequestMessageUpdate(IndirectNeighbor &aNeighbor)
{
    Message *curMessage = aNeighbor.GetIndirectMessage();
    Message *newMessage;

    // Purge the frame if the current message is no longer destined
    // for the child. This check needs to be done first to cover the
    // case where we have a pending "replace frame" request and while
    // waiting for the callback, the current message is removed.
    if (curMessage != nullptr)
    {
        bool purge = false;

        if (Get<ChildTable>().IsChild(aNeighbor))
        {
            purge = !curMessage->GetIndirectTxChildMask().Has(
                Get<ChildTable>().GetChildIndex(static_cast<Child &>(aNeighbor)));
        }
        else
        {
            OT_ASSERT(false);
        }

        if (purge)
        {
            // Set the indirect message for this child to `nullptr` to ensure
            // it is not processed on `HandleSentFrameToChild()` callback.

            aNeighbor.SetIndirectMessage(nullptr);

            // Request a "frame purge" using `RequestFrameChange()` and
            // wait for `HandleFrameChangeDone()` callback for completion
            // of the request. Note that the callback may be directly
            // called from the `RequestFrameChange()` itself when the
            // request can be handled immediately.

            aNeighbor.SetWaitingForMessageUpdate(true);
            mDataPollHandler.RequestFrameChange(DataPollHandler::kPurgeFrame, aNeighbor);
#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
            mCslTxScheduler.Update();
#endif

            ExitNow();
        }
    }

    VerifyOrExit(!aNeighbor.IsWaitingForMessageUpdate());

    newMessage = FindQueuedIndirectMessageForNeighbor(aNeighbor, AcceptAnyMessage);

    VerifyOrExit(curMessage != newMessage);

    if (curMessage == nullptr)
    {
        // Current message is `nullptr`, but new message is not.
        // We have a new indirect message.

        UpdateIndirectMessage(aNeighbor);
        ExitNow();
    }

    // Current message and new message differ and are both
    // non-`nullptr`. We need to request the frame to be replaced.
    // The current indirect message can be replaced only if it is
    // the first fragment. If a next fragment frame for message is
    // already prepared, we wait for the entire message to be
    // delivered.

    VerifyOrExit(aNeighbor.GetIndirectFragmentOffset() == 0);

    aNeighbor.SetWaitingForMessageUpdate(true);
    mDataPollHandler.RequestFrameChange(DataPollHandler::kReplaceFrame, aNeighbor);
#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    mCslTxScheduler.Update();
#endif

exit:
    return;
}

void IndirectSender::HandleFrameChangeDone(IndirectNeighbor &aNeighbor)
{
    VerifyOrExit(aNeighbor.IsWaitingForMessageUpdate());
    UpdateIndirectMessage(aNeighbor);

exit:
    return;
}

void IndirectSender::UpdateIndirectMessage(IndirectNeighbor &aNeighbor)
{
    Message *message = FindQueuedIndirectMessageForNeighbor(aNeighbor, AcceptAnyMessage);

    aNeighbor.SetWaitingForMessageUpdate(false);
    aNeighbor.SetIndirectMessage(message);
    aNeighbor.SetIndirectFragmentOffset(0);
    aNeighbor.SetIndirectTxSuccess(true);

#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    mCslTxScheduler.Update();
#endif

    if (message != nullptr)
    {
        Mac::Address address;

        mDataPollHandler.HandleNewFrame(aNeighbor);

        aNeighbor.GetMacAddress(address);
        Get<MeshForwarder>().LogMessage(MeshForwarder::kMessagePrepareIndirect, *message, kErrorNone, &address);
    }
}

Error IndirectSender::PrepareFrameForNeighbor(Mac::TxFrame &aFrame, FrameContext &aContext, IndirectNeighbor &aNeighbor)
{
    Error    error   = kErrorNone;
    Message *message = aNeighbor.GetIndirectMessage();

    VerifyOrExit(mEnabled, error = kErrorAbort);

    if (message == nullptr)
    {
        PrepareEmptyFrame(aFrame, aNeighbor, /* aAckRequest */ true);
        aContext.mMessageNextOffset = 0;
        ExitNow();
    }

    switch (message->GetType())
    {
    case Message::kTypeIp6:
        aContext.mMessageNextOffset = PrepareDataFrame(aFrame, aNeighbor, *message);
        break;

    case Message::kTypeSupervision:
        PrepareEmptyFrame(aFrame, aNeighbor, /* aAckRequest */ true);
        aContext.mMessageNextOffset = message->GetLength();
        break;

    default:
        OT_ASSERT(false);
    }

exit:
    return error;
}

uint16_t IndirectSender::PrepareDataFrame(Mac::TxFrame &aFrame, IndirectNeighbor &aNeighbor, Message &aMessage)
{
    Ip6::Header    ip6Header;
    Mac::Addresses macAddrs;
    uint16_t       directTxOffset;
    uint16_t       nextOffset;

    // Determine the MAC source and destination addresses.

    IgnoreError(aMessage.Read(0, ip6Header));

    Get<MeshForwarder>().GetMacSourceAddress(ip6Header.GetSource(), macAddrs.mSource);

    if (ip6Header.GetDestination().IsLinkLocalUnicast())
    {
        Get<MeshForwarder>().GetMacDestinationAddress(ip6Header.GetDestination(), macAddrs.mDestination);
    }
    else
    {
        aNeighbor.GetMacAddress(macAddrs.mDestination);
    }

    // Prepare the data frame from previous child's indirect offset.

    directTxOffset = aMessage.GetOffset();
    aMessage.SetOffset(aNeighbor.GetIndirectFragmentOffset());

    nextOffset = Get<MeshForwarder>().PrepareDataFrameWithNoMeshHeader(aFrame, aMessage, macAddrs);

    aMessage.SetOffset(directTxOffset);

    // Set `FramePending` if there are more queued messages (excluding
    // the current one being sent out) for the child (note `> 1` check).
    // The case where the current message itself requires fragmentation
    // is already checked and handled in `PrepareDataFrame()` method.

    if (aNeighbor.GetIndirectMessageCount() > 1)
    {
        aFrame.SetFramePending(true);
    }

    return nextOffset;
}

void IndirectSender::PrepareEmptyFrame(Mac::TxFrame &aFrame, IndirectNeighbor &aNeighbor, bool aAckRequest)
{
    Mac::Address macDest;
    aNeighbor.GetMacAddress(macDest);
    Get<MeshForwarder>().PrepareEmptyFrame(aFrame, macDest, aAckRequest);
}

void IndirectSender::HandleSentFrameToNeighbor(const Mac::TxFrame &aFrame,
                                               const FrameContext &aContext,
                                               Error               aError,
                                               IndirectNeighbor   &aNeighbor)
{
    Message *message    = aNeighbor.GetIndirectMessage();
    uint16_t nextOffset = aContext.mMessageNextOffset;

    VerifyOrExit(mEnabled);

    if (aError == kErrorNone && Get<ChildTable>().IsChild(aNeighbor))
    {
        Get<ChildSupervisor>().UpdateOnSend(static_cast<Child &>(aNeighbor));
    }

    // A zero `nextOffset` indicates that the sent frame is an empty
    // frame generated by `PrepareFrameForChild()` when there was no
    // indirect message in the send queue for the child. This can happen
    // in the (not common) case where the radio platform does not
    // support the "source address match" feature and always includes
    // "frame pending" flag in acks to data poll frames. In such a case,
    // `IndirectSender` prepares and sends an empty frame to the child
    // after it sends a data poll. Here in `HandleSentFrameToChild()` we
    // exit quickly if we detect the "send done" is for the empty frame
    // to ensure we do not update any newly added indirect message after
    // preparing the empty frame.

    VerifyOrExit(nextOffset != 0);

    switch (aError)
    {
    case kErrorNone:
        break;

    case kErrorNoAck:
    case kErrorChannelAccessFailure:
    case kErrorAbort:

        aNeighbor.SetIndirectTxSuccess(false);

#if OPENTHREAD_CONFIG_DROP_MESSAGE_ON_FRAGMENT_TX_FAILURE
        // We set the nextOffset to end of message, since there is no need to
        // send any remaining fragments in the message to the child, if all tx
        // attempts of current frame already failed.

        if (message != nullptr)
        {
            nextOffset = message->GetLength();
        }
#endif
        break;

    default:
        OT_ASSERT(false);
    }

    if ((message != nullptr) && (nextOffset < message->GetLength()))
    {
        aNeighbor.SetIndirectFragmentOffset(nextOffset);
        mDataPollHandler.HandleNewFrame(aNeighbor);
#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
        mCslTxScheduler.Update();
#endif
        ExitNow();
    }

    if (message != nullptr)
    {
        // The indirect tx of this message to the child is done.

        Error        txError = aError;
        Mac::Address macDest;

        aNeighbor.SetIndirectMessage(nullptr);
        aNeighbor.GetLinkInfo().AddMessageTxStatus(aNeighbor.GetIndirectTxSuccess());

        // Enable short source address matching after the first indirect
        // message transmission attempt to the child. We intentionally do
        // not check for successful tx here to address the scenario where
        // the child does receive "Child ID Response" but parent misses the
        // 15.4 ack from child. If the "Child ID Response" does not make it
        // to the child, then the child will need to send a new "Child ID
        // Request" which will cause the parent to switch to using long
        // address mode for source address matching.

        mSourceMatchController.SetSrcMatchAsShort(aNeighbor, true);

#if !OPENTHREAD_CONFIG_DROP_MESSAGE_ON_FRAGMENT_TX_FAILURE

        // When `CONFIG_DROP_MESSAGE_ON_FRAGMENT_TX_FAILURE` is
        // disabled, all fragment frames of a larger message are
        // sent even if the transmission of an earlier fragment fail.
        // Note that `GetIndirectTxSuccess() tracks the tx success of
        // the entire message to the child, while `txError = aError`
        // represents the error status of the last fragment frame
        // transmission.

        if (!aNeighbor.GetIndirectTxSuccess() && (txError == kErrorNone))
        {
            txError = kErrorFailed;
        }
#endif

        if (!aFrame.IsEmpty())
        {
            IgnoreError(aFrame.GetDstAddr(macDest));
            Get<MeshForwarder>().LogMessage(MeshForwarder::kMessageTransmit, *message, txError, &macDest);
        }

        if (message->GetType() == Message::kTypeIp6)
        {
            if (aNeighbor.GetIndirectTxSuccess())
            {
                Get<MeshForwarder>().mIpCounters.mTxSuccess++;
            }
            else
            {
                Get<MeshForwarder>().mIpCounters.mTxFailure++;
            }
        }

        if (Get<ChildTable>().IsChild(aNeighbor))
        {
            uint16_t childIndex = Get<ChildTable>().GetChildIndex(static_cast<Child &>(aNeighbor));

            if (message->GetIndirectTxChildMask().Has(childIndex))
            {
                message->GetIndirectTxChildMask().Remove(childIndex);
                mSourceMatchController.DecrementMessageCount(aNeighbor);
            }
        }
        else
        {
            OT_ASSERT(false);
        }

        Get<MeshForwarder>().RemoveMessageIfNoPendingTx(*message);
    }

    UpdateIndirectMessage(aNeighbor);

exit:
    if (mEnabled)
    {
        ClearMessagesForRemovedChildren();
    }
}

void IndirectSender::ClearMessagesForRemovedChildren(void)
{
    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateAnyExceptValidOrRestoring))
    {
        if (child.GetIndirectMessageCount() == 0)
        {
            continue;
        }

        ClearAllIndirectMessagesForNeighbor(child);
    }
}

bool IndirectSender::AcceptAnyMessage(const Message &aMessage)
{
    OT_UNUSED_VARIABLE(aMessage);

    return true;
}

bool IndirectSender::AcceptSupervisionMessage(const Message &aMessage)
{
    return aMessage.GetType() == Message::kTypeSupervision;
}

} // namespace ot

#endif // #if OPENTHREAD_FTD
