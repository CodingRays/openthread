/*
 *  Copyright (c) 2020, The OpenThread Authors.
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

#include "csl_tx_scheduler.hpp"

#if (OPENTHREAD_FTD && OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE) || (OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE && OPENTHREAD_MTD)

#include "common/locator_getters.hpp"
#include "common/log.hpp"
#include "common/time.hpp"
#include "mac/mac.hpp"

namespace ot {

RegisterLogModule("CslTxScheduler");

CslTxScheduler::Callbacks::Callbacks(Instance &aInstance)
    : InstanceLocator(aInstance)
{
}

inline Error CslTxScheduler::Callbacks::PrepareFrameForNeighbor(Mac::TxFrame      &aFrame,
                                                             FrameContext      &aContext,
                                                             IndirectReachable &aNeighbor)
{
    return Get<IndirectSender>().PrepareFrameForChild(aFrame, aContext, aNeighbor);
}

inline void CslTxScheduler::Callbacks::HandleSentFrameToNeighbor(const Mac::TxFrame &aFrame,
                                                              const FrameContext &aContext,
                                                              Error               aError,
                                                              IndirectReachable  &aNeighbor)
{
    Get<IndirectSender>().HandleSentFrameToChild(aFrame, aContext, aError, aNeighbor);
}

//---------------------------------------------------------

CslTxScheduler::CslTxScheduler(Instance &aInstance)
    : InstanceLocator(aInstance)
    , mCslTxChild(nullptr)
    , mCslTxMessage(nullptr)
    , mFrameContext()
    , mCallbacks(aInstance)
{
    InitFrameRequestAhead();
}

void CslTxScheduler::InitFrameRequestAhead(void)
{
    uint32_t busSpeedHz = otPlatRadioGetBusSpeed(&GetInstance());
    // longest frame on bus is 127 bytes with some metadata, use 150 bytes for bus Tx time estimation
    uint32_t busTxTimeUs = ((busSpeedHz == 0) ? 0 : (150 * 8 * 1000000 + busSpeedHz - 1) / busSpeedHz);

    mCslFrameRequestAheadUs = OPENTHREAD_CONFIG_MAC_CSL_REQUEST_AHEAD_US + busTxTimeUs;
}

void CslTxScheduler::Update(void)
{
    if (mCslTxMessage == nullptr)
    {
        RescheduleCslTx();
    }
    else if ((mCslTxChild != nullptr) && (mCslTxChild->GetIndirectMessage() != mCslTxMessage))
    {
        // `Mac` has already started the CSL tx, so wait for tx done callback
        // to call `RescheduleCslTx`
        mCslTxChild->ResetCslTxAttempts();
        mCslTxChild                      = nullptr;
        mFrameContext.mMessageNextOffset = 0;
    }
}

void CslTxScheduler::Clear(void)
{
    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateAnyExceptInvalid))
    {
        child.ResetCslTxAttempts();
        child.SetCslSynchronized(false);
        child.SetCslChannel(0);
        child.SetCslTimeout(0);
        child.SetCslPeriod(0);
        child.SetCslPhase(0);
#if OPENTHREAD_FTD
        child.SetCslLastHeard(TimeMilli(0));
#else
        // We reuse the csl last heard value to schedule update requests
        child.SetCslLastHeard(TimerMilli::GetNow());
#endif
    }

    mFrameContext.mMessageNextOffset = 0;
    mCslTxChild                      = nullptr;
    mCslTxMessage                    = nullptr;
}

/**
 * Always finds the most recent CSL tx among all children,
 * and requests `Mac` to do CSL tx at specific time. It shouldn't be called
 * when `Mac` is already starting to do the CSL tx (indicated by `mCslTxMessage`).
 *
 */
void CslTxScheduler::RescheduleCslTx(void)
{
    uint32_t           minDelayTime = Time::kMaxDuration;
    IndirectReachable *bestChild    = nullptr;

    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateAnyExceptInvalid))
    {
        ProcessNeighborCslTxTime(child, minDelayTime, bestChild);
    }

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    if (!Get<Mle::Mle>().GetParent().IsStateInvalid()) 
    {
        ProcessNeighborCslTxTime(Get<Mle::Mle>().GetParent(), minDelayTime, bestChild);
    }

    if (!Get<Mle::Mle>().GetParentCandidate().IsStateInvalid())
    {
        ProcessNeighborCslTxTime(Get<Mle::Mle>().GetParentCandidate(), minDelayTime, bestChild);
    }
