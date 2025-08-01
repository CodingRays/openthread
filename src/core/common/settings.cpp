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
 *   This file includes definitions for non-volatile storage of settings.
 */

#include "settings.hpp"

#include "instance/instance.hpp"

namespace ot {

RegisterLogModule("Settings");

//---------------------------------------------------------------------------------------------------------------------
// SettingsBase

// LCOV_EXCL_START

#if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_INFO)

void SettingsBase::NetworkInfo::Log(Action aAction) const
{
    LogInfo("%s NetworkInfo {rloc:0x%04x, extaddr:%s, role:%s, mode:0x%02x, version:%u, keyseq:0x%lx, ...",
            ActionToString(aAction), GetRloc16(), GetExtAddress().ToString().AsCString(),
            Mle::RoleToString(static_cast<Mle::DeviceRole>(GetRole())), GetDeviceMode(), GetVersion(),
            ToUlong(GetKeySequence()));

    LogInfo("... pid:0x%lx, mlecntr:0x%lx, maccntr:0x%lx, mliid:%s}", ToUlong(GetPreviousPartitionId()),
            ToUlong(GetMleFrameCounter()), ToUlong(GetMacFrameCounter()), GetMeshLocalIid().ToString().AsCString());
}

void SettingsBase::ParentInfo::Log(Action aAction) const
{
    LogInfo("%s ParentInfo {extaddr:%s, version:%u}", ActionToString(aAction), GetExtAddress().ToString().AsCString(),
            GetVersion());
}

#if OPENTHREAD_FTD
void SettingsBase::ChildInfo::Log(Action aAction) const
{
    LogInfo("%s ChildInfo {rloc:0x%04x, extaddr:%s, timeout:%lu, mode:0x%02x, version:%u}", ActionToString(aAction),
            GetRloc16(), GetExtAddress().ToString().AsCString(), ToUlong(GetTimeout()), GetMode(), GetVersion());
}
#endif

#if OPENTHREAD_CONFIG_DUA_ENABLE
void SettingsBase::DadInfo::Log(Action aAction) const
{
    LogInfo("%s DadInfo {DadCounter:%2d}", ActionToString(aAction), GetDadCounter());
}
#endif

#if OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
void SettingsBase::LogPrefix(Action aAction, Key aKey, const Ip6::Prefix &aPrefix)
{
    LogInfo("%s %s %s", ActionToString(aAction), KeyToString(aKey), aPrefix.ToString().AsCString());
}
#endif

#if OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE && OPENTHREAD_CONFIG_SRP_CLIENT_SAVE_SELECTED_SERVER_ENABLE
void SettingsBase::SrpClientInfo::Log(Action aAction) const
{
    LogInfo("%s SrpClientInfo {Server:[%s]:%u}", ActionToString(aAction), GetServerAddress().ToString().AsCString(),
            GetServerPort());
}
#endif

#if OPENTHREAD_CONFIG_SRP_SERVER_ENABLE && OPENTHREAD_CONFIG_SRP_SERVER_PORT_SWITCH_ENABLE
void SettingsBase::SrpServerInfo::Log(Action aAction) const
{
    LogInfo("%s SrpServerInfo {port:%u}", ActionToString(aAction), GetPort());
}
#endif

#if OPENTHREAD_CONFIG_BORDER_AGENT_ENABLE && OPENTHREAD_CONFIG_BORDER_AGENT_ID_ENABLE
void SettingsBase::BorderAgentId::Log(Action aAction, const MeshCoP::BorderAgent::Id &aId)
{
    static constexpr uint8_t kStringSize = sizeof(MeshCoP::BorderAgent::Id) * 2 + 1;

    String<kStringSize> string;

    string.AppendHexBytes(aId.mId, sizeof(aId));
    LogInfo("%s BorderAgentId {id:%s}", ActionToString(aAction), string.AsCString());
}
#endif

#endif // OT_SHOULD_LOG_AT(OT_LOG_LEVEL_INFO)

#if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_INFO)
const char *SettingsBase::ActionToString(Action aAction)
{
    static const char *const kActionStrings[] = {
        "Read",     // (0) kActionRead
        "Saved",    // (1) kActionSave
        "Re-saved", // (2) kActionResave
        "Deleted",  // (3) kActionDelete
#if OPENTHREAD_FTD
        "Added",      // (4) kActionAdd,
        "Removed",    // (5) kActionRemove,
        "Deleted all" // (6) kActionDeleteAll
#endif
    };

    struct EnumCheck
    {
        InitEnumValidatorCounter();
        ValidateNextEnum(kActionRead);
        ValidateNextEnum(kActionSave);
        ValidateNextEnum(kActionResave);
        ValidateNextEnum(kActionDelete);
#if OPENTHREAD_FTD
        ValidateNextEnum(kActionAdd);
        ValidateNextEnum(kActionRemove);
        ValidateNextEnum(kActionDeleteAll);
#endif
    };

    return kActionStrings[aAction];
}
#endif // OT_SHOULD_LOG_AT(OT_LOG_LEVEL_INFO)

#if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_WARN)
const char *SettingsBase::KeyToString(Key aKey)
{
    static const char *const kKeyStrings[] = {
        "",                  // (0)  (Unused)
        "ActiveDataset",     // (1)  kKeyActiveDataset
        "PendingDataset",    // (2)  kKeyPendingDataset
        "NetworkInfo",       // (3)  kKeyNetworkInfo
        "ParentInfo",        // (4)  kKeyParentInfo
        "ChildInfo",         // (5)  kKeyChildInfo
        "",                  // (6)  Removed (previously auto-start).
        "SlaacIidSecretKey", // (7)  kKeySlaacIidSecretKey
        "DadInfo",           // (8)  kKeyDadInfo
        "",                  // (9)  Removed (previously OMR prefix).
        "",                  // (10) Removed (previously on-link prefix).
        "SrpEcdsaKey",       // (11) kKeySrpEcdsaKey
        "SrpClientInfo",     // (12) kKeySrpClientInfo
        "SrpServerInfo",     // (13) kKeySrpServerInfo
        "",                  // (14) Removed (previously NAT64 prefix)
        "BrUlaPrefix",       // (15) kKeyBrUlaPrefix
        "BrOnLinkPrefixes",  // (16) kKeyBrOnLinkPrefixes
        "BorderAgentId",     // (17) kKeyBorderAgentId
        "TcatCommrCert"      // (18) kKeyTcatCommrCert
    };

    struct EnumCheck
    {
        InitEnumValidatorCounter();
        SkipNextEnum();
        ValidateNextEnum(kKeyActiveDataset);
        ValidateNextEnum(kKeyPendingDataset);
        ValidateNextEnum(kKeyNetworkInfo);
        ValidateNextEnum(kKeyParentInfo);
        ValidateNextEnum(kKeyChildInfo);
        SkipNextEnum();
        ValidateNextEnum(kKeySlaacIidSecretKey);
        ValidateNextEnum(kKeyDadInfo);
        SkipNextEnum();
        SkipNextEnum();
        ValidateNextEnum(kKeySrpEcdsaKey);
        ValidateNextEnum(kKeySrpClientInfo);
        ValidateNextEnum(kKeySrpServerInfo);
        SkipNextEnum();
        ValidateNextEnum(kKeyBrUlaPrefix);
        ValidateNextEnum(kKeyBrOnLinkPrefixes);
        ValidateNextEnum(kKeyBorderAgentId);
        ValidateNextEnum(kKeyTcatCommrCert);
    };

    static_assert(kLastKey == kKeyTcatCommrCert, "kLastKey is not valid");

    OT_ASSERT(aKey <= kLastKey);

    return kKeyStrings[aKey];
}
#endif // OT_SHOULD_LOG_AT(OT_LOG_LEVEL_WARN)

// LCOV_EXCL_STOP

//---------------------------------------------------------------------------------------------------------------------
// Settings

// This array contains sensitive keys that should be stored in the secure area.
const uint16_t Settings::kSensitiveKeys[] = {
    SettingsBase::kKeyActiveDataset,
    SettingsBase::kKeyPendingDataset,
    SettingsBase::kKeySrpEcdsaKey,
};

void Settings::Init(void) { Get<SettingsDriver>().Init(kSensitiveKeys, GetArrayLength(kSensitiveKeys)); }

void Settings::Deinit(void) { Get<SettingsDriver>().Deinit(); }

void Settings::Wipe(void)
{
    Get<SettingsDriver>().Wipe();
    LogInfo("Wiped all info");
}

Settings::Key Settings::KeyForDatasetType(MeshCoP::Dataset::Type aType)
{
    return (aType == MeshCoP::Dataset::kActive) ? kKeyActiveDataset : kKeyPendingDataset;
}

void Settings::SaveOperationalDataset(MeshCoP::Dataset::Type aType, const MeshCoP::Dataset &aDataset)
{
    Key   key   = KeyForDatasetType(aType);
    Error error = Get<SettingsDriver>().Set(key, aDataset.GetBytes(), aDataset.GetLength());

    Log(kActionSave, error, key);

    SuccessOrAssert(error);
}

Error Settings::ReadOperationalDataset(MeshCoP::Dataset::Type aType, MeshCoP::Dataset &aDataset) const
{
    Error    error  = kErrorNone;
    uint16_t length = MeshCoP::Dataset::kMaxLength;

    SuccessOrExit(error = Get<SettingsDriver>().Get(KeyForDatasetType(aType), aDataset.GetBytes(), &length));
    VerifyOrExit(length <= MeshCoP::Dataset::kMaxLength, error = kErrorNotFound);

    aDataset.SetLength(static_cast<uint8_t>(length));

exit:
    return error;
}

void Settings::DeleteOperationalDataset(MeshCoP::Dataset::Type aType)
{
    Key   key   = KeyForDatasetType(aType);
    Error error = Get<SettingsDriver>().Delete(key);

    Log(kActionDelete, error, key);
}

#if OPENTHREAD_CONFIG_BLE_TCAT_ENABLE
void Settings::SaveTcatCommissionerCertificate(uint8_t *aCert, uint16_t aCertLen)
{
    Error error = Get<SettingsDriver>().Set(kKeyTcatCommrCert, aCert, aCertLen);

    Log(kActionSave, error, kKeyTcatCommrCert);

    SuccessOrAssert(error);
}
#endif

#if OPENTHREAD_FTD
Error Settings::AddChildInfo(const ChildInfo &aChildInfo)
{
    Error error = Get<SettingsDriver>().Add(kKeyChildInfo, &aChildInfo, sizeof(aChildInfo));

    Log(kActionAdd, error, kKeyChildInfo, &aChildInfo);

    return error;
}

void Settings::DeleteAllChildInfo(void)
{
    Error error = Get<SettingsDriver>().Delete(kKeyChildInfo);

    Log(kActionDeleteAll, error, kKeyChildInfo);
}

Settings::ChildInfoIterator::ChildInfoIterator(Instance &aInstance)
    : SettingsBase(aInstance)
    , mIndex(0)
    , mIsDone(false)
{
    Read();
}

void Settings::ChildInfoIterator::Advance(void)
{
    if (!mIsDone)
    {
        mIndex++;
        Read();
    }
}

Error Settings::ChildInfoIterator::Delete(void)
{
    Error error = kErrorNone;

    VerifyOrExit(!mIsDone, error = kErrorInvalidState);
    SuccessOrExit(error = Get<SettingsDriver>().Delete(kKeyChildInfo, mIndex));

exit:
    Log(kActionRemove, error, kKeyChildInfo, &mChildInfo);
    return error;
}

void Settings::ChildInfoIterator::Read(void)
{
    uint16_t length = sizeof(ChildInfo);
    Error    error;

    mChildInfo.Init();
    SuccessOrExit(
        error = Get<SettingsDriver>().Get(kKeyChildInfo, mIndex, reinterpret_cast<uint8_t *>(&mChildInfo), &length));

exit:
    Log(kActionRead, error, kKeyChildInfo, &mChildInfo);
    mIsDone = (error != kErrorNone);
}
#endif // OPENTHREAD_FTD

#if OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
Error Settings::AddOrUpdateBrOnLinkPrefix(const BrOnLinkPrefix &aBrOnLinkPrefix)
{
    Error          error = kErrorNone;
    int            index = 0;
    BrOnLinkPrefix brPrefix;
    bool           didUpdate = false;

    while (ReadBrOnLinkPrefix(index, brPrefix) == kErrorNone)
    {
        if (brPrefix.GetPrefix() == aBrOnLinkPrefix.GetPrefix())
        {
            if (brPrefix.GetLifetime() == aBrOnLinkPrefix.GetLifetime())
            {
                // Existing entry fully matches `aBrOnLinkPrefix`.
                // No need to make any changes.
                ExitNow();
            }

            SuccessOrExit(error = Get<SettingsDriver>().Delete(kKeyBrOnLinkPrefixes, index));
            didUpdate = true;
            break;
        }

        index++;
    }

    SuccessOrExit(error = Get<SettingsDriver>().Add(kKeyBrOnLinkPrefixes, &aBrOnLinkPrefix, sizeof(BrOnLinkPrefix)));
    brPrefix.Log(didUpdate ? "Updated" : "Added");

exit:
    return error;
}

void Settings::RemoveBrOnLinkPrefix(const Ip6::Prefix &aPrefix)
{
    BrOnLinkPrefix brPrefix;

    for (int index = 0; ReadBrOnLinkPrefix(index, brPrefix) == kErrorNone; index++)
    {
        if (brPrefix.GetPrefix() == aPrefix)
        {
            IgnoreError(Get<SettingsDriver>().Delete(kKeyBrOnLinkPrefixes, index));
            brPrefix.Log("Removed");
            break;
        }
    }
}

void Settings::DeleteAllBrOnLinkPrefixes(void) { IgnoreError(Get<SettingsDriver>().Delete(kKeyBrOnLinkPrefixes)); }

Error Settings::ReadBrOnLinkPrefix(int aIndex, BrOnLinkPrefix &aBrOnLinkPrefix)
{
    uint16_t length = sizeof(BrOnLinkPrefix);

    aBrOnLinkPrefix.Init();

    return Get<SettingsDriver>().Get(kKeyBrOnLinkPrefixes, aIndex, &aBrOnLinkPrefix, &length);
}

void Settings::BrOnLinkPrefix::Log(const char *aActionText) const
{
    OT_UNUSED_VARIABLE(aActionText);

    LogInfo("%s %s entry {prefix:%s,lifetime:%lu}", aActionText, KeyToString(kKeyBrOnLinkPrefixes),
            GetPrefix().ToString().AsCString(), ToUlong(GetLifetime()));
}

#endif // OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE

Error Settings::ReadEntry(Key aKey, void *aValue, uint16_t aMaxLength) const
{
    Error    error;
    uint16_t length = aMaxLength;

    error = Get<SettingsDriver>().Get(aKey, aValue, &length);
    Log(kActionRead, error, aKey, aValue);

    return error;
}

void Settings::SaveEntry(Key aKey, const void *aValue, void *aPrev, uint16_t aLength)
{
    Error    error      = kErrorNone;
    uint16_t readLength = aLength;
    Action   action     = kActionSave;

    if ((Get<SettingsDriver>().Get(aKey, aPrev, &readLength) == kErrorNone) && (readLength == aLength) &&
        (memcmp(aValue, aPrev, aLength) == 0))
    {
        action = kActionResave;
    }
    else
    {
        error = Get<SettingsDriver>().Set(aKey, aValue, aLength);
    }

    Log(action, error, aKey, aValue);

    SuccessOrAssert(error);
}

void Settings::DeleteEntry(Key aKey)
{
    Error error = Get<SettingsDriver>().Delete(aKey);

    Log(kActionDelete, error, aKey);
}

void Settings::Log(Action aAction, Error aError, Key aKey, const void *aValue)
{
    OT_UNUSED_VARIABLE(aAction);
    OT_UNUSED_VARIABLE(aKey);
    OT_UNUSED_VARIABLE(aError);
    OT_UNUSED_VARIABLE(aValue);

    if (aError != kErrorNone)
    {
        // Log error if log level is at "warn" or higher.

#if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_WARN)
        const char *actionText = "";

        switch (aAction)
        {
        case kActionSave:
        case kActionResave:
            actionText = "saving";
            break;

        case kActionDelete:
            VerifyOrExit(aError != kErrorNotFound);
            actionText = "deleting";
            break;

#if OPENTHREAD_FTD
        case kActionAdd:
            actionText = "adding";
            break;

        case kActionRemove:
            VerifyOrExit(aError != kErrorNotFound);
            actionText = "removing";
            break;

        case kActionDeleteAll:
            VerifyOrExit(aError != kErrorNotFound);
            actionText = "deleting all";
            break;
#endif
        case kActionRead:
            ExitNow();
        }

        LogWarn("Error %s %s %s", ErrorToString(aError), actionText, KeyToString(aKey));

#endif // #if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_WARN)

        ExitNow();
    }

    // We reach here when `aError` is `kErrorNone`.
    // Log success if log level is at "info" or higher.

#if OT_SHOULD_LOG_AT(OT_LOG_LEVEL_INFO)
    if (aValue != nullptr)
    {
        switch (aKey)
        {
        case kKeyNetworkInfo:
            reinterpret_cast<const NetworkInfo *>(aValue)->Log(aAction);
            break;

        case kKeyParentInfo:
            reinterpret_cast<const ParentInfo *>(aValue)->Log(aAction);
            break;

#if OPENTHREAD_FTD
        case kKeyChildInfo:
            reinterpret_cast<const ChildInfo *>(aValue)->Log(aAction);
            break;
#endif

#if OPENTHREAD_CONFIG_DUA_ENABLE
        case kKeyDadInfo:
            reinterpret_cast<const DadInfo *>(aValue)->Log(aAction);
            break;
#endif

#if OPENTHREAD_CONFIG_BORDER_ROUTING_ENABLE
        case kKeyBrUlaPrefix:
            LogPrefix(aAction, aKey, *reinterpret_cast<const Ip6::Prefix *>(aValue));
            break;
#endif

#if OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE && OPENTHREAD_CONFIG_SRP_CLIENT_SAVE_SELECTED_SERVER_ENABLE
        case kKeySrpClientInfo:
            reinterpret_cast<const SrpClientInfo *>(aValue)->Log(aAction);
            break;
#endif

#if OPENTHREAD_CONFIG_SRP_SERVER_ENABLE && OPENTHREAD_CONFIG_SRP_SERVER_PORT_SWITCH_ENABLE
        case kKeySrpServerInfo:
            reinterpret_cast<const SrpServerInfo *>(aValue)->Log(aAction);
            break;
#endif

#if OPENTHREAD_CONFIG_BORDER_AGENT_ENABLE && OPENTHREAD_CONFIG_BORDER_AGENT_ID_ENABLE
        case kKeyBorderAgentId:
            BorderAgentId::Log(aAction, *reinterpret_cast<const MeshCoP::BorderAgent::Id *>(aValue));
            break;
#endif

        default:
            // For any other keys, we do not want to include the value
            // in the log, so even if it is given we set `aValue` to
            // `nullptr`. This ensures that we just log the action and
            // the key.
            aValue = nullptr;
            break;
        }
    }

    if (aValue == nullptr)
    {
        LogInfo("%s %s", ActionToString(aAction), KeyToString(aKey));
    }
#endif // OT_SHOULD_LOG_AT(OT_LOG_LEVEL_INFO)

exit:
    return;
}

} // namespace ot

//---------------------------------------------------------------------------------------------------------------------
// Default/weak implementation of settings platform APIs

OT_TOOL_WEAK void otPlatSettingsSetCriticalKeys(otInstance *aInstance, const uint16_t *aKeys, uint16_t aKeysLength)
{
    OT_UNUSED_VARIABLE(aInstance);
    OT_UNUSED_VARIABLE(aKeys);
    OT_UNUSED_VARIABLE(aKeysLength);
}
