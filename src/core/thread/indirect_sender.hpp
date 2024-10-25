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

#ifndef INDIRECT_SENDER_HPP_
#define INDIRECT_SENDER_HPP_

#include "openthread-core-config.h"

#if OPENTHREAD_FTD

#include "common/locator.hpp"
#include "common/message.hpp"
#include "common/non_copyable.hpp"
#include "mac/data_poll_handler.hpp"
#include "mac/mac_frame.hpp"
#include "thread/csl_tx_scheduler.hpp"
#include "thread/indirect_sender_frame_context.hpp"
#include "thread/mle_types.hpp"
#include "thread/src_match_controller.hpp"

namespace ot {

/**
 * @addtogroup core-mesh-forwarding
 *
 * @brief
 *   This module includes definitions for handling indirect transmissions.
 *
 * @{
 */

class IndirectNeighbor;
class Child;

/**
 * Implements indirect transmission.
 */
class IndirectSender : public InstanceLocator, public IndirectSenderBase, private NonCopyable
{
    friend class Instance;
    friend class DataPollHandler::Callbacks;
#if OPENTHREAD_FTD && OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    friend class CslTxScheduler::Callbacks;
#endif

public:
    /**
     * Defines all the neighbor info required for indirect transmission.
     *
     * `IndirectNeighbor` class publicly inherits from this class.
     */
    class NeighborInfo
    {
        friend class IndirectSender;
        friend class DataPollHandler;
        friend class CslTxScheduler;
        friend class SourceMatchController;

    public:
        /**
         * Returns the number of queued messages for the neighbor.
         *
         * @returns Number of queued messages for the neighbor.
         */
        uint16_t GetIndirectMessageCount(void) const { return mQueuedMessageCount; }

    private:
        Message *GetIndirectMessage(void) { return mIndirectMessage; }
        void     SetIndirectMessage(Message *aMessage) { mIndirectMessage = aMessage; }

        uint16_t GetIndirectFragmentOffset(void) const { return mIndirectFragmentOffset; }
        void     SetIndirectFragmentOffset(uint16_t aFragmentOffset) { mIndirectFragmentOffset = aFragmentOffset; }

        bool GetIndirectTxSuccess(void) const { return mIndirectTxSuccess; }
        void SetIndirectTxSuccess(bool aTxStatus) { mIndirectTxSuccess = aTxStatus; }

        bool IsIndirectSourceMatchShort(void) const { return mUseShortAddress; }
        void SetIndirectSourceMatchShort(bool aShort) { mUseShortAddress = aShort; }

        bool IsIndirectSourceMatchPending(void) const { return mSourceMatchPending; }
        void SetIndirectSourceMatchPending(bool aPending) { mSourceMatchPending = aPending; }

        void IncrementIndirectMessageCount(void) { mQueuedMessageCount++; }
        void DecrementIndirectMessageCount(void) { mQueuedMessageCount--; }
        void ResetIndirectMessageCount(void) { mQueuedMessageCount = 0; }

        bool IsWaitingForMessageUpdate(void) const { return mWaitingForMessageUpdate; }
        void SetWaitingForMessageUpdate(bool aNeedsUpdate) { mWaitingForMessageUpdate = aNeedsUpdate; }

        const Mac::Address &GetMacAddress(Mac::Address &aMacAddress) const;

        Message *mIndirectMessage;             // Current indirect message.
        uint16_t mIndirectFragmentOffset : 14; // 6LoWPAN fragment offset for the indirect message.
        bool     mIndirectTxSuccess : 1;       // Indicates tx success/failure of current indirect message.
        bool     mWaitingForMessageUpdate : 1; // Indicates waiting for updating the indirect message.
        uint16_t mQueuedMessageCount : 14;     // Number of queued indirect messages for the neighbor.
        bool     mUseShortAddress : 1;         // Indicates whether to use short or extended address.
        bool     mSourceMatchPending : 1;      // Indicates whether or not pending to add to src match table.

        static_assert(OPENTHREAD_CONFIG_NUM_MESSAGE_BUFFERS < (1UL << 14),
                      "mQueuedMessageCount cannot fit max required!");
    };

    /**
     * Represents a predicate function for checking if a given `Message` meets specific criteria.
     *
     * @param[in] aMessage The message to evaluate.
     *
     * @retval TRUE   If the @p aMessage satisfies the predicate condition.
     * @retval FALSE  If the @p aMessage does not satisfy the predicate condition.
     */
    typedef bool (&MessageChecker)(const Message &aMessage);

    /**
     * Initializes the object.
     *
     * @param[in]  aInstance  A reference to the OpenThread instance.
     */
    explicit IndirectSender(Instance &aInstance);

    /**
     * Enables indirect transmissions.
     */
    void Start(void) { mEnabled = true; }

    /**
     * Disables indirect transmission.
     *
     * Any previously scheduled indirect transmission is canceled.
     */
    void Stop(void);

    /**
     * Adds a message for indirect transmission to a neighbor.
     *
     * @param[in] aMessage  The message to add.
     * @param[in] aNeighbor The (sleepy) neighbor for indirect transmission.
     */
    void AddIndirectMessageForNeighbor(Message &aMessage, IndirectNeighbor &aNeighbor);