#endif 

    if (bestChild != nullptr)
    {
        Get<Mac::Mac>().RequestCslFrameTransmission(minDelayTime / 1000UL);
    }

    mCslTxChild = bestChild;
}

void CslTxScheduler::ProcessNeighborCslTxTime(IndirectReachable &aNeighbor, uint32_t &aDelayTime, IndirectReachable *&aNeighborOut) const
{
    uint32_t delay;
    uint32_t cslTxDelay;

    if (!aNeighbor.IsCslSynchronized() || aNeighbor.GetIndirectMessageCount() == 0)
    {
        ExitNow();
    }

    delay = GetNextCslTransmissionDelay(aNeighbor, cslTxDelay, mCslFrameRequestAheadUs);

    if (delay < aDelayTime)
    {
        aDelayTime   = delay;
        aNeighborOut = &aNeighbor;
    }

exit:
    return;
}

uint32_t CslTxScheduler::GetNextCslTransmissionDelay(const IndirectReachable &aNeighbor,
                                                     uint32_t    &aDelayFromLastRx,
                                                     uint32_t     aAheadUs) const
{
    uint64_t radioNow   = otPlatRadioGetNow(&GetInstance());
    uint32_t periodInUs = aNeighbor.GetCslPeriod() * kUsPerTenSymbols;

    /* see CslTxScheduler::ChildInfo::mCslPhase */
    uint64_t firstTxWindow = aNeighbor.GetLastRxTimestamp() + aNeighbor.GetCslPhase() * kUsPerTenSymbols;
    uint64_t nextTxWindow  = radioNow - (radioNow % periodInUs) + (firstTxWindow % periodInUs);

    if (periodInUs == 0)
    {
        LogWarn("Csl neighbor %#06X has a period of 0", aNeighbor.GetRloc16());
        periodInUs = 10000;
    }

    while (nextTxWindow < radioNow + aAheadUs)
    {
        nextTxWindow += periodInUs;
    }

    aDelayFromLastRx = static_cast<uint32_t>(nextTxWindow - aNeighbor.GetLastRxTimestamp());

    return static_cast<uint32_t>(nextTxWindow - radioNow - aAheadUs);
}

#if OPENTHREAD_CONFIG_RADIO_LINK_IEEE_802_15_4_ENABLE

Mac::TxFrame *CslTxScheduler::HandleFrameRequest(Mac::TxFrames &aTxFrames)
{
    Mac::TxFrame *frame = nullptr;
    uint32_t      txDelay;
    uint32_t      delay;

    VerifyOrExit(mCslTxChild != nullptr);
    VerifyOrExit(mCslTxChild->IsCslSynchronized());

#if OPENTHREAD_CONFIG_MULTI_RADIO
    frame = &aTxFrames.GetTxFrame(Mac::kRadioTypeIeee802154);
#else
    frame = &aTxFrames.GetTxFrame();
#endif

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    if (!Get<Mle::Mle>().IsRxOnWhenIdle())
    {
        mCslTxChild->GetIndirectMessage()->SetLinkSecurityEnabled(true);
    }
#endif

    VerifyOrExit(mCallbacks.PrepareFrameForNeighbor(*frame, mFrameContext, *mCslTxChild) == kErrorNone, frame = nullptr);
    mCslTxMessage = mCslTxChild->GetIndirectMessage();
    VerifyOrExit(mCslTxMessage != nullptr, frame = nullptr);

    if (mCslTxChild->GetIndirectTxAttempts() > 0 || mCslTxChild->GetCslTxAttempts() > 0)
    {
        // For a re-transmission of an indirect frame to a sleepy
        // child, we ensure to use the same frame counter, key id, and
        // data sequence number as the previous attempt.

        frame->SetIsARetransmission(true);
        frame->SetSequence(mCslTxChild->GetIndirectDataSequenceNumber());

        if (frame->GetSecurityEnabled())
        {
            frame->SetFrameCounter(mCslTxChild->GetIndirectFrameCounter());
            frame->SetKeyId(mCslTxChild->GetIndirectKeyId());
        }
    }
    else
    {
        frame->SetIsARetransmission(false);
    }

    frame->SetChannel(mCslTxChild->GetCslChannel() == 0 ? Get<Mac::Mac>().GetPanChannel()
                                                        : mCslTxChild->GetCslChannel());

    if (frame->GetChannel() != Get<Mac::Mac>().GetPanChannel())
    {
        frame->SetRxChannelAfterTxDone(Get<Mac::Mac>().GetPanChannel());
    }

    delay = GetNextCslTransmissionDelay(*mCslTxChild, txDelay, /* aAheadUs */ 0);

    // We make sure that delay is less than `mCslFrameRequestAheadUs`
    // plus some guard time. Note that we used `mCslFrameRequestAheadUs`
    // in `RescheduleCslTx()` when determining the next CSL delay to
    // schedule CSL tx with `Mac` but here we calculate the delay with
    // zero `aAheadUs`. All the timings are in usec but when passing
    // delay to `Mac` we divide by `1000` (to convert to msec) which
    // can round the value down and cause `Mac` to start operation a
    // bit (some usec) earlier. This is covered by adding the guard
    // time `kFramePreparationGuardInterval`.
    //
    // In general this check handles the case where `Mac` is busy with
    // other operations and therefore late to start the CSL tx operation
    // and by the time `HandleFrameRequest()` is invoked, we miss the
    // current CSL window and move to the next window.

    VerifyOrExit(delay <= mCslFrameRequestAheadUs + kFramePreparationGuardInterval, frame = nullptr);

    frame->SetTxDelay(txDelay);
    frame->SetTxDelayBaseTime(
        static_cast<uint32_t>(mCslTxChild->GetLastRxTimestamp())); // Only LSB part of the time is required.
    frame->SetCsmaCaEnabled(false);

exit:
    return frame;
}

