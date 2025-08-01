/*
 *  Copyright (c) 2023, The OpenThread Authors.
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

#include "ble_secure.hpp"
#include "common/error.hpp"

#if OPENTHREAD_CONFIG_BLE_TCAT_ENABLE

#include <openthread/platform/ble.h>

#include "instance/instance.hpp"

using namespace ot;

/**
 * @file
 *   This file implements the secure Ble agent.
 */

namespace ot {
namespace Ble {

RegisterLogModule("BleSecure");

BleSecure::BleSecure(Instance &aInstance)
    : InstanceLocator(aInstance)
    , MeshCoP::Tls::Extension(mTls)
    , mTls(aInstance, kNoLinkSecurity, *this)
    , mTcatAgent(aInstance)
    , mTlvMode(false)
    , mReceivedMessage(nullptr)
    , mSendMessage(nullptr)
    , mTransmitTask(aInstance)
    , mBleState(kStopped)
    , mMtuSize(kInitialMtuSize)
{
}

Error BleSecure::Start(ConnectCallback aConnectHandler, ReceiveCallback aReceiveHandler, bool aTlvMode, void *aContext)
{
    Error    error             = kErrorNone;
    uint16_t advertisementLen  = 0;
    uint8_t *advertisementData = nullptr;

    VerifyOrExit(mBleState == kStopped, error = kErrorAlready);

    mConnectCallback.Set(aConnectHandler, aContext);
    mReceiveCallback.Set(aReceiveHandler, aContext);
    mTlvMode = aTlvMode;
    mMtuSize = kInitialMtuSize;

    SuccessOrExit(error = otPlatBleEnable(&GetInstance()));

    SuccessOrExit(error = otPlatBleGetAdvertisementBuffer(&GetInstance(), &advertisementData));
    SuccessOrExit(error = mTcatAgent.GetAdvertisementData(advertisementLen, advertisementData));
    VerifyOrExit(advertisementData != nullptr, error = kErrorFailed);
    SuccessOrExit(error = otPlatBleGapAdvSetData(&GetInstance(), advertisementData, advertisementLen));
    SuccessOrExit(error = otPlatBleGapAdvStart(&GetInstance(), OT_BLE_ADV_INTERVAL_DEFAULT));

    SuccessOrExit(error = mTls.Open());
    mTls.SetReceiveCallback(HandleTlsReceive, this);
    mTls.SetConnectCallback(HandleTlsConnectEvent, this);
    SuccessOrExit(error = mTls.Bind(HandleTransport, this));

exit:
    if (error == kErrorNone)
    {
        mBleState = kAdvertising;
    }
    return error;
}

Error BleSecure::TcatStart(MeshCoP::TcatAgent::JoinCallback aJoinHandler)
{
    Error error;

    VerifyOrExit(mBleState != kStopped, error = kErrorInvalidState);

    error = mTcatAgent.Start(mReceiveCallback.GetHandler(), aJoinHandler, mReceiveCallback.GetContext());

exit:
    return error;
}

void BleSecure::Stop(void)
{
    VerifyOrExit(mBleState != kStopped);
    SuccessOrExit(otPlatBleGapAdvStop(&GetInstance()));
    SuccessOrExit(otPlatBleDisable(&GetInstance()));
    mBleState = kStopped;
    mMtuSize  = kInitialMtuSize;

    if (mTcatAgent.IsEnabled())
    {
        mTcatAgent.Stop();
    }

    mTls.Close();

    mTransmitQueue.DequeueAndFreeAll();

    mConnectCallback.Clear();
    mReceiveCallback.Clear();

    FreeMessage(mReceivedMessage);
    mReceivedMessage = nullptr;
    FreeMessage(mSendMessage);
    mSendMessage = nullptr;

exit:
    return;
}

Error BleSecure::Connect(void)
{
    Ip6::SockAddr sockaddr;
    Error         error;

    VerifyOrExit(mBleState == kConnected, error = kErrorInvalidState);

    error = mTls.Connect(sockaddr);

exit:
    return error;
}

void BleSecure::Disconnect(void)
{
    if (mTls.IsConnected())
    {
        mTls.Disconnect();
    }

    if (mBleState == kConnected)
    {
        mBleState = kAdvertising;
        IgnoreError(otPlatBleGapDisconnect(&GetInstance()));
    }

    // Update advertisement
    IgnoreError(NotifyAdvertisementChanged());

    mConnectCallback.InvokeIfSet(&GetInstance(), false, false);
}

Error BleSecure::NotifyAdvertisementChanged(void)
{
    Error    error             = kErrorNone;
    uint16_t advertisementLen  = 0;
    uint8_t *advertisementData = nullptr;

    VerifyOrExit(mBleState == kAdvertising);
    SuccessOrExit(error = otPlatBleGetAdvertisementBuffer(&GetInstance(), &advertisementData));
    SuccessOrExit(error = mTcatAgent.GetAdvertisementData(advertisementLen, advertisementData));
    SuccessOrExit(error = otPlatBleGapAdvUpdateData(&GetInstance(), advertisementData, advertisementLen));

exit:
    return error;
}

void BleSecure::SetPsk(const MeshCoP::JoinerPskd &aPskd)
{
    static_assert(static_cast<uint16_t>(MeshCoP::JoinerPskd::kMaxLength) <=
                      static_cast<uint16_t>(MeshCoP::Tls::kPskMaxLength),
                  "The maximum length of TLS PSK is smaller than joiner PSKd");

    SuccessOrAssert(mTls.SetPsk(reinterpret_cast<const uint8_t *>(aPskd.GetAsCString()), aPskd.GetLength()));
}

Error BleSecure::SendMessage(ot::Message &aMessage)
{
    Error error = kErrorNone;

    VerifyOrExit(IsConnected(), error = kErrorInvalidState);
    if (mSendMessage == nullptr)
    {
        mSendMessage = Get<MessagePool>().Allocate(Message::kTypeBle);
        VerifyOrExit(mSendMessage != nullptr, error = kErrorNoBufs);
    }
    SuccessOrExit(error = mSendMessage->AppendBytesFromMessage(aMessage, 0, aMessage.GetLength()));
    SuccessOrExit(error = Flush());

    aMessage.Free();
exit:
    return error;
}

Error BleSecure::Send(uint8_t *aBuf, uint16_t aLength)
{
    Error error = kErrorNone;

    VerifyOrExit(IsConnected(), error = kErrorInvalidState);
    if (mSendMessage == nullptr)
    {
        mSendMessage = Get<MessagePool>().Allocate(Message::kTypeBle);
        VerifyOrExit(mSendMessage != nullptr, error = kErrorNoBufs);
    }
    SuccessOrExit(error = mSendMessage->AppendBytes(aBuf, aLength));

exit:
    return error;
}

Error BleSecure::SendApplicationTlv(MeshCoP::TcatAgent::TcatApplicationProtocol aTcatApplicationProtocol,
                                    uint8_t                                    *aBuf,
                                    uint16_t                                    aLength)
{
    Error error = kErrorNone;

    VerifyOrExit((aTcatApplicationProtocol != MeshCoP::TcatAgent::kApplicationProtocolStatus &&
                  aTcatApplicationProtocol != MeshCoP::TcatAgent::kApplicationProtocolResponse) ||
                     mTcatAgent.GetApplicationResponsePending(),
                 error = kErrorRejected);

    if (aLength > Tlv::kBaseTlvMaxLength)
    {
        ot::ExtendedTlv tlv;

        tlv.SetType(static_cast<uint8_t>(aTcatApplicationProtocol));
        tlv.SetLength(aLength);
        SuccessOrExit(error = Send(reinterpret_cast<uint8_t *>(&tlv), sizeof(tlv)));
    }
    else
    {
        ot::Tlv tlv;

        tlv.SetType(static_cast<uint8_t>(aTcatApplicationProtocol));
        tlv.SetLength((uint8_t)aLength);
        SuccessOrExit(error = Send(reinterpret_cast<uint8_t *>(&tlv), sizeof(tlv)));
    }

    SuccessOrExit(error = Send(aBuf, aLength));

    if (aTcatApplicationProtocol == MeshCoP::TcatAgent::kApplicationProtocolStatus ||
        aTcatApplicationProtocol == MeshCoP::TcatAgent::kApplicationProtocolResponse)
    {
        mTcatAgent.NotifyApplicationResponseSent();
    }

exit:
    return error;
}

Error BleSecure::Flush(void)
{
    Error        error   = kErrorNone;
    ot::Message *message = nullptr;
    uint16_t     length;

    VerifyOrExit(mSendMessage != nullptr);
    VerifyOrExit(IsConnected(), error = kErrorInvalidState);
    length = mSendMessage->GetLength();

    // Split send buffer in chunks which can later be processed by mTls.Send(..)
    while (length > kTlsDataMaxSize)
    {
        VerifyOrExit((message = Get<MessagePool>().Allocate(Message::kTypeBle, 0)) != nullptr, error = kErrorNoBufs);
        SuccessOrExit(error = message->AppendBytesFromMessage(*mSendMessage, 0, kTlsDataMaxSize));

        // We accept an expensive copy operation in favor of optimal buffer usage for long messages
        mSendMessage->WriteBytesFromMessage(0, *mSendMessage, kTlsDataMaxSize, length - kTlsDataMaxSize);
        length -= kTlsDataMaxSize;

        // Should never fail since we are decreasing the length of the message
        SuccessOrAssert(error = mSendMessage->SetLength(length));
        mTransmitQueue.Enqueue(*message);
        mTransmitTask.Post();
        message = nullptr;
    }

    VerifyOrExit(length != 0, error = kErrorNone);
    mTransmitQueue.Enqueue(*mSendMessage);
    mTransmitTask.Post();
    mSendMessage = nullptr;

exit:
    FreeMessage(message);

    if (mSendMessage != nullptr)
    {
        mSendMessage->Free();
        mSendMessage = nullptr;
    }

    return error;
}

Error BleSecure::HandleBleReceive(uint8_t *aBuf, uint16_t aLength)
{
    ot::Message     *message = nullptr;
    Ip6::MessageInfo messageInfo;
    Error            error = kErrorNone;

    if ((message = Get<MessagePool>().Allocate(Message::kTypeBle, 0)) == nullptr)
    {
        error = kErrorNoBufs;
        ExitNow();
    }
    SuccessOrExit(error = message->AppendBytes(aBuf, aLength));

    // Cannot call Receive(..) directly because Setup(..) and mState are private
    mTls.HandleReceive(*message, messageInfo);

exit:
    FreeMessage(message);
    return error;
}

void BleSecure::HandleBleConnected(uint16_t aConnectionId)
{
    OT_UNUSED_VARIABLE(aConnectionId);

    mBleState = kConnected;

    IgnoreError(otPlatBleGattMtuGet(&GetInstance(), &mMtuSize));

    mConnectCallback.InvokeIfSet(&GetInstance(), IsConnected(), true);
}

void BleSecure::HandleBleDisconnected(uint16_t aConnectionId)
{
    OT_UNUSED_VARIABLE(aConnectionId);

    mBleState = kAdvertising;
    mMtuSize  = kInitialMtuSize;

    Disconnect(); // Stop TLS connection
}

Error BleSecure::HandleBleMtuUpdate(uint16_t aMtu)
{
    Error error = kErrorNone;

    if (aMtu <= OT_BLE_ATT_MTU_MAX)
    {
        mMtuSize = aMtu;
    }
    else
    {
        mMtuSize = OT_BLE_ATT_MTU_MAX;
        error    = kErrorInvalidArgs;
    }

    return error;
}

void BleSecure::HandleTlsConnectEvent(MeshCoP::Tls::ConnectEvent aEvent, void *aContext)
{
    return static_cast<BleSecure *>(aContext)->HandleTlsConnectEvent(aEvent);
}

void BleSecure::HandleTlsConnectEvent(MeshCoP::Tls::ConnectEvent aEvent)
{
    if (aEvent == MeshCoP::Tls::kConnected)
    {
        Error err;

        if (mReceivedMessage == nullptr)
        {
            mReceivedMessage = Get<MessagePool>().Allocate(Message::kTypeBle);
        }
        err = mTcatAgent.Connected(*this);

        if (err != kErrorNone)
        {
            mTls.Disconnect(); // must not use Close(), so that next Commissioner can connect
            LogWarn("Rejected TCAT Commissioner, error: %s", ErrorToString(err));
            ExitNow();
        }
    }
    else
    {
        FreeMessage(mReceivedMessage);
        mReceivedMessage = nullptr;
        FreeMessage(mSendMessage);
        mSendMessage = nullptr;

        if (mTcatAgent.IsEnabled())
        {
            mTcatAgent.Disconnected();
        }
    }

    mConnectCallback.InvokeIfSet(&GetInstance(), aEvent == MeshCoP::Tls::kConnected, true);

exit:
    return;
}

void BleSecure::HandleTlsReceive(void *aContext, uint8_t *aBuf, uint16_t aLength)
{
    return static_cast<BleSecure *>(aContext)->HandleTlsReceive(aBuf, aLength);
}

void BleSecure::HandleTlsReceive(uint8_t *aBuf, uint16_t aLength)
{
    VerifyOrExit(mReceivedMessage != nullptr);

    if (!mTlvMode)
    {
        SuccessOrExit(mReceivedMessage->AppendBytes(aBuf, aLength));
        mReceiveCallback.InvokeIfSet(&GetInstance(), mReceivedMessage, 0, OT_TCAT_APPLICATION_PROTOCOL_NONE);
        IgnoreError(mReceivedMessage->SetLength(0));
    }
    else
    {
        ot::Tlv  tlv;
        uint32_t requiredBytes = sizeof(Tlv);
        uint32_t offset;

        while (aLength > 0)
        {
            if (mReceivedMessage->GetLength() < requiredBytes)
            {
                uint32_t missingBytes = requiredBytes - mReceivedMessage->GetLength();

                if (missingBytes > aLength)
                {
                    SuccessOrExit(mReceivedMessage->AppendBytes(aBuf, aLength));
                    break;
                }
                else
                {
                    SuccessOrExit(mReceivedMessage->AppendBytes(aBuf, (uint16_t)missingBytes));
                    aLength -= missingBytes;
                    aBuf += missingBytes;
                }
            }

            IgnoreError(mReceivedMessage->Read(0, tlv));

            if (tlv.IsExtended())
            {
                ot::ExtendedTlv extTlv;
                requiredBytes = sizeof(extTlv);

                if (mReceivedMessage->GetLength() < requiredBytes)
                {
                    continue;
                }

                IgnoreError(mReceivedMessage->Read(0, extTlv));
                requiredBytes = extTlv.GetSize();
                offset        = sizeof(extTlv);
            }
            else
            {
                requiredBytes = tlv.GetSize();
                offset        = sizeof(tlv);
            }

            if (mReceivedMessage->GetLength() < requiredBytes)
            {
                continue;
            }

            // TLV fully loaded

            if (mTcatAgent.IsEnabled())
            {
                Error error = kErrorNone;

                IgnoreError(Flush());

                if (mSendMessage == nullptr)
                {
                    mSendMessage = Get<MessagePool>().Allocate(Message::kTypeBle);
                    VerifyOrExit(mSendMessage != nullptr, error = kErrorNoBufs);
                }

                error = mTcatAgent.HandleSingleTlv(*mReceivedMessage, *mSendMessage);
                IgnoreError(Flush());

                if (error == kErrorAbort)
                {
                    LogInfo("Disconnecting TCAT client.");
                    // kErrorAbort indicates that a Disconnect command TLV has been received.
                    Disconnect();
                    // BleSecure is not stopped here, it must remain active in advertising state and
                    // must be ready to receive a next TCAT commissioner.
                    ExitNow();
                }
            }
            else
            {
                mReceivedMessage->SetOffset((uint16_t)offset);
                mReceiveCallback.InvokeIfSet(&GetInstance(), mReceivedMessage, (int32_t)offset,
                                             OT_TCAT_APPLICATION_PROTOCOL_NONE);
            }

            SuccessOrExit(mReceivedMessage->SetLength(0)); // also sets the offset to 0
            requiredBytes = sizeof(Tlv);
        }
    }

exit:
    return;
}

void BleSecure::HandleTransmit(void)
{
    Error        error   = kErrorNone;
    ot::Message *message = mTransmitQueue.GetHead();

    VerifyOrExit(message != nullptr);
    mTransmitQueue.Dequeue(*message);

    if (mTransmitQueue.GetHead() != nullptr)
    {
        mTransmitTask.Post();
    }

    SuccessOrExit(error = mTls.Send(*message));
    LogDebg("Transmit");

exit:
    FreeMessageOnError(message, error);
    LogWarnOnError(error, "transmit");
}

Error BleSecure::HandleTransport(void *aContext, ot::Message &aMessage, const Ip6::MessageInfo &aMessageInfo)
{
    OT_UNUSED_VARIABLE(aMessageInfo);
    return static_cast<BleSecure *>(aContext)->HandleTransport(aMessage);
}

Error BleSecure::HandleTransport(ot::Message &aMessage)
{
    otBleRadioPacket packet;
    uint16_t         len    = aMessage.GetLength();
    uint16_t         offset = 0;
    Error            error  = kErrorNone;

    while (len > 0)
    {
        if (len <= mMtuSize - kGattOverhead)
        {
            packet.mLength = len;
        }
        else
        {
            packet.mLength = mMtuSize - kGattOverhead;
        }

        if (packet.mLength > kPacketBufferSize)
        {
            packet.mLength = kPacketBufferSize;
        }

        IgnoreError(aMessage.Read(offset, mPacketBuffer, packet.mLength));
        packet.mValue = mPacketBuffer;
        packet.mPower = OT_BLE_DEFAULT_POWER;

        SuccessOrExit(error = otPlatBleGattServerIndicate(&GetInstance(), kTxBleHandle, &packet));

        len -= packet.mLength;
        offset += packet.mLength;
    }

    aMessage.Free();
exit:
    return error;
}

} // namespace Ble
} // namespace ot

void otPlatBleGattServerOnWriteRequest(otInstance *aInstance, uint16_t aHandle, const otBleRadioPacket *aPacket)
{
    OT_UNUSED_VARIABLE(aHandle); // Only a single handle is expected for RX

    VerifyOrExit(aPacket != nullptr);
    IgnoreError(AsCoreType(aInstance).Get<Ble::BleSecure>().HandleBleReceive(aPacket->mValue, aPacket->mLength));
exit:
    return;
}

void otPlatBleGapOnConnected(otInstance *aInstance, uint16_t aConnectionId)
{
    AsCoreType(aInstance).Get<Ble::BleSecure>().HandleBleConnected(aConnectionId);
}

void otPlatBleGapOnDisconnected(otInstance *aInstance, uint16_t aConnectionId)
{
    AsCoreType(aInstance).Get<Ble::BleSecure>().HandleBleDisconnected(aConnectionId);
}

void otPlatBleGattOnMtuUpdate(otInstance *aInstance, uint16_t aMtu)
{
    IgnoreError(AsCoreType(aInstance).Get<Ble::BleSecure>().HandleBleMtuUpdate(aMtu));
}

#endif // OPENTHREAD_CONFIG_BLE_TCAT_ENABLE
