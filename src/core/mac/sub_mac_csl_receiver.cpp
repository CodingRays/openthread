/*
 *  Copyright (c) 2024, The OpenThread Authors.
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
 *   This file implements the CSL receiver of the subset of IEEE 802.15.4 MAC primitives.
 */

#include "sub_mac.hpp"

#if (OPENTHREAD_FTD || OPENTHREAD_MTD) && OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE

#include "common/code_utils.hpp"
#include "common/locator_getters.hpp"
#include "common/log.hpp"
#include "instance/instance.hpp"

namespace ot {
namespace Mac {

RegisterLogModule("SubMac");

void SubMac::CslInit(void)
{
    mCslPeriod     = 0;
    mCslChannel    = 0;
    mCslPeerShort  = 0;
    mCslState      = kCslStateCslQueued;
    mCslSampleTime = TimeMicro{0};
    mCslTimer.Stop();
}

void SubMac::UpdateCslLastSyncTimestamp(TxFrame &aFrame, RxFrame *aAckFrame)
{
    // Actual synchronization timestamp should be from the sent frame instead of the current time.
    // Assuming the error here since it is bounded and has very small effect on the final window duration.
    if (aAckFrame != nullptr && aFrame.GetHeaderIe(CslIe::kHeaderIeId) != nullptr)
    {
        CslInfo *neighbor{ nullptr };

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
        Address address;

        if (aFrame.GetDstAddr(address) == OT_ERROR_NONE)
        {
            neighbor = Get<NeighborTable>().FindParent(address, Neighbor::kInStateValid);
            if (neighbor == nullptr)
            {
                neighbor = Get<ChildTable>().FindChild(address, Neighbor::kInStateValid);
            }
        }
#else
        if (Get<Mle::Mle>().GetParent().IsStateValid())
        {
            neighbor = &Get<Mle::Mle>().GetParent();
        }
#endif
        if (neighbor != nullptr)
        {
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_LOCAL_TIME_SYNC
            neighbor->SetCslLastSync(TimerMicro::GetNow());
#else
            neighbor->SetCslLastSync(TimeMicro(otPlatRadioGetNow(&GetInstance())));
#endif
        }
    }
}

void SubMac::UpdateCslLastSyncTimestamp(RxFrame *aFrame, Error aError)
{
    VerifyOrExit(aFrame != nullptr && aError == kErrorNone);

#if OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE
    LogReceived(aFrame);
#endif

    // Assuming the risk of the parent missing the Enh-ACK in favor of smaller CSL receive window
    if ((mCslPeriod > 0) && aFrame->mInfo.mRxInfo.mAckedWithSecEnhAck)
    {
        CslInfo *neighbor{ nullptr };

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
        Address address;

        if (aFrame->GetSrcAddr(address) == OT_ERROR_NONE)
        {
            neighbor = Get<NeighborTable>().FindParent(address, Neighbor::kInStateValid);
            if (neighbor == nullptr)
            {
                neighbor = Get<ChildTable>().FindChild(address, Neighbor::kInStateValid);
            }
        }
#else
        if (Get<Mle::Mle>().GetParent().IsStateValid())
        {
            neighbor = &Get<Mle::Mle>().GetParent();
        }
#endif
        if (neighbor != nullptr)
        {
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_LOCAL_TIME_SYNC
            neighbor->SetCslLastSync(TimerMicro::GetNow());
#else
            neighbor->SetCslLastSync(TimeMicro(static_cast<uint32_t>(aFrame->mInfo.mRxInfo.mTimestamp)));
#endif
        }
    }

exit:
    return;
}

void SubMac::CslSample(void)
{
#if OPENTHREAD_CONFIG_MAC_FILTER_ENABLE
    VerifyOrExit(!mRadioFilterEnabled, IgnoreError(Get<Radio>().Sleep()));
#endif

    SetState(kStateCslSample);

    if (mCslState == kCslStateCslReceive && !RadioSupportsReceiveTiming())
    {
        IgnoreError(Get<Radio>().Receive(mCslChannel));
        ExitNow();
    }

#if !OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE
    IgnoreError(Get<Radio>().Sleep()); // Don't actually sleep for debugging
#endif

exit:
    return;
}

bool SubMac::UpdateCsl(uint16_t aPeriod, uint8_t aChannel, otShortAddress aShortAddr, const otExtAddress *aExtAddr)
{
    bool diffPeriod  = aPeriod != mCslPeriod;
    bool diffChannel = aChannel != mCslChannel;
    bool diffPeer    = aShortAddr != mCslPeerShort;
    bool retval      = diffPeriod || diffChannel || diffPeer;

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    retval = true;
    UpdateCslNeighbors();
#endif

    VerifyOrExit(retval);
    mCslChannel = aChannel;

    VerifyOrExit(diffPeriod || diffPeer);
    mCslPeriod    = aPeriod;
    mCslPeerShort = aShortAddr;
    IgnoreError(Get<Radio>().EnableCsl(aPeriod));

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    UpdateCslNeighbors();
    OT_UNUSED_VARIABLE(aShortAddr);
    OT_UNUSED_VARIABLE(aExtAddr);
#else
    IgnoreError(Get<Radio>().AddCslShortEntry(aShortAddr));
    IgnoreError(Get<Radio>().AddCslExtEntry(*static_cast<const ExtAddress*>(aExtAddr)));
#endif

    mCslTimer.Stop();
    if (mCslPeriod > 0)
    {
        mCslSampleTime = TimeMicro(static_cast<uint32_t>(otPlatRadioGetNow(&GetInstance())));
        
#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
        if (mWakeupListenTime > TimerMicro::GetNow() + 20000000 || mWakeupListenTime < TimerMicro::GetNow())
        {
            uint32_t wakeupPeriodUs = static_cast<uint32_t>(mWakeupListenPeriod) * 256 * kUsPerTenSymbols;
            uint32_t count = (TimerMicro::GetNow() - mWakeupListenTime) % wakeupPeriodUs;
            mWakeupListenTime += (count + 1) * wakeupPeriodUs;
        }
        mWakeupListenTime = TimerMicro::GetNow();
#endif

        mCslState = kCslStateCslReceive;
        HandleCslTimer();
    }

exit:
    return retval;
}

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
void SubMac::UpdateCslNeighbors(void)
{
    IgnoreError(Get<Radio>().ClearCslShortEntries());
    IgnoreError(Get<Radio>().ClearCslExtEntries());

    if (Get<Mle::Mle>().GetParent().IsStateValid())
    {
        IgnoreError(Get<Radio>().AddCslShortEntry(Get<Mle::Mle>().GetParent().GetRloc16()));
        IgnoreError(Get<Radio>().AddCslExtEntry(Get<Mle::Mle>().GetParent().GetExtAddress()));
    }
    if (Get<Mle::Mle>().GetParentCandidate().IsStateValid())
    {
        IgnoreError(Get<Radio>().AddCslShortEntry(Get<Mle::Mle>().GetParent().GetRloc16()));
        IgnoreError(Get<Radio>().AddCslExtEntry(Get<Mle::Mle>().GetParent().GetExtAddress()));
    }
    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateWithSecurityReady))
    {
        if (child.GetRloc16() != Mle::kInvalidRloc16)
        {
            IgnoreError(Get<Radio>().AddCslShortEntry(child.GetRloc16()));
        }
        IgnoreError(Get<Radio>().AddCslExtEntry(child.GetExtAddress()));
    }
}
#endif

