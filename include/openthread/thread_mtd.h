#ifndef OPENTHREAD_THREAD_MTD_H_
#define OPENTHREAD_THREAD_MTD_H_

#include <openthread/thread.h>

// This is identical to otChildInfo in thread_ftd.h but with some fields removed
// and mPrefixLength added.
typedef struct
{
    otExtAddress mExtAddress;
    uint32_t     mTimeout;
    uint32_t     mAge;
    uint16_t     mRloc16;
    uint8_t      mPrefixLength;
    uint8_t      mLinkQualityIn;
    int8_t       mAverageRssi;
    int8_t       mLastRssi;
    uint16_t     mFrameErrorRate;
    uint16_t     mMessageErrorRate;
    uint16_t     mQueuedMessageCnt;
    uint8_t      mVersion;
    bool         mRxOnWhenIdle : 1;
} otSubChildInfo;

otError otThreadSetSubChildMinWakeupLength(otInstance *aInstance, uint32_t aWakeupLength);

uint32_t otThreadGetSubChildMinWakeupLength(otInstance *aInstance);

otError otThreadSetSubChildMaxWakeupLength(otInstance *aInstance, uint32_t aWakeupLength);

uint32_t otThreadGetSubChildMaxWakeupLength(otInstance *aInstance);

bool otThreadIsDirectChild(otInstance *aInstance);

uint8_t otThreadGetRlocPrefixLength(otInstance *aInstance);

uint16_t otThreadGetMaxSubChildren(otInstance *aInstance);

otError otThreadGetSubChildInfoByIndex(otInstance *aInstance, uint16_t aChildIndex, otSubChildInfo *aChildInfo);

otError otThreadGetSubChildInfoByRloc16(otInstance *aInstance, uint16_t aRloc16, otSubChildInfo *aChildInfo);

#endif // OPENTHREAD_THREAD_MTD_H
