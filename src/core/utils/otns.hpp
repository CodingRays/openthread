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

/**
 * @file
 *   This file wraps the calls to platform OTNS abstractions.
 */

#ifndef UTILS_OTNS_HPP_
#define UTILS_OTNS_HPP_

#include "openthread-core-config.h"

#if (OPENTHREAD_MTD || OPENTHREAD_FTD) && OPENTHREAD_CONFIG_OTNS_ENABLE

#include <openthread/thread.h>
#include <openthread/thread_ftd.h>
#include <openthread/platform/otns.h>

#include "coap/coap_message.hpp"
#include "common/locator.hpp"
#include "common/non_copyable.hpp"
#include "common/notifier.hpp"
#include "common/string.hpp"
#include "mac/mac_frame.hpp"
#include "mac/mac_types.hpp"
#include "net/ip6_address.hpp"
#include "thread/neighbor.hpp"
#include "thread/neighbor_table.hpp"

namespace ot {
namespace Utils {

/**
 * Implements the OTNS Stub that interacts with OTNS.
 */
class Otns : public InstanceLocator, private NonCopyable
{
    friend class ot::Notifier;

public:
    /**
     * Initializes the object.
     *
     * @param[in]  aInstance     A reference to the OpenThread instance.
     */
    explicit Otns(Instance &aInstance)
        : InstanceLocator(aInstance)
    {
    }

    /**
     * Emits radio short address to OTNS when changed.
     *
     * @param[in]  aShortAddress  The new short address.
     */
    void EmitShortAddress(uint16_t aShortAddress) const;

    /**
     * Emits radio extended address to OTNS when changed.
     *
     * @param[in]  aExtAddress  The new extended address.
     */
    void EmitExtendedAddress(const Mac::ExtAddress &aExtAddress) const;

    /**
     * Emits ping request information to OTNS when sending.
     *
     * @param[in]  aPeerAddress  The peer address of the ping request.
     * @param[in]  aPingLength   The data length of the ping request.
     * @param[in]  aTimestamp    The timestamp of the ping request.
     * @param[in]  aHopLimit     The hop limit of the ping request.
     */
    void EmitPingRequest(const Ip6::Address &aPeerAddress,
                         uint16_t            aPingLength,
                         uint32_t            aTimestamp,
                         uint8_t             aHopLimit) const;

    /**
     * Emits ping reply information to OTNS when received.
     *
     * @param[in]  aPeerAddress  The peer address of the ping request.
     * @param[in]  aPingLength   The data length of the ping reply.
     * @param[in]  aTimestamp    The timestamp of the ping reply.
     * @param[in]  aHopLimit     The hop limit of the ping reply.
     */
    void EmitPingReply(const Ip6::Address &aPeerAddress,
                       uint16_t            aPingLength,
                       uint32_t            aTimestamp,
                       uint8_t             aHopLimit) const;

    /**
     * Emits a neighbor table event to OTNS when a neighbor is added or removed.
     *
     * @param[in]  aEvent     The event type.
     * @param[in]  aNeighbor  The neighbor that is added or removed.
     */
    void EmitNeighborChange(NeighborTable::Event aEvent, const Neighbor &aNeighbor) const;

    /**
     * Emits a transmit event to OTNS.
     *
     * @param[in]  aFrame  The frame of the transmission.
     */
    void EmitTransmit(const Mac::TxFrame &aFrame) const;

    /**
     * Emits the device mode to OTNS.
     *
     * @param[in] aMode The device mode.
     */
    void EmitDeviceMode(Mle::DeviceMode aMode) const;

    /**
     * Emits the sending COAP message info to OTNS.
     *
     * @param[in] aMessage      The sending COAP message.
     * @param[in] aMessageInfo  The message info.
     */
    void EmitCoapSend(const Coap::Message &aMessage, const Ip6::MessageInfo &aMessageInfo) const;

    /**
     * Emits the COAP message sending failure to OTNS.
     *
     * @param[in] aError        The error in sending the COAP message.
     * @param[in] aMessage      The COAP message failed to send.
     * @param[in] aMessageInfo  The message info.
     */
    void EmitCoapSendFailure(Error aError, Coap::Message &aMessage, const Ip6::MessageInfo &aMessageInfo) const;

    /**
     * Emits the received COAP message info to OTNS.
     *
     * @param[in] aMessage      The received COAP message.
     * @param[in] aMessageInfo  The message info.
     */
    void EmitCoapReceive(const Coap::Message &aMessage, const Ip6::MessageInfo &aMessageInfo) const;

private:
    static constexpr uint16_t kStatusStringLength = 128;

    using StatusString = String<kStatusStringLength>;

    void EmitStatus(const StatusString &aString) const;
    void EmitStatus(const char *aFmt, ...) const OT_TOOL_PRINTF_STYLE_FORMAT_ARG_CHECK(2, 3);
    void EmitCoapStatus(const char             *aAction,
                        const Coap::Message    &aMessage,
                        const Ip6::MessageInfo &aMessageInfo,
                        Error                  *aError = nullptr) const;

    void HandleNotifierEvents(Events aEvents) const;
};

} // namespace Utils
} // namespace ot

#endif //(OPENTHREAD_MTD || OPENTHREAD_FTD) && OPENTHREAD_CONFIG_OTNS_ENABLE

#endif // UTILS_OTNS_HPP_
