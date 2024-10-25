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
 *   This file includes the implementation for handling of data polls and indirect frame transmission.
 */

#include "data_poll_handler.hpp"

#if OPENTHREAD_FTD

#include "instance/instance.hpp"

namespace ot {

RegisterLogModule("DataPollHandlr");

DataPollHandler::Callbacks::Callbacks(Instance &aInstance)
    : InstanceLocator(aInstance)
{
}

inline Error DataPollHandler::Callbacks::PrepareFrameForNeighbor(Mac::TxFrame     &aFrame,
                                                                 FrameContext     &aContext,
                                                                 IndirectNeighbor &aNeighbor)
{
    return Get<IndirectSender>().PrepareFrameForNeighbor(aFrame, aContext, aNeighbor);
}

inline void DataPollHandler::Callbacks::HandleSentFrameToNeighbor(const Mac::TxFrame &aFrame,
                                                                  const FrameContext &aContext,
                                                                  Error               aError,
                                                                  IndirectNeighbor   &aNeighbor)
{
    Get<IndirectSender>().HandleSentFrameToNeighbor(aFrame, aContext, aError, aNeighbor);
}

inline void DataPollHandler::Callbacks::HandleFrameChangeDone(IndirectNeighbor &aNeighbor)
{
    Get<IndirectSender>().HandleFrameChangeDone(aNeighbor);
}

//---------------------------------------------------------

DataPollHandler::DataPollHandler(Instance &aInstance)
    : InstanceLocator(aInstance)
    , mIndirectTxNeighbor(nullptr)
    , mFrameContext()
    , mCallbacks(aInstance)
{
}

void DataPollHandler::Clear(void)
{
    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateAnyExceptInvalid))
    {
        child.SetDataPollPending(false);
        child.SetFrameReplacePending(false);
        child.SetFramePurgePending(false);
        child.ResetIndirectTxAttempts();
    }

    mIndirectTxNeighbor = nullptr;
}

void DataPollHandler::HandleNewFrame(IndirectNeighbor &aNeighbor)
{
    OT_UNUSED_VARIABLE(aNeighbor);

    // There is no need to take any action with current data poll
    // handler implementation, since the preparation of the frame
    // happens after receiving of a data poll from the child. This
    // method is included for use by other data poll handler models
    // (e.g., in RCP/host model if the handling of data polls is
    // delegated to RCP).
}

void DataPollHandler::RequestFrameChange(FrameChange aChange, IndirectNeighbor &aNeighbor)
{
    if ((mIndirectTxNeighbor == &aNeighbor) && Get<Mac::Mac>().IsPerformingIndirectTransmit())
    {
        switch (aChange)
        {
        case kReplaceFrame:
            aNeighbor.SetFrameReplacePending(true);
            break;

        case kPurgeFrame:
            aNeighbor.SetFramePurgePending(true);
            break;
        }
    }
    else
    {
        ResetTxAttempts(aNeighbor);
        mCallbacks.HandleFrameChangeDone(aNeighbor);
    }
}

void DataPollHandler::HandleDataPoll(Mac::RxFrame &aFrame)
{
    Mac::Address macSource;
    Child       *child;
    uint16_t     indirectMsgCount;

    VerifyOrExit(aFrame.GetSecurityEnabled());
    VerifyOrExit(!Get<Mle::MleRouter>().IsDetached());

    SuccessOrExit(aFrame.GetSrcAddr(macSource));
    child = Get<ChildTable>().FindChild(macSource, Child::kInStateValidOrRestoring);
    VerifyOrExit(child != nullptr);

    child->SetLastHeard(TimerMilli::GetNow());
    child->ResetLinkFailures();
#if OPENTHREAD_CONFIG_MULTI_RADIO
    child->SetLastPollRadioType(aFrame.GetRadioType());
#endif

    indirectMsgCount = child->GetIndirectMessageCount();

    LogInfo("Rx data poll, src:0x%04x, qed_msgs:%d, rss:%d, ack-fp:%d", child->GetRloc16(), indirectMsgCount,
            aFrame.GetRssi(), aFrame.IsAckedWithFramePending());

    if (!aFrame.IsAckedWithFramePending())
    {
        if ((indirectMsgCount > 0) && macSource.IsShort())
        {
            Get<SourceMatchController>().SetSrcMatchAsShort(*child, true);
        }

        ExitNow();
    }

    if (mIndirectTxNeighbor == nullptr)
    {
        mIndirectTxNeighbor = child;
        Get<Mac::Mac>().RequestIndirectFrameTransmission();
    }
    else
    {
        child->SetDataPollPending(true);
    }

exit:
    return;
}

