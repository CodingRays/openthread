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
 *   This file includes definitions for MLE functionality required by the Thread Router and Leader roles.
 */

#ifndef MLE_SUB_CHILD_HPP_
#define MLE_SUB_CHILD_HPP_

#include "openthread-core-config.h"

#include <openthread/thread_mtd.h>

#include "thread/mle.hpp"
#include "thread/child_table.hpp"

namespace ot {
namespace Mle {

#if OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
class MleSubChild : public Mle
{
    friend class Mle;
    friend class ot::Instance;
    friend class ot::TimeTicker;

public:
    explicit MleSubChild(Instance &aInstance);

    /**
     * Returns the csl round trip time to the ftd parent.
     *
     * If this device is not currently attached returns 0.
     *
     * @return  The csl round trip time to the ftd parent.
     *
     */
    uint32_t GetCslRoundTripTime(void) const;

    /**
     * Returns whether this device is currently attached as a non direct child.
     *
     * @retval TRUE   If this device is attached and the parent is a sub child.
     * @retval FALSE  If this device is not attached to the network or the parent is a FTD.
     *
     */
    bool IsNonDirectChild(void) const { return mParent.IsStateValid() && mParent.IsSubChild(); }

    /**
     * Returns the rloc prefix length assigned to this device. Must only be called
     * when this device is attached.
     *
     * @return  The rloc prefix length assigned to this device.
     *
     */
    uint8_t GetRlocPrefixLength(void) const { return mPrefixLength; }

    /**
     * Indicates whether the device can accept sub children.
     *
     * @returns TRUE if the device can accept sub children. FALSE otherwise.
     *
     */
    bool HasSubChildAddressSpace(void) const { return IsChild() && mPrefixLength < 9; }

    void RemoveNeighbor(IndirectReachable &aNeighbor);

private:
    void HandleParentRequest(RxInfo &aRxInfo);
    void HandleSubChildParentResponse(RxInfo &aRxInfo);
    void HandleLinkRequest(RxInfo &aRxInfo);
    void HandleLinkAccept(RxInfo &aRxInfo);
    void HandleChildIdRequest(RxInfo &aRxInfo);
    void HandleSubChildIdResponse(RxInfo &aRxInfo, uint16_t aRloc16, Mac::ExtAddress &aExtAddress);
    void HandleSubChildDetachResponse(RxInfo &aRxInfo, const Mac::ExtAddress &aExtAddress);

    void ForwardFromSubChildUpdateRequest(RxInfo &aRxInfo);
    void ForwardToSubChildUpdateRequest(RxInfo &aRxInfo, uint16_t aDstRloc);
    void ForwardFromSubChildUpdateResponse(RxInfo &aRxInfo);
    void ForwardToSubChildUpdateResponse(RxInfo &aRxInfo, uint16_t aDstRloc);

    Error SendParentResponse(Child &aChild, const RxChallenge &aChallenge);
    Error SendLinkRequest(void);
    Error SendLinkAccept(Child &aChild);
    Error SendDetachMessage(Child &aChild);

    void HandleDetach(void);

    void HandleTimeTick(void);

    ChildTable mChildTable;
};

typedef MleSubChild MleRouter;
#endif // OPENTHREAD_MTD && OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE

} // namespace Mle
} // namespace ot

#endif // MLE_SUB_CHILD_HPP_