    /**
     * Removes a message for indirect transmission to a neighbor.
     *
     * @param[in] aMessage  The message to update.
     * @param[in] aNeighbor The (sleepy) neighbor for indirect transmission.
     *
     * @retval kErrorNone          Successfully removed the message for indirect transmission.
     * @retval kErrorNotFound      The message was not scheduled for indirect transmission to the neighbor.
     */
    Error RemoveIndirectMessageFromNeighbor(Message &aMessage, IndirectNeighbor &aNeighbor);

    /**
     * Removes all added messages for a specific neighbor and frees message (with no indirect/direct tx).
     *
     * @param[in]  aNeighbor  A reference to a neighbor whose messages shall be removed.
     */
    void ClearAllIndirectMessagesForNeighbor(IndirectNeighbor &aNeighbor);

    /**
     * Finds the first queued message for a given neighbor that also satisfies the conditions of a given
     * `MessageChecker`.
     *
     * The caller MUST ensure that @p aNeighbor is sleepy.
     *
     * @param[in] aNeighbor  The neighbor to check.
     * @param[in] aChecker   The predicate function to apply.
     *
     * @returns A pointer to the matching queued message, or `nullptr` if none is found.
     */
    Message *FindQueuedIndirectMessageForNeighbor(const IndirectNeighbor &aNeighbor, MessageChecker aChecker)
    {
        return AsNonConst(AsConst(this)->FindQueuedIndirectMessageForNeighbor(aNeighbor, aChecker));
    }

    /**
     * Finds the first queued message for a given neighbor that also satisfies the conditions of a given
     * `MessageChecker`.
     *
     * The caller MUST ensure that @p aNeighbor is sleepy.
     *
     * @param[in] aNeighbor  The neighbor to check.
     * @param[in] aChecker   The predicate function to apply.
     *
     * @returns A pointer to the matching queued message, or `nullptr` if none is found.
     */
    const Message *FindQueuedIndirectMessageForNeighbor(const IndirectNeighbor &aNeighbor,
                                                        MessageChecker          aChecker) const;

    /**
     * Indicates whether there is any queued message for a given neighbor that also satisfies the conditions of a
     * given `MessageChecker`.
     *
     * The caller MUST ensure that @p aNeighbor is sleepy.
     *
     * @param[in] aNeighbor The neighbor to check for.
     * @param[in] aChecker  The predicate function to apply.
     *
     * @retval TRUE   There is a queued message satisfying @p aChecker for neighbor @p aNeighbor.
     * @retval FALSE  There is no queued message satisfying @p aChecker for neighbor @p aNeighbor.
     */
    bool HasQueuedIndirectMessageForNeighbor(const IndirectNeighbor &aNeighbor, MessageChecker aChecker) const
    {
        return (FindQueuedIndirectMessageForNeighbor(aNeighbor, aChecker) != nullptr);
    }

    /**
     * Sets whether to use the extended or short address for a neighbor.
     *
     * @param[in] aNeighbor         A reference to the neighbor.
     * @param[in] aUseShortAddress  `true` to use short address, `false` to use extended address.
     */
    void SetNeighborUseShortAddress(IndirectNeighbor &aNeighbor, bool aUseShortAddress);

    /**
     * Handles a child mode change and updates any queued messages for the child accordingly.
     *
     * @param[in]  aChild    The child whose device mode was changed.
     * @param[in]  aOldMode  The old device mode of the child.
     */
    void HandleChildModeChange(Child &aChild, Mle::DeviceMode aOldMode);

private:
    // Callbacks from DataPollHandler
    Error PrepareFrameForNeighbor(Mac::TxFrame &aFrame, FrameContext &aContext, IndirectNeighbor &aNeighbor);
    void  HandleSentFrameToNeighbor(const Mac::TxFrame &aFrame,
                                    const FrameContext &aContext,
                                    Error               aError,
                                    IndirectNeighbor   &aNeighbor);
    void  HandleFrameChangeDone(IndirectNeighbor &aNeighbor);

    void     UpdateIndirectMessage(IndirectNeighbor &aNeighbor);
    void     RequestMessageUpdate(IndirectNeighbor &aNeighbor);
    uint16_t PrepareDataFrame(Mac::TxFrame &aFrame, IndirectNeighbor &aNeighbor, Message &aMessage);
    void     PrepareEmptyFrame(Mac::TxFrame &aFrame, IndirectNeighbor &aNeighbor, bool aAckRequest);
    void     ClearMessagesForRemovedChildren(void);

    static bool AcceptAnyMessage(const Message &aMessage);
    static bool AcceptSupervisionMessage(const Message &aMessage);

    bool                  mEnabled;
    SourceMatchController mSourceMatchController;
    DataPollHandler       mDataPollHandler;
#if OPENTHREAD_FTD && OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
    CslTxScheduler mCslTxScheduler;
#endif
};

/**
 * @}
 */

} // namespace ot

#endif // OPENTHREAD_FTD

#endif // INDIRECT_SENDER_HPP_