Mac::TxFrame *DataPollHandler::HandleFrameRequest(Mac::TxFrames &aTxFrames)
{
    Mac::TxFrame *frame = nullptr;

    VerifyOrExit(mIndirectTxNeighbor != nullptr);

#if OPENTHREAD_CONFIG_MULTI_RADIO
    frame = &aTxFrames.GetTxFrame(mIndirectTxNeighbor->GetLastPollRadioType());
#else
    frame = &aTxFrames.GetTxFrame();
#endif

    VerifyOrExit(mCallbacks.PrepareFrameForNeighbor(*frame, mFrameContext, *mIndirectTxNeighbor) == kErrorNone,
                 frame = nullptr);

#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    if ((mIndirectTxNeighbor->GetIndirectTxAttempts() > 0) || (mIndirectTxNeighbor->GetCslTxAttempts() > 0))
#else
    if (mIndirectTxNeighbor->GetIndirectTxAttempts() > 0)
#endif
    {
        // For a re-transmission of an indirect frame to a sleepy
        // child, we ensure to use the same frame counter, key id, and
        // data sequence number as the previous attempt.

        frame->SetIsARetransmission(true);
        frame->SetSequence(mIndirectTxNeighbor->GetIndirectDataSequenceNumber());

        if (frame->GetSecurityEnabled())
        {
            frame->SetFrameCounter(mIndirectTxNeighbor->GetIndirectFrameCounter());
            frame->SetKeyId(mIndirectTxNeighbor->GetIndirectKeyId());
        }
    }
    else
    {
        frame->SetIsARetransmission(false);
    }

exit:
    return frame;
}

void DataPollHandler::HandleSentFrame(const Mac::TxFrame &aFrame, Error aError)
{
    IndirectNeighbor *neighbor = mIndirectTxNeighbor;

    VerifyOrExit(neighbor != nullptr);

    mIndirectTxNeighbor = nullptr;
    HandleSentFrame(aFrame, aError, *neighbor);

exit:
    ProcessPendingPolls();
}

void DataPollHandler::HandleSentFrame(const Mac::TxFrame &aFrame, Error aError, IndirectNeighbor &aNeighbor)
{
    if (aNeighbor.IsFramePurgePending())
    {
        aNeighbor.SetFramePurgePending(false);
        aNeighbor.SetFrameReplacePending(false);
        ResetTxAttempts(aNeighbor);
        mCallbacks.HandleFrameChangeDone(aNeighbor);
        ExitNow();
    }

    switch (aError)
    {
    case kErrorNone:
        ResetTxAttempts(aNeighbor);
        aNeighbor.SetFrameReplacePending(false);
        break;

    case kErrorNoAck:
        OT_ASSERT(!aFrame.GetSecurityEnabled() || aFrame.IsHeaderUpdated());

        aNeighbor.IncrementIndirectTxAttempts();
        LogInfo("Indirect tx to child %04x failed, attempt %d/%d", aNeighbor.GetRloc16(), aChild.GetIndirectTxAttempts(),
                kMaxPollTriggeredTxAttempts);

        OT_FALL_THROUGH;

    case kErrorChannelAccessFailure:
    case kErrorAbort:

        if (aNeighbor.IsFrameReplacePending())
        {
            aNeighbor.SetFrameReplacePending(false);
            ResetTxAttempts(aNeighbor);
            mCallbacks.HandleFrameChangeDone(aNeighbor);
            ExitNow();
        }

        if ((aNeighbor.GetIndirectTxAttempts() < kMaxPollTriggeredTxAttempts) && !aFrame.IsEmpty())
        {
            // We save the frame counter, key id, and data sequence number of
            // current frame so we use the same values for the retransmission
            // of the frame following the receipt of the next data poll.

            aNeighbor.SetIndirectDataSequenceNumber(aFrame.GetSequence());

            if (aFrame.GetSecurityEnabled() && aFrame.IsHeaderUpdated())
            {
                uint32_t frameCounter;
                uint8_t  keyId;

                SuccessOrAssert(aFrame.GetFrameCounter(frameCounter));
                aNeighbor.SetIndirectFrameCounter(frameCounter);

                SuccessOrAssert(aFrame.GetKeyId(keyId));
                aNeighbor.SetIndirectKeyId(keyId);
            }

            ExitNow();
        }

        aNeighbor.ResetIndirectTxAttempts();
        break;

    default:
        OT_ASSERT(false);
    }

    mCallbacks.HandleSentFrameToNeighbor(aFrame, mFrameContext, aError, aNeighbor);

exit:
    return;
}

void DataPollHandler::ProcessPendingPolls(void)
{
    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateValidOrRestoring))
    {
        if (!child.IsDataPollPending())
        {
            continue;
        }

        // Find the child with earliest poll receive time.

        if ((mIndirectTxNeighbor == nullptr) || (child.GetLastHeard() < mIndirectTxNeighbor->GetLastHeard()))
        {
            mIndirectTxNeighbor = &child;
        }
    }

    if (mIndirectTxNeighbor != nullptr)
    {
        mIndirectTxNeighbor->SetDataPollPending(false);
        Get<Mac::Mac>().RequestIndirectFrameTransmission();
    }
}

void DataPollHandler::ResetTxAttempts(IndirectNeighbor &aNeighbor)
{
    aNeighbor.ResetIndirectTxAttempts();

#if OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    aNeighbor.ResetCslTxAttempts();
#endif
}

} // namespace ot

#endif // #if OPENTHREAD_FTD
