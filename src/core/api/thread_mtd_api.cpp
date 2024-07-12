#include "openthread-core-config.h"

#if OPENTHREAD_MTD

#include <openthread/thread_mtd.h>

#include "common/as_core_type.hpp"
#include "common/locator_getters.hpp"
#include "mac/mac.hpp"
#include "radio/radio.hpp"

using namespace ot;

#if OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE
otError otThreadSetSubChildMinWakeupLength(otInstance *aInstance, uint32_t aWakeupLength)
{
    Error    error = kErrorNone;
    uint16_t lengthInTenSymbols;

    VerifyOrExit((aWakeupLength % kUsPerTenSymbols) == 0, error = kErrorInvalidArgs);
    lengthInTenSymbols = ClampToUint16(aWakeupLength / kUsPerTenSymbols);
    VerifyOrExit(lengthInTenSymbols > 31, error = kErrorInvalidArgs);

    AsCoreType(aInstance).Get<Mle::Mle>().SetSubChildMinWakeupLength(lengthInTenSymbols);

exit:
    return error;
}

uint32_t otThreadGetSubChildMinWakeupLength(otInstance *aInstance)
{
    return Mac::Mac::CslPeriodToUsec(AsCoreType(aInstance).Get<Mle::Mle>().GetSubChildMinWakeupLength());
}

otError otThreadSetSubChildMaxWakeupLength(otInstance *aInstance, uint32_t aWakeupLength)
{
    Error    error = kErrorNone;
    uint16_t lengthInTenSymbols;

    VerifyOrExit((aWakeupLength % kUsPerTenSymbols) == 0, error = kErrorInvalidArgs);
    lengthInTenSymbols = ClampToUint16(aWakeupLength / kUsPerTenSymbols);
    VerifyOrExit(lengthInTenSymbols > 31, error = kErrorInvalidArgs);

    AsCoreType(aInstance).Get<Mle::Mle>().SetSubChildMaxWakeupLength(lengthInTenSymbols);

exit:
    return error;
}

uint32_t otThreadGetSubChildMaxWakeupLength(otInstance *aInstance)
{
    return Mac::Mac::CslPeriodToUsec(AsCoreType(aInstance).Get<Mle::Mle>().GetSubChildMaxWakeupLength());
}

bool otThreadIsDirectChild(otInstance *aInstance)
{
    return !AsCoreType(aInstance).Get<Mle::MleSubChild>().IsNonDirectChild();
}

uint8_t otThreadGetRlocPrefixLength(otInstance *aInstance)
{
    return AsCoreType(aInstance).Get<Mle::MleSubChild>().GetRlocPrefixLength();
}

uint16_t otThreadGetMaxSubChildren(otInstance *aInstance)
{
    return AsCoreType(aInstance).Get<ChildTable>().GetMaxChildren();
}

otError otThreadGetSubChildInfoByIndex(otInstance *aInstance, uint16_t aChildIndex, otSubChildInfo *aChildInfo)
{
    Error  error = kErrorNone;
    Child *child = AsCoreType(aInstance).Get<ChildTable>().GetChildAtIndex(aChildIndex);

    VerifyOrExit(child != nullptr, error = kErrorNotFound);
    VerifyOrExit(child->IsStateValid(), error = kErrorInvalidState);

    static_cast<Child::SubChildInfo*>(aChildInfo)->SetFrom(*child);

exit:
    return error;
}


otError otThreadGetSubChildInfoByRloc16(otInstance *aInstance, uint16_t aRloc16, otSubChildInfo *aChildInfo)
{
    Error error = kErrorNone;
    Child *child = AsCoreType(aInstance).Get<ChildTable>().FindChild(aRloc16, Child::kInStateValid);

    VerifyOrExit(child != nullptr, error = kErrorNotFound);

    static_cast<Child::SubChildInfo*>(aChildInfo)->SetFrom(*child);

exit:
    return error;
}
#endif // OPENTHREAD_CONFIG_CHILD_NETWORK_ENABLE

#endif // OPENTHREAD_MTD