#else // OPENTHREAD_CONFIG_RADIO_LINK_IEEE_802_15_4_ENABLE

Mac::TxFrame *CslTxScheduler::HandleFrameRequest(Mac::TxFrames &) { return nullptr; }

#endif // OPENTHREAD_CONFIG_RADIO_LINK_IEEE_802_15_4_ENABLE

void CslTxScheduler::HandleSentFrame(const Mac::TxFrame &aFrame, Error aError)
{
    IndirectReachable *neighbor = mCslTxChild;

    mCslTxMessage = nullptr;

    VerifyOrExit(neighbor != nullptr); // The result is no longer interested by upper layer

    mCslTxChild = nullptr;

    HandleSentFrame(aFrame, aError, *neighbor);

exit:
#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    if (!(aFrame.GetFramePending() && aError == kErrorNone))
#endif
    {
        RescheduleCslTx();
    }
}

void CslTxScheduler::HandleSentFrame(const Mac::TxFrame &aFrame, Error aError, IndirectReachable &aNeighbor)
{
    switch (aError)
    {
    case kErrorNone:
        aNeighbor.ResetCslTxAttempts();
        aNeighbor.ResetIndirectTxAttempts();
        break;

    case kErrorNoAck:
        OT_ASSERT(!aFrame.GetSecurityEnabled() || aFrame.IsHeaderUpdated());

        aNeighbor.IncrementCslTxAttempts();
        LogInfo("CSL tx to neighbor %04x failed, attempt %d/%d", aNeighbor.GetRloc16(), aNeighbor.GetCslTxAttempts(),
                kMaxCslTriggeredTxAttempts);

        if (aNeighbor.GetCslTxAttempts() >= kMaxCslTriggeredTxAttempts)
        {
            // CSL transmission attempts reach max, consider child out of sync
            aNeighbor.SetCslSynchronized(false);
            aNeighbor.ResetCslTxAttempts();
        }

        OT_FALL_THROUGH;

    case kErrorChannelAccessFailure:
    case kErrorAbort:

        // Even if CSL tx attempts count reaches max, the message won't be
        // dropped until indirect tx attempts count reaches max. So here it
        // would set sequence number and schedule next CSL tx.

        if (!aFrame.IsEmpty())
        {
            aNeighbor.SetIndirectDataSequenceNumber(aFrame.GetSequence());

            if (aFrame.GetSecurityEnabled() && aFrame.IsHeaderUpdated())
            {
                uint32_t frameCounter;
                uint8_t  keyId;

                IgnoreError(aFrame.GetFrameCounter(frameCounter));
                aNeighbor.SetIndirectFrameCounter(frameCounter);

                IgnoreError(aFrame.GetKeyId(keyId));
                aNeighbor.SetIndirectKeyId(keyId);
            }
        }

        ExitNow();

    default:
        OT_ASSERT(false);
        OT_UNREACHABLE_CODE(break);
    }

    mCallbacks.HandleSentFrameToNeighbor(aFrame, mFrameContext, aError, aNeighbor);

exit:
    return;
}

} // namespace ot

#endif // (OPENTHREAD_FTD && OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE) || (OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE && OPENTHREAD_MTD)