void SubMac::HandleCslTimer(Timer &aTimer) { aTimer.Get<SubMac>().HandleCslTimer(); }

void SubMac::HandleCslTimer(void)
{
    /*
     * CSL sample timing diagram
     *    |<---------------------------------Sample--------------------------------->|<--------Sleep--------->|
     *    |                                                                          |                        |
     *    |<--Ahead-->|<--UnCert-->|<--Drift-->|<--Drift-->|<--UnCert-->|<--MinWin-->|                        |
     *    |           |            |           |           |            |            |                        |
     * ---|-----------|------------|-----------|-----------|------------|------------|----------//------------|---
     * -timeAhead                           CslPhase                             +timeAfter             -timeAhead
     *
     * The handler works in different ways when the radio supports receive-timing and doesn't.
     *
     * When the radio supports receive-timing:
     *   The handler will be called once per CSL period. When the handler is called, it will set the timer to
     *   fire at the next CSL sample time and call `Radio::ReceiveAt` to start sampling for the current CSL period.
     *   The timer fires some time before the actual sample time. After `Radio::ReceiveAt` is called, the radio will
     *   remain in sleep state until the actual sample time.
     *   Note that it never call `Radio::Sleep` explicitly. The radio will fall into sleep after `ReceiveAt` ends. This
     *   will be done by the platform as part of the `otPlatRadioReceiveAt` API.
     *
     *   Timer fires                                         Timer fires
     *       ^                                                    ^
     *       x-|------------|-------------------------------------x-|------------|---------------------------------------|
     *            sample                   sleep                        sample                    sleep
     *
     * When the radio doesn't support receive-timing:
     *   The handler will be called twice per CSL period: at the beginning of sample and sleep. When the handler is
     *   called, it will explicitly change the radio state due to the current state by calling `Radio::Receive` or
     *   `Radio::Sleep`.
     *
     *   Timer fires  Timer fires                            Timer fires  Timer fires
     *       ^            ^                                       ^            ^
     *       |------------|---------------------------------------|------------|---------------------------------------|
     *          sample                   sleep                        sample                    sleep
     *
     */
    uint32_t timeAhead, timeAfter;
    TimeMicro nextFireTime = TimerMicro::GetNow();

    GetCslWindowEdges(timeAhead, timeAfter);

    switch (mCslState)
    {
#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    case kCslStateWakeupReceive:
#endif
    case kCslStateCslReceive:
        nextFireTime = ScheduleNextCslEvent(timeAhead, TimerMicro::GetNow());
        if (mState == kStateCslSample)
        {
#if !OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE
            IgnoreError(Get<Radio>().Sleep()); // Don't actually sleep for debugging
#endif
            LogDebg("CSL sleep %lu", ToUlong(mCslTimer.GetNow().GetValue()));
        }
        break;

    case kCslStateCslQueued:
        nextFireTime = HandleCslWindowBegin(timeAhead, timeAfter);
        break;

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    case kCslStateWakeupQueued:
        nextFireTime = HandleWakeupListenWindowBegin(timeAhead);
        break;
#endif
    }

    mCslTimer.FireAt(nextFireTime);

}

