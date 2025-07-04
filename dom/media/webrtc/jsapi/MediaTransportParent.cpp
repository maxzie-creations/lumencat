/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/MediaTransportParent.h"
#include "jsapi/MediaTransportHandler.h"

#include "transport/sigslot.h"
#include "common/browser_logging/CSFLog.h"
#include "MediaEventSource.h"

namespace mozilla {

// Deals with the MediaTransportHandler interface, so MediaTransportParent
// doesn't have to..
class MediaTransportParent::Impl : public sigslot::has_slots<> {
 public:
  explicit Impl(MediaTransportParent* aParent)
      : mHandler(MediaTransportHandler::Create()), mParent(aParent) {
    mCandidateListener = mHandler->GetCandidateGathered().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnCandidate);
    mAlpnNegotiatedListener = mHandler->GetAlpnNegotiated().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnAlpnNegotiated);
    mGatheringStateChangeListener = mHandler->GetGatheringStateChange().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnGatheringStateChange);
    mConnectionStateChangeListener =
        mHandler->GetConnectionStateChange().Connect(
            GetCurrentSerialEventTarget(), this,
            &MediaTransportParent::Impl::OnConnectionStateChange);
    mRtpPacketListener = mHandler->GetRtpPacketReceived().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnPacketReceived);
    mSctpPacketListener = mHandler->GetSctpPacketReceived().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnPacketReceived);
    mEncryptedSendingListener = mHandler->GetEncryptedSending().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnEncryptedSending);
    mStateChangeListener = mHandler->GetStateChange().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnStateChange);
    mRtcpStateChangeListener = mHandler->GetRtcpStateChange().Connect(
        GetCurrentSerialEventTarget(), this,
        &MediaTransportParent::Impl::OnRtcpStateChange);
    mTarget = GetCurrentSerialEventTarget();
  }

  virtual ~Impl() {
    MOZ_ASSERT(mTarget->IsOnCurrentThread());
    mCandidateListener.DisconnectIfExists();
    mAlpnNegotiatedListener.DisconnectIfExists();
    mGatheringStateChangeListener.DisconnectIfExists();
    mConnectionStateChangeListener.DisconnectIfExists();
    mRtpPacketListener.DisconnectIfExists();
    mSctpPacketListener.DisconnectIfExists();
    mEncryptedSendingListener.DisconnectIfExists();
    mStateChangeListener.DisconnectIfExists();
    mRtcpStateChangeListener.DisconnectIfExists();
    mHandler = nullptr;
  }

  void OnCandidate(const std::string& aTransportId,
                   const CandidateInfo& aCandidateInfo) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnCandidate(aTransportId, aCandidateInfo));
  }

  void OnAlpnNegotiated(const std::string& aAlpn, bool aPrivacyRequested) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnAlpnNegotiated(aAlpn));
  }

  void OnGatheringStateChange(const std::string& aTransportId,
                              dom::RTCIceGathererState aState) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnGatheringStateChange(
        aTransportId, static_cast<int>(aState)));
  }

  void OnConnectionStateChange(const std::string& aTransportId,
                               dom::RTCIceTransportState aState) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnConnectionStateChange(
        aTransportId, static_cast<int>(aState)));
  }

  void OnPacketReceived(const std::string& aTransportId,
                        const MediaPacket& aPacket) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnPacketReceived(aTransportId, aPacket));
  }

  void OnEncryptedSending(const std::string& aTransportId,
                          const MediaPacket& aPacket) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnEncryptedSending(aTransportId, aPacket));
  }

  void OnStateChange(const std::string& aTransportId,
                     TransportLayer::State aState) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnStateChange(aTransportId, aState));
  }

  void OnRtcpStateChange(const std::string& aTransportId,
                         TransportLayer::State aState) {
    NS_ENSURE_TRUE_VOID(mParent->SendOnRtcpStateChange(aTransportId, aState));
  }

  RefPtr<MediaTransportHandler> mHandler;

 private:
  MediaTransportParent* mParent;
  MediaEventListener mCandidateListener;
  MediaEventListener mAlpnNegotiatedListener;
  MediaEventListener mGatheringStateChangeListener;
  MediaEventListener mConnectionStateChangeListener;
  MediaEventListener mRtpPacketListener;
  MediaEventListener mSctpPacketListener;
  MediaEventListener mEncryptedSendingListener;
  MediaEventListener mStateChangeListener;
  MediaEventListener mRtcpStateChangeListener;
  RefPtr<nsISerialEventTarget> mTarget;
};

MediaTransportParent::MediaTransportParent() : mImpl(new Impl(this)) {}

MediaTransportParent::~MediaTransportParent() {}

