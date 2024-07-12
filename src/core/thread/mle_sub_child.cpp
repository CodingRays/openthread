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

#include "mle_sub_child.hpp"

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE

#include "openthread-core-config.h"

#include "instance/instance.hpp"
#include "common/locator.hpp"
#include "common/debug.hpp"
#include "common/as_core_type.hpp"
#include "common/code_utils.hpp"
#include "common/debug.hpp"
#include "common/locator_getters.hpp"
#include "thread/child_supervision.hpp"
#include "mac/sub_mac.hpp"

namespace ot {
namespace Mle {

RegisterLogModule("Mle");

MleSubChild::MleSubChild(Instance &aInstance)
    : Mle(aInstance)
    , mChildTable(aInstance)
{
}

uint32_t MleSubChild::GetCslRoundTripTime(void) const
{
    uint32_t value = 0;

    if (mParent.IsStateValid())
    {
        value = mParent.GetCslRoundTripTime() + Get<Mac::Mac>().GetCslPeriod();
    }

    return value;
}

void MleSubChild::RemoveNeighbor(IndirectReachable &aNeighbor)
{
    VerifyOrExit(!aNeighbor.IsStateInvalid());

    Get<IndirectSender>().ClearAllMessagesForSleepyChild(aNeighbor);

    if (&aNeighbor == &mParent)
    {
        if (IsChild())
        {
            IgnoreError(BecomeDetached());
        }
    }
    else if (&aNeighbor == &mParentCandidate)
    {
        ClearParentCandidate();
    }
    else
    {
        OT_ASSERT(Get<ChildTable>().Contains(aNeighbor));

        if (aNeighbor.IsStateValidOrRestoring())
        {
            Get<Mac::SubMac>().UpdateCslNeighbors();
            mNeighborTable.Signal(NeighborTable::kChildRemoved, aNeighbor);
        }

        aNeighbor.SetState(Neighbor::kStateDetachPending);
        IgnoreError(SendDetachMessage(static_cast<Child&>(aNeighbor)));
    }

    aNeighbor.GetLinkInfo().Clear();
    if (!aNeighbor.IsStateDetachPending())
    {
        aNeighbor.SetState(Neighbor::kStateInvalid);
    }

exit:
    return;
}

void MleSubChild::HandleParentRequest(RxInfo &aRxInfo)
{
    Error           error = kErrorNone;
    Mac::ExtAddress extAddr;
    RxChallenge     challenge;
    Child          *child;
    uint16_t        version;
    uint8_t         scanMask;
    ChannelTlvValue channel;

    Log(kMessageReceive, kTypeParentRequest, aRxInfo.mMessageInfo.GetPeerAddr());

    VerifyOrExit(!IsDetached() && !IsAttaching(), error = kErrorDrop);
    VerifyOrExit(HasSubChildAddressSpace(), error = kErrorInvalidState);

    aRxInfo.mMessageInfo.GetPeerAddr().GetIid().ConvertToExtAddress(extAddr);

    SuccessOrExit(error = Tlv::Find<VersionTlv>(aRxInfo.mMessage, version));
    VerifyOrExit(version >= kThreadVersion1p3, error = kErrorParse);

    SuccessOrExit(error = Tlv::Find<ScanMaskTlv>(aRxInfo.mMessage, scanMask));
    VerifyOrExit(ScanMaskTlv::IsSubChildFlagSet(scanMask));

    SuccessOrExit(error = Tlv::Find<CslChannelTlv>(aRxInfo.mMessage, channel));

    SuccessOrExit(error = aRxInfo.mMessage.ReadChallengeTlv(challenge));

    child = Get<ChildTable>().FindChild(extAddr, Child::kInStateAnyExceptInvalid);

    // We dont want to reset a child that is already fully attached as it could
    // be simply scanning for a better parent. However we still have to process
    // it as it could have detached.
    if (child == nullptr || !child->IsStateValid())
    {
        if (child == nullptr)
        {
            VerifyOrExit((child = Get<ChildTable>().GetNewChild()) != nullptr, error = kErrorNoBufs);
        }

        InitNeighbor(*child, aRxInfo);
        child->SetState(Neighbor::kStateSubChildParentRequest);
        child->SetRloc16(kInvalidRloc16);
        child->SetCslChannel(channel.GetChannel());

        OT_ASSERT(child->GetExtAddress() == extAddr);

        child->SetVersion(version);
    }

    if (!child->IsStateValidOrRestoring())
    {
        // Configure attach timeouts
        child->SetLastHeard(TimerMilli::GetNow());
        child->SetTimeout(100);
        child->SetSupervisionInterval(0);
    }

    aRxInfo.mClass = RxInfo::kPeerMessage;
    ProcessKeySequence(aRxInfo);

    IgnoreError(SendParentResponse(*child, challenge));

exit:
    LogProcessError(kTypeParentRequest, error);
}

void MleSubChild::HandleLinkRequest(RxInfo &aRxInfo)
{
    Error            error;
    Mac::ExtAddress  extAddr;
    RxChallenge      response;
    Child           *child;
    uint32_t         linkFrameCounter;
    uint32_t         mleFrameCounter;
    Mac::CslAccuracy cslAccuracy;

    Log(kMessageReceive, kTypeParentRequest, aRxInfo.mMessageInfo.GetPeerAddr());

    VerifyOrExit(!IsDetached() && !IsAttaching(), error = kErrorDrop);
    VerifyOrExit(HasSubChildAddressSpace(), error = kErrorInvalidState);

    aRxInfo.mMessageInfo.GetPeerAddr().GetIid().ConvertToExtAddress(extAddr);

    VerifyOrExit((child = Get<ChildTable>().FindChild(extAddr, Child::kInStateAnyExceptInvalid)) != nullptr, error = kErrorNotFound);
    VerifyOrExit(child->GetState() == Neighbor::kStateValid || child->GetState() == Neighbor::kStateSubChildParentResponse);

    SuccessOrExit(error = aRxInfo.mMessage.ReadResponseTlv(response));
    VerifyOrExit(response == child->GetChallenge(), error = kErrorSecurity);
    
    SuccessOrExit(error = aRxInfo.mMessage.ReadFrameCounterTlvs(linkFrameCounter, mleFrameCounter));

    SuccessOrExit(error = aRxInfo.mMessage.ReadCslClockAccuracyTlv(cslAccuracy));
    child->SetCslAccuracy(cslAccuracy);

    if (!child->IsStateValid())
    {
        // Make sure we remove all messages from any previous attach attempt
        Get<IndirectSender>().ClearAllMessagesForSleepyChild(*child);
    }

    if (child->GetState() == Neighbor::kStateSubChildParentResponse)
    {
        child->SetState(Neighbor::kStateSubChildLinkAccept);
    }
    child->GetLinkFrameCounters().SetAll(linkFrameCounter);
    child->SetLinkAckFrameCounter(linkFrameCounter);
    child->SetMleFrameCounter(mleFrameCounter);

    // Who should be responsible for defining this?
    child->SetTimeout(100);
    child->SetLastHeard(TimerMilli::GetNow());

    Get<Mac::SubMac>().UpdateCslNeighbors();

    IgnoreError(SendLinkAccept(*child));

exit:
    return;
}

void MleSubChild::HandleLinkAccept(RxInfo &aRxInfo)
{
    Error           error = kErrorNone;
    Mac::ExtAddress extAddr;

    Log(kMessageReceive, kTypeLinkAccept, aRxInfo.mMessageInfo.GetPeerAddr());

    aRxInfo.mMessageInfo.GetPeerAddr().GetIid().ConvertToExtAddress(extAddr);

    VerifyOrExit(mParentCandidate.GetExtAddress() == extAddr, error = kErrorNotFound);
    VerifyOrExit(mParentCandidate.GetState() == Neighbor::kStateSubChildLinkRequest, error = kErrorInvalidState);

    mParentCandidate.SetState(Neighbor::kStateSubChildLinkAccept);

    // We dont need to do anything here as the child id request is sent in HandleAttachTimer

exit:
    LogProcessError(kTypeLinkAccept, error);
}

void MleSubChild::HandleChildIdRequest(RxInfo &aRxInfo)
{
    Error           error;
    Mac::ExtAddress extAddr;
    uint16_t        rloc16;
    bool            extPresent;
    Ip6::Address    destination;
    TxMessage*      message = nullptr;

    Log(kMessageReceive, kTypeChildIdRequest, aRxInfo.mMessageInfo.GetPeerAddr());

    VerifyOrExit(IsChild(), error = kErrorInvalidState);

    SuccessOrExit(error = aRxInfo.mMessage.ReadFromSubChildTlv(rloc16, extAddr, extPresent));
    VerifyOrExit(extPresent, error = kErrorParse);

    if (rloc16 == GetRloc16())
    {
        // The child is attempting to attach over this device so verify state
        uint32_t timeout;

        Child *child = Get<ChildTable>().FindChild(extAddr, Neighbor::kInStateAnyExceptInvalid);
        VerifyOrExit(child != nullptr, error = kErrorInvalidState);

        if (!child->IsRxOnWhenIdle() && !child->IsCslSynchronized())
        {
            LogWarn("Received child id request from sleepy child but we are not csl synchronized. Aborting");
            ExitNow(error = kErrorInvalidState);
        }

        SuccessOrExit(error = Tlv::Find<TimeoutTlv>(aRxInfo.mMessage, timeout));

        child->SetState(Neighbor::kStateSubChildIdRequest);
        child->SetTimeout(timeout);
        child->SetSupervisionInterval(0);
        child->SetLastHeard(TimerMilli::GetNow());
    }

    destination.SetToLinkLocalAddress(mParent.GetExtAddress());

    // TODO better investigate this offset behaviour
    aRxInfo.mMessage.SetOffset(aRxInfo.mMessage.GetOffset() - 12);
    message = static_cast<TxMessage*>(aRxInfo.mMessage.Clone());
    message->SetOrigin(Message::kOriginHostTrusted);
    VerifyOrExit(message != nullptr, error = kErrorNoBufs);

    SuccessOrExit(error = message->SendTo(destination)); 

exit:
    FreeMessageOnError(message, error);
    LogProcessError(kTypeChildIdRequest, error);
}

void MleSubChild::HandleSubChildIdResponse(RxInfo &aRxInfo, uint16_t aRloc16, Mac::ExtAddress &aExtAddress)
{
    Error        error = kErrorNone;
    Child       *nextHop = nullptr;
    Ip6::Address destination;
    TxMessage   *message = nullptr;
    
    VerifyOrExit(IsChild());

    if (aRloc16 == GetRloc16())
    {
        // This device is the parent of the attaching device. So update state accordingly.

        uint16_t childRloc16;
        uint8_t  prefixLength;

        nextHop = Get<ChildTable>().FindChild(aExtAddress, Neighbor::kInStateWithSecurityReady);
        VerifyOrExit(nextHop != nullptr, error = kErrorNotFound);

        if (!nextHop->IsRxOnWhenIdle() && !nextHop->IsCslSynchronized())
        {
            LogWarn("Received child id response for sleepy child but we are not csl synchronized. Aborting");
            RemoveNeighbor(*nextHop);
            ExitNow(error = kErrorInvalidState);
        }

        SuccessOrExit(error = Tlv::Find<Address16Tlv>(aRxInfo.mMessage, childRloc16));
        SuccessOrExit(error = Tlv::Find<RlocPrefixLengthTlv>(aRxInfo.mMessage, prefixLength));

        nextHop->SetState(Neighbor::kStateValid);
        nextHop->SetRloc16(childRloc16);
        nextHop->SetRlocPrefixLength(prefixLength);
        nextHop->SetSupervisionInterval(nextHop->GetTimeout() / 2);

        Get<Mac::SubMac>().UpdateCslNeighbors();

        // This link is allowed to time out provided the child responds before its link times out too
        nextHop->SetLastHeard(TimerMilli::GetNow());

        Get<NeighborTable>().Signal(NeighborTable::kChildAdded, *nextHop);

        destination.SetToLinkLocalAddress(aExtAddress);
    }
    else
    {
        // This device is not the parent of the attaching child. We need to forward the message.

        for (Child &child : Get<ChildTable>().Iterate(Neighbor::kInStateValid))
        {
            if (IsSubChildOf(aRloc16, child.GetRloc16(), child.GetRlocPrefixLength()))
            {
                nextHop = &child;
                break;
            }
        }

        VerifyOrExit(nextHop != nullptr, error = kErrorNoRoute);

        destination.SetToLinkLocalAddress(nextHop->GetExtAddress());
    }

    aRxInfo.mMessage.SetOffset(aRxInfo.mMessage.GetOffset() - 12);
    message = static_cast<TxMessage*>(aRxInfo.mMessage.Clone());
    message->SetOrigin(Message::kOriginHostTrusted);
    VerifyOrExit(message != nullptr, error = kErrorNoBufs);

    SuccessOrExit(error = message->SendTo(destination));

exit:
    FreeMessageOnError(message, error);
    LogProcessError(kTypeChildIdResponse, error);
}

void MleSubChild::HandleSubChildDetachResponse(RxInfo &aRxInfo, const Mac::ExtAddress &aExtAddress)
{
    OT_UNUSED_VARIABLE(aRxInfo);

    Child *child = Get<ChildTable>().FindChild(aExtAddress, Child::kInStateDetachPending);
    VerifyOrExit(child != nullptr);
   
    child->SetState(Child::kStateInvalid);

exit:
    return;
}

void MleSubChild::ForwardFromSubChildUpdateRequest(RxInfo &aRxInfo)
{
    Error        error = kErrorNone;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    VerifyOrExit(IsChild());

    destination.SetToLinkLocalAddress(mParent.GetExtAddress());

    aRxInfo.mMessage.SetOffset(aRxInfo.mMessage.GetOffset() - 12);
    message = static_cast<TxMessage*>(aRxInfo.mMessage.Clone());
    message->SetOrigin(Message::kOriginHostTrusted);
    VerifyOrExit(message != nullptr, error = kErrorNoBufs);

    SuccessOrExit(error = message->SendTo(destination));

exit:
    FreeMessageOnError(message, error);
    LogProcessError(kTypeGenericUdp, error);
}

void MleSubChild::ForwardToSubChildUpdateRequest(RxInfo &aRxInfo, uint16_t aDstRloc)
{
    Error        error = kErrorNone;
    Child       *nextHop = nullptr;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    VerifyOrExit(IsChild());

    for (Child &child : Get<ChildTable>().Iterate(Neighbor::kInStateValid))
    {
        if (IsSubChildOf(aDstRloc, child.GetRloc16(), child.GetRlocPrefixLength()))
        {
            nextHop = &child;
        }
    }

    VerifyOrExit(nextHop != nullptr, error = kErrorNoRoute);

    destination.SetToLinkLocalAddress(nextHop->GetExtAddress());

    aRxInfo.mMessage.SetOffset(aRxInfo.mMessage.GetOffset() - 12);
    message = static_cast<TxMessage*>(aRxInfo.mMessage.Clone());
    message->SetOrigin(Message::kOriginHostTrusted);
    VerifyOrExit(message != nullptr, error = kErrorNoBufs);

    SuccessOrExit(error = message->SendTo(destination));

exit:
    FreeMessageOnError(message, error);
    LogProcessError(kTypeGenericUdp, error);
}

void MleSubChild::ForwardFromSubChildUpdateResponse(RxInfo &aRxInfo)
{
    Error        error = kErrorNone;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    VerifyOrExit(IsChild());

    destination.SetToLinkLocalAddress(mParent.GetExtAddress());

    aRxInfo.mMessage.SetOffset(aRxInfo.mMessage.GetOffset() - 12);
    message = static_cast<TxMessage*>(aRxInfo.mMessage.Clone());
    message->SetOrigin(Message::kOriginHostTrusted);
    VerifyOrExit(message != nullptr, error = kErrorNoBufs);
    
    SuccessOrExit(error = message->SendTo(destination));

exit:
    FreeMessageOnError(message, error);
    LogProcessError(kTypeGenericUdp, error);
}

void MleSubChild::ForwardToSubChildUpdateResponse(RxInfo &aRxInfo, uint16_t aDstRloc)
{ 
    Error        error = kErrorNone;
    Child       *nextHop = nullptr;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    VerifyOrExit(IsChild());

    for (Child &child : Get<ChildTable>().Iterate(Neighbor::kInStateValid))
    {
        if (IsSubChildOf(aDstRloc, child.GetRloc16(), child.GetRlocPrefixLength()))
        {
            nextHop = &child;
        }
    }

    VerifyOrExit(nextHop != nullptr, error = kErrorNoRoute);

    destination.SetToLinkLocalAddress(nextHop->GetExtAddress());

    aRxInfo.mMessage.SetOffset(aRxInfo.mMessage.GetOffset() - 12);
    message = static_cast<TxMessage*>(aRxInfo.mMessage.Clone());
    message->SetOrigin(Message::kOriginHostTrusted);
    VerifyOrExit(message != nullptr, error = kErrorNoBufs);

    SuccessOrExit(error = message->SendTo(destination));

exit:
    FreeMessageOnError(message, error);
    LogProcessError(kTypeGenericUdp, error);
}

Error MleSubChild::SendParentResponse(Child &aChild, const RxChallenge &aChallenge)
{
    Error           error = kErrorNone;
    Ip6::Address    destination;
    TxMessage      *message;
    uint16_t        delay;

    VerifyOrExit((message = NewMleMessage(kCommandParentResponse)) != nullptr, error = kErrorNoBufs);
    message->SetSubType(TxMessage::kSubTypeMleSubChildParentResponse);
    message->SetDirectTransmission();
    message->SetChannel(aChild.GetCslChannel());

    SuccessOrExit(error = message->AppendSourceAddressTlv());
    SuccessOrExit(error = message->AppendModeTlv(mDeviceMode));
    SuccessOrExit(error = message->AppendLinkFrameCounterTlv());
    SuccessOrExit(error = message->AppendMleFrameCounterTlv());
    SuccessOrExit(error = message->AppendResponseTlv(aChallenge));
    SuccessOrExit(error = message->AppendCslClockAccuracyTlv());
    SuccessOrExit(error = message->AppendSubChildLinkTlv());

    aChild.GenerateChallenge();
    SuccessOrExit(error = message->AppendChallengeTlv(aChild.GetChallenge()));
    SuccessOrExit(error = message->AppendLinkMarginTlv(aChild.GetLinkInfo().GetLinkMargin()));
    SuccessOrExit(error = message->AppendVersionTlv());
    SuccessOrExit(error = message->AppendLeaderDataTlv());

    destination.SetToLinkLocalAddress(aChild.GetExtAddress());

    // TODO: Make the maximum here a constant and fine tune it
    delay = 1 + Random::NonCrypto::GetUint16InRange(0, 50);
    SuccessOrExit(error = message->SendAfterDelay(destination, delay));

    if (aChild.GetState() == Neighbor::kStateSubChildParentRequest)
    {
        aChild.SetState(Neighbor::kStateSubChildParentResponse);
    }

    Log(kMessageDelay, kTypeParentResponse, destination);

exit:
    FreeMessageOnError(message, error);
    LogSendError(kTypeParentResponse, error);

    return error;
}

Error MleSubChild::SendLinkRequest(void)
{
    Error        error;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    VerifyOrExit(mParentCandidate.GetState() == Neighbor::kStateSubChildParentResponse && mParentCandidate.IsSubChild(), error = kErrorInvalidState);
    VerifyOrExit((message = NewMleMessage(kCommandLinkRequest)) != nullptr, error = kErrorNoBufs);
    message->SetSubType(TxMessage::kSubTypeMleSubChildLinkRequest);
    message->SetDirectTransmission();
    message->SetChannel(mParentCandidate.GetCslChannel());

    SuccessOrExit(error = message->AppendResponseTlv(mParentCandidate.mRxChallenge));
    SuccessOrExit(error = message->AppendLinkFrameCounterTlv());
    SuccessOrExit(error = message->AppendMleFrameCounterTlv());
    SuccessOrExit(error = message->AppendCslClockAccuracyTlv());

    destination.SetToLinkLocalAddress(mParentCandidate.GetExtAddress());
    SuccessOrExit(error = message->SendTo(destination));

    mParentCandidate.SetState(Neighbor::kStateSubChildLinkRequest);

    Log(kMessageSend, kTypeLinkRequest, destination);

exit:
    FreeMessageOnError(message, error);
    LogSendError(kTypeLinkRequest, error);

    return error;
}

Error MleSubChild::SendLinkAccept(Child &aChild)
{
    Error        error;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    VerifyOrExit((message = NewMleMessage(kCommandLinkAccept)) != nullptr, error = kErrorNoBufs);

    message->SetDirectTransmission();
    message->SetChannel(aChild.GetCslChannel());

    destination.SetToLinkLocalAddress(aChild.GetExtAddress());

    SuccessOrExit(error = message->SendTo(destination));

    Log(kMessageSend, kTypeLinkAccept, destination);

exit:
    FreeMessageOnError(message, error);
    LogSendError(kTypeLinkAccept, error);

    return error;
}

Error MleSubChild::SendDetachMessage(Child &aChild)
{
    Error        error;
    Ip6::Address destination;
    TxMessage   *message = nullptr;

    VerifyOrExit((message = NewMleMessage(kCommandChildUpdateRequest)) != nullptr, error = kErrorNoBufs);

    SuccessOrExit(error = message->AppendTimeoutTlv(0));
    SuccessOrExit(error = message->AppendFromSubChildTlv(aChild.GetExtAddress()));

    destination.SetToLinkLocalAddress(mParent.GetExtAddress());

    SuccessOrExit(error = message->SendTo(destination));
    aChild.SetLastHeard(TimerMilli::GetNow() + 4 * GetCslRoundTripTime() + 1000);

    Log(kMessageSend, kTypeSubChildDetach, destination);

exit:
    FreeMessageOnError(message, error);
    LogSendError(kTypeSubChildDetach, error);

    return error;
}

void MleSubChild::HandleDetach(void)
{
}

void MleSubChild::HandleTimeTick(void)
{
    TimeMilli now = TimerMilli::GetNow();

    for (Child &child : Get<ChildTable>().Iterate(Child::kInStateDetachPending))
    {
        // Were reusing the last heard time as the time for the next detach
        // transmission.
        if (child.GetLastHeard() < now)
        {
            SendDetachMessage(child);
        }
    }
}

} // namespace Mle
} // namespace ot
#endif