TimeMicro SubMac::HandleCslWindowBegin(uint32_t aTimeAhead, uint32_t aTimeAfter)
{
    TimeMicro nextFireTime = TimerMicro::GetNow();
    uint32_t  periodUs = mCslPeriod * kUsPerTenSymbols;
    uint32_t  winStart, winDuration;

    if (RadioSupportsReceiveTiming())
    {
        winStart    = mCslSampleTime.GetValue() - aTimeAhead + kCslReceiveTimeAhead;
        winDuration = aTimeAhead + aTimeAfter - kCslReceiveTimeAhead;

        mCslSampleTime += periodUs;
        nextFireTime    = ScheduleNextCslEvent(aTimeAhead, TimeMicro(winStart + winDuration));
    }
    else
    {
        nextFireTime = mCslSampleTime + aTimeAfter;
        mCslState    = kCslStateCslReceive;

        winStart    = TimerMicro::GetNow().GetValue();
        winDuration = aTimeAhead + aTimeAfter;

        mCslSampleTime += periodUs;
    }

    Get<Radio>().UpdateCslSampleTime(mCslSampleTime.GetValue());

    // Schedule reception window for any state except RX - so that CSL RX Window has lower priority
    // than scanning or RX after the data poll.
    if (RadioSupportsReceiveTiming() && (mState != kStateDisabled) && (mState != kStateReceive))
    {
        IgnoreError(Get<Radio>().ReceiveAt(mCslChannel, winStart, winDuration));
    }
    else if (mState == kStateCslSample)
    {
        IgnoreError(Get<Radio>().Receive(mCslChannel));
    }

    LogDebg("CSL window start %lu, duration %lu", ToUlong(winStart), ToUlong(winDuration));

    return nextFireTime;
}