mozilla::ipc::IPCResult MediaTransportParent::RecvGetIceLog(
    const nsCString& pattern, GetIceLogResolver&& aResolve) {
  mImpl->mHandler->GetIceLog(pattern)->Then(
      GetCurrentSerialEventTarget(), __func__,
      // IPDL doesn't give us a reject function, so we cannot reject async, so
      // we are forced to resolve with an empty result. Laaaaaaame.
      [aResolve = std::move(aResolve)](
          MediaTransportHandler::IceLogPromise::ResolveOrRejectValue&&
              aResult) mutable {
        WebrtcGlobalLog logLines;
        if (aResult.IsResolve()) {
          logLines = std::move(aResult.ResolveValue());
        }
        aResolve(logLines);
      });

  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvClearIceLog() {
  mImpl->mHandler->ClearIceLog();
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvEnterPrivateMode() {
  mImpl->mHandler->EnterPrivateMode();
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvExitPrivateMode() {
  mImpl->mHandler->ExitPrivateMode();
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvCreateIceCtx(
    const string& name) {
  mImpl->mHandler->CreateIceCtx(name);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvSetIceConfig(
    nsTArray<RTCIceServer>&& iceServers,
    const RTCIceTransportPolicy& icePolicy) {
  nsresult rv = mImpl->mHandler->SetIceConfig(iceServers, icePolicy);
  if (NS_FAILED(rv)) {
    return ipc::IPCResult::Fail(WrapNotNull(this), __func__,
                                "MediaTransportHandler::SetIceConfig failed");
  }
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvSetProxyConfig(
    const net::WebrtcProxyConfig& aProxyConfig) {
  mImpl->mHandler->SetProxyConfig(NrSocketProxyConfig(aProxyConfig));
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvEnsureProvisionalTransport(
    const string& transportId, const string& localUfrag, const string& localPwd,
    const int& componentCount) {
  mImpl->mHandler->EnsureProvisionalTransport(transportId, localUfrag, localPwd,
                                              componentCount);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult
MediaTransportParent::RecvSetTargetForDefaultLocalAddressLookup(
    const std::string& targetIp, uint16_t targetPort) {
  mImpl->mHandler->SetTargetForDefaultLocalAddressLookup(targetIp, targetPort);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvStartIceGathering(
    const bool& defaultRouteOnly, const bool& obfuscateHostAddresses,
    const net::NrIceStunAddrArray& stunAddrs) {
  mImpl->mHandler->StartIceGathering(defaultRouteOnly, obfuscateHostAddresses,
                                     stunAddrs);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvActivateTransport(
    const string& transportId, const string& localUfrag, const string& localPwd,
    const int& componentCount, const string& remoteUfrag,
    const string& remotePwd, nsTArray<uint8_t>&& keyDer,
    nsTArray<uint8_t>&& certDer, const int& authType, const bool& dtlsClient,
    const DtlsDigestList& digests, const bool& privacyRequested) {
  mImpl->mHandler->ActivateTransport(
      transportId, localUfrag, localPwd, componentCount, remoteUfrag, remotePwd,
      keyDer, certDer, static_cast<SSLKEAType>(authType), dtlsClient, digests,
      privacyRequested);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvRemoveTransportsExcept(
    const StringVector& transportIds) {
  std::set<std::string> ids(transportIds.begin(), transportIds.end());
  mImpl->mHandler->RemoveTransportsExcept(ids);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvStartIceChecks(
    const bool& isControlling, const StringVector& iceOptions) {
  mImpl->mHandler->StartIceChecks(isControlling, iceOptions);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvSendPacket(
    const string& transportId, MediaPacket&& packet) {
  mImpl->mHandler->SendPacket(transportId, std::move(packet));
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvAddIceCandidate(
    const string& transportId, const string& candidate, const string& ufrag,
    const string& obfuscatedAddr) {
  mImpl->mHandler->AddIceCandidate(transportId, candidate, ufrag,
                                   obfuscatedAddr);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvUpdateNetworkState(
    const bool& online) {
  mImpl->mHandler->UpdateNetworkState(online);
  return ipc::IPCResult::Ok();
}

mozilla::ipc::IPCResult MediaTransportParent::RecvGetIceStats(
    const string& transportId, const double& now,
    GetIceStatsResolver&& aResolve) {
  mImpl->mHandler->GetIceStats(transportId, now)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          // IPDL doesn't give us a reject function, so we cannot reject async,
          // so we are forced to resolve with an unmodified result. Laaaaaaame.
          [aResolve = std::move(aResolve)](
              dom::RTCStatsPromise::ResolveOrRejectValue&& aResult) {
            if (aResult.IsResolve()) {
              aResolve(aResult.ResolveValue());
            } else {
              aResolve(MakeUnique<dom::RTCStatsCollection>());
            }
          });

  return ipc::IPCResult::Ok();
}

}  // namespace mozilla
