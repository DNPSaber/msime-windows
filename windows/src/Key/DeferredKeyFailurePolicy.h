#pragma once

// What a failed key costs. An eaten key is never handed back to the host and
// never sent twice: whatever went wrong, the key is dropped. The reason only
// decides how much session state has to be discarded with it. Kept free of
// TSF/COM and Windows headers so the mapping is unit-tested
// (tests/deferred_key_failure_policy.cpp).

// Why an eaten key could not be applied.
enum class DeferredKeyFailureReason
{
    // Its focus token, composition epoch, reset token or focus generation was
    // replaced before it ran. Whatever replaced it owns the cleanup.
    Superseded,
    // The host refused or failed the edit session (S_FALSE from GetSelection,
    // E_NOTIMPL, TF_E_*), or the handler itself could not apply the key.
    HostEditRejected,
    // RequestEditSession failed, or the edit session could not be allocated.
    EditSessionRequestFailed,
    // The key's async request could not be posted or handed to a context.
    AsyncPostFailed,
    // Write failure, reply read failure or invalid frame, dead pipe, worker
    // disconnect, or a focus session that could not be established.
    TransportBroken,
    // The request was written but its reply never arrived: the Server may
    // already have acted on it (committed a candidate). Never resend.
    DeliveryAmbiguous,
};

// How much state the failure discards.
enum class DeferredKeyFailureKind
{
    // Drop the key only. No reset is started.
    Stale,
    // Offline raw lane: the TSF composition is the only authority. Drop the
    // key and the keys queued behind it; the composition stays.
    Offline,
    // Pipes are fine, but the local composition and the Server may disagree:
    // drop the key and the queue, cancel the composition locally and clear
    // the Server composition on the current focus token.
    Resync,
    // The transport is broken or its state is unknown: drop the key and the
    // queue, and run the full reset (new focus token, reply pipe reopened).
    Transport,
};

inline DeferredKeyFailureKind ResolveDeferredKeyFailure(DeferredKeyFailureReason reason, bool offlineLaneActive)
{
    switch (reason)
    {
    case DeferredKeyFailureReason::Superseded:
        return DeferredKeyFailureKind::Stale;
    case DeferredKeyFailureReason::TransportBroken:
    case DeferredKeyFailureReason::DeliveryAmbiguous:
        return DeferredKeyFailureKind::Transport;
    case DeferredKeyFailureReason::HostEditRejected:
    case DeferredKeyFailureReason::EditSessionRequestFailed:
    case DeferredKeyFailureReason::AsyncPostFailed:
    default:
        return offlineLaneActive ? DeferredKeyFailureKind::Offline : DeferredKeyFailureKind::Resync;
    }
}

// Reason for an edit session that ran but did not apply its key. The caller
// derives the flags from the session's own validation and HRESULT.
inline DeferredKeyFailureReason ClassifyEditSessionFailure(bool superseded, bool deliveryAmbiguous,
                                                           bool transportBroken)
{
    if (superseded)
    {
        return DeferredKeyFailureReason::Superseded;
    }
    if (deliveryAmbiguous)
    {
        return DeferredKeyFailureReason::DeliveryAmbiguous;
    }
    if (transportBroken)
    {
        return DeferredKeyFailureReason::TransportBroken;
    }
    return DeferredKeyFailureReason::HostEditRejected;
}