TimeMicro SubMac::ScheduleNextCslEvent(uint32_t aTimeAhead, TimeMicro aBusyUntil)
{
    TimeMicro cslFireTime = mCslSampleTime - aTimeAhead;
#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    TimeMicro wakeupTime = mWakeupListenTime - kCslReceiveTimeAhead;
    bool      wakeupNext;
#endif

    mCslState = kCslStateCslQueued;

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    if (wakeupTime < aBusyUntil)
    {
        wakeupTime = aBusyUntil;
    }

    wakeupNext = mWakeupListenTime + kWakeupListenLength < cslFireTime;

    if (!mWakeupListenEnabled)
    {
        wakeupNext = false;
        if (mWakeupListenTime + 40000 < TimerMicro::GetNow())
        {
            mWakeupListenTime += static_cast<uint32_t>(mWakeupListenPeriod) * 256 * kUsPerTenSymbols;
        }
    }

    if (wakeupNext)
    {
        mCslState = kCslStateWakeupQueued;
        cslFireTime = mWakeupListenTime - 1000;
    }
#endif

    OT_UNUSED_VARIABLE(aBusyUntil);

    return cslFireTime;
}

void SubMac::GetCslWindowEdges(uint32_t &aAhead, uint32_t &aAfter)
{
    uint32_t semiPeriod = mCslPeriod * kUsPerTenSymbols / 2;
    uint32_t curTime, semiWindow{ 0 };

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_LOCAL_TIME_SYNC
    curTime = TimerMicro::GetNow().GetValue();
#else
    curTime = static_cast<uint32_t>(otPlatRadioGetNow(&GetInstance()));
#endif

    if (Get<Mle::Mle>().GetParent().IsStateValid())
    {
        semiWindow = GetCslNeighborSemiWindow(Get<Mle::Mle>().GetParent(), curTime);
    }

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    if (Get<Mle::Mle>().GetParentCandidate().IsStateValid())
    {
        // There is a bug where the child update response gets missed. We add 5ms to address that as a temporary fix
        uint32_t newSemiWindow = GetCslNeighborSemiWindow(Get<Mle::Mle>().GetParentCandidate(), curTime) + 5000;
        if (newSemiWindow > semiWindow)
        {
            semiWindow = newSemiWindow;
        }
    }

    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateValid))
    {
        uint32_t childSemiWindow = GetCslNeighborSemiWindow(child, curTime);
        if (childSemiWindow > semiWindow)
        {
            semiWindow = childSemiWindow;
        }
    }
#endif

    aAhead = Min(semiPeriod, semiWindow + kMinReceiveOnAhead + kCslReceiveTimeAhead);
    aAfter = Min(semiPeriod, semiWindow + kMinReceiveOnAfter);
}

