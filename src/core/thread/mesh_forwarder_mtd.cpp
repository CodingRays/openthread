/*
 *  Copyright (c) 2018, The OpenThread Authors.
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
 *   This file implements MTD-specific mesh forwarding of IPv6/6LoWPAN messages.
 */

#include "mesh_forwarder.hpp"
#include "common/message.hpp"

#if OPENTHREAD_MTD

#include "common/locator_getters.hpp"
#include "common/log.hpp"

namespace ot {

RegisterLogModule("Mle");

void MeshForwarder::SendMessage(OwnedPtr<Message> aMessagePtr)
{
    Message &message = *aMessagePtr.Release();

    message.SetOffset(0);
    message.SetDatagramTag(0);
    message.SetTimestampToNow();
    mSendQueue.Enqueue(message);

#if OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    switch (message.GetType())
    {
    case Message::kTypeIp6:
    {
        Ip6::Header  ip6Header;
        bool         childFound = false;

        IgnoreError(message.Read(0, ip6Header));

        if (message.GetSubType() == Message::kSubTypeMleAnnounce || message.GetSubType() == Message::kSubTypeMleDiscoverRequest)
        {
            message.SetDirectTransmission();
            childFound = true;
        }
        else if (message.IsDirectTransmission())
        {
            childFound = true;
        }
        else if (Get<Mle::Mle>().IsRoutingLocator(ip6Header.GetDestination()))
        {
            uint16_t rloc = ip6Header.GetDestination().GetIid().GetLocator();

            for (Child &child : Get<ChildTable>().Iterate(Child::kInStateValid))
            {
                if (Mle::IsSubChildOf(rloc, child.GetRloc16(), child.GetRlocPrefixLength()))
                {
                    if (!child.IsRxOnWhenIdle())
                    {
                        mIndirectSender.AddMessageForSleepyChild(message, child);
                    }
                    else
                    {
                        message.SetDirectTransmission();
                    }
                    childFound = true;
                    break;
                }
            }
        }
        else if (ip6Header.GetDestination().IsLinkLocalUnicast())
        {
            Mac::Address address;
            ip6Header.GetDestination().GetIid().ConvertToMacAddress(address);

            IndirectReachable *neighbor = Get<NeighborTable>().FindIndirectReachable(address, Neighbor::kInStateAnyExceptInvalid);
            if (neighbor != nullptr) 
            {
                if (!neighbor->IsRxOnWhenIdle())
                {
                    mIndirectSender.AddMessageForSleepyChild(message, *neighbor);
                }
                else
                {
                    message.SetDirectTransmission();
                }
                childFound = true;
            }
        }
        
        if (!childFound) 
        {
            Parent &parent = Get<Mle::Mle>().GetParent();
            if (parent.IsStateValid() && !parent.IsRxOnWhenIdle())
            {
                mIndirectSender.AddMessageForSleepyChild(message, parent);
            }
            else
            {
                message.SetDirectTransmission();
            }
        }

        break;
    }

    case Message::kTypeSupervision:
    {
        IndirectReachable *child = Get<ChildSupervisor>().GetDestination(message);
        OT_ASSERT((child != nullptr) && !child->IsRxOnWhenIdle());

        mIndirectSender.AddMessageForSleepyChild(message, *child);
        break;
    }

    default:
        message.SetDirectTransmission();
        break;
    }
#else // OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
    message.SetDirectTransmission();
#endif

#if (OPENTHREAD_CONFIG_MAX_FRAMES_IN_DIRECT_TX_QUEUE > 0)
    ApplyDirectTxQueueLimit(message);
#endif

    mScheduleTransmissionTask.Post();
}

Error MeshForwarder::EvictMessage(Message::Priority aPriority)
{
    Error    error = kErrorNotFound;
    Message *message;

#if OPENTHREAD_CONFIG_DELAY_AWARE_QUEUE_MANAGEMENT_ENABLE
    error = RemoveAgedMessages();
    VerifyOrExit(error == kErrorNotFound);
#endif

    VerifyOrExit((message = mSendQueue.GetTail()) != nullptr);

    if (message->GetPriority() < static_cast<uint8_t>(aPriority))
    {
        EvictMessage(*message);
        ExitNow(error = kErrorNone);
    }

exit:
    return error;
}

} // namespace ot

#endif // OPENTHREAD_MTD