uint32_t SubMac::GetCslNeighborSemiWindow(CslInfo &aNeighbor, uint32_t aTime)
{
    uint32_t semiWindow;
    uint32_t elapsed = aTime - aNeighbor.GetCslLastSync().GetValue();

    semiWindow =
        static_cast<uint32_t>(static_cast<uint64_t>(elapsed) *
                              (Get<Radio>().GetCslAccuracy() + aNeighbor.GetCslAccuracy().GetClockAccuracy()) / 1000000);
    semiWindow += aNeighbor.GetCslAccuracy().GetUncertaintyInMicrosec() + Get<Radio>().GetCslUncertainty() * 10;

    return semiWindow;
}

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
TimeMicro SubMac::HandleWakeupListenWindowBegin(uint32_t aTimeAhead)
{
    TimeMicro nextFireTime;

    if (RadioSupportsReceiveTiming())
    {
        TimeMicro winEnd = TimeMicro(mWakeupListenTime + kWakeupListenLength);
        IgnoreError(Get<Radio>().ReceiveAt(mWakeupListenChannel, mWakeupListenTime.GetValue(), kWakeupListenLength));

        mWakeupListenTime += static_cast<uint32_t>(mWakeupListenPeriod) * 256 * kUsPerTenSymbols;

        nextFireTime = ScheduleNextCslEvent(aTimeAhead, winEnd);
    }
    else
    {
        IgnoreError(Get<Radio>().Receive(mWakeupListenChannel));
        mCslState = kCslStateWakeupReceive;

        mWakeupListenTime += static_cast<uint32_t>(mWakeupListenPeriod) * 256 * kUsPerTenSymbols;

        nextFireTime = TimerMicro::GetNow() + kWakeupListenLength + kCslReceiveTimeAhead;
    }

    LogDebg("Wakeup window start %lu, %lu", ToUlong(mWakeupListenChannel), ToUlong(TimerMicro::GetNow().GetValue()));
    return nextFireTime;
}
#endif

#if OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE
void SubMac::LogReceived(RxFrame *aFrame)
{
    static constexpr uint8_t kLogStringSize = 72;

    String<kLogStringSize> logString;
    Address                dst;
    int32_t                deviation;
    uint32_t               sampleTime, ahead, after;

    IgnoreError(aFrame->GetDstAddr(dst));

    VerifyOrExit((dst.GetType() == Address::kTypeShort && dst.GetShort() == GetShortAddress()) ||
                 (dst.GetType() == Address::kTypeExtended && dst.GetExtended() == GetExtAddress()));

    LogDebg("Received frame in state (SubMac %s, CSL %s), timestamp %lu", StateToString(mState),
            mIsCslSampling ? "CslSample" : "CslSleep",
            ToUlong(static_cast<uint32_t>(aFrame->mInfo.mRxInfo.mTimestamp)));

    VerifyOrExit(mState == kStateCslSample);

    GetCslWindowEdges(ahead, after);
    ahead -= kMinReceiveOnAhead + kCslReceiveTimeAhead;

    sampleTime = mCslSampleTime.GetValue() - mCslPeriod * kUsPerTenSymbols;
    deviation  = aFrame->mInfo.mRxInfo.mTimestamp + kRadioHeaderPhrDuration - sampleTime;

    // This logs three values (all in microseconds):
    // - Absolute sample time in which the CSL receiver expected the MHR of the received frame.
    // - Allowed margin around that time accounting for accuracy and uncertainty from both devices.
    // - Real deviation on the reception of the MHR with regards to expected sample time. This can
    //   be due to clocks drift and/or CSL Phase rounding error.
    // This means that a deviation absolute value greater than the margin would result in the frame
    // not being received out of the debug mode.
    logString.Append("Expected sample time %lu, margin ±%lu, deviation %ld", ToUlong(sampleTime), ToUlong(ahead),
                     static_cast<long>(deviation));

    // Treat as a warning when the deviation is not within the margins. Neither kCslReceiveTimeAhead
    // or kMinReceiveOnAhead/kMinReceiveOnAfter are considered for the margin since they have no
    // impact on understanding possible deviation errors between transmitter and receiver. So in this
    // case only `ahead` is used, as an allowable max deviation in both +/- directions.
    if ((deviation + ahead > 0) && (deviation < static_cast<int32_t>(ahead)))
    {
        LogDebg("%s", logString.AsCString());
    }
    else
    {
        LogWarn("%s", logString.AsCString());
    }

exit:
    return;
}
#endif

} // namespace Mac
} // namespace ot

#endif // OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
