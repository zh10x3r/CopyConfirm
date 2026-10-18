#pragma once
#include <cstdint>
#include <optional>

namespace copyconfirm {
using Tick = std::uint64_t;

struct CopyEvent {
    std::uint64_t id{};
    Tick time{};
    std::uint32_t baseline{};
    std::uint32_t sourcePid{};
    std::uintptr_t sourceWindow{};
};

struct Snapshot {
    std::uint32_t sequence{};
    std::uint32_t ownerPid{};
    std::uint32_t foregroundPid{};
    bool sourceAlive{true};
};

enum class ProbeStatus : std::uint32_t {
    Pending, Ok, Busy, Stale, WrongOwner, NoData, Unsupported,
    Unreadable, Timeout, Canceled, StartFailed
};

struct ProbeTask {
    CopyEvent copy;
    std::uint32_t candidateSequence{};
};

struct ProbeResult {
    std::uint64_t id{};
    ProbeStatus status{ProbeStatus::StartFailed};
    std::uint32_t sequence{};
    std::uint32_t ownerPid{};
    std::uint32_t format{};
    std::uint32_t error{};
};

// A copy key alone is never sufficient. Only a matching, fresh, readable
// clipboard update can complete an attempt. Unknown cases intentionally miss.
class CopyGate {
public:
    static constexpr Tick DeadlineMs = 1800;
    static constexpr Tick SettleMs = 65;

    void Begin(CopyEvent event) {
        Cancel();
        if (event.baseline && event.sourcePid && event.sourceWindow) {
            attempt_ = event;
            lastSequence_ = event.baseline;
            stableSince_ = event.time;
        }
    }

    void Cancel() { attempt_.reset(); probing_ = false; probes_ = 0; blockedSequence_ = 0; }
    bool Pending() const { return attempt_.has_value(); }

    std::optional<ProbeTask> Observe(Tick now, const Snapshot& snapshot) {
        if (!ContextValid(now, snapshot)) { Cancel(); return {}; }
        if (snapshot.sequence != lastSequence_) {
            lastSequence_ = snapshot.sequence;
            stableSince_ = now;
        }
        if (probing_ || !snapshot.sequence || snapshot.sequence == attempt_->baseline ||
            snapshot.ownerPid != attempt_->sourcePid ||
            snapshot.sequence == blockedSequence_ || now - stableSince_ < SettleMs ||
            probes_ >= 3) return {};
        probing_ = true;
        ++probes_;
        return ProbeTask{*attempt_, snapshot.sequence};
    }

    bool Complete(Tick now, const ProbeResult& result, const Snapshot& snapshot) {
        if (!attempt_ || result.id != attempt_->id) return false;
        probing_ = false;
        if (!ContextValid(now, snapshot)) { Cancel(); return false; }
        const bool confirmed = result.status == ProbeStatus::Ok && result.sequence &&
            result.sequence != attempt_->baseline && result.sequence == snapshot.sequence &&
            result.ownerPid == attempt_->sourcePid && snapshot.ownerPid == attempt_->sourcePid;
        if (confirmed) { Cancel(); return true; }
        if (result.status == ProbeStatus::Timeout || result.status == ProbeStatus::Canceled) {
            Cancel();
        } else {
            stableSince_ = now;
            if (result.status == ProbeStatus::NoData || result.status == ProbeStatus::Unsupported)
                blockedSequence_ = snapshot.sequence;
        }
        return false;
    }

    const std::optional<CopyEvent>& Attempt() const { return attempt_; }

private:
    bool ContextValid(Tick now, const Snapshot& snapshot) const {
        return attempt_ && now >= attempt_->time && now - attempt_->time <= DeadlineMs &&
            snapshot.sourceAlive && snapshot.foregroundPid == attempt_->sourcePid;
    }
    std::optional<CopyEvent> attempt_;
    std::uint32_t lastSequence_{};
    std::uint32_t blockedSequence_{};
    Tick stableSince_{};
    unsigned probes_{};
    bool probing_{};
};

// The Windows callback passes through every key. This class only detects a
// fresh C keydown with Ctrl, and rejects Ctrl+Shift/Alt/Win+C and injected keys.
class KeyChord {
public:
    bool Feed(bool isC, bool down, bool ctrl, bool shift, bool alt, bool win,
              bool injected, bool enabled) {
        if (!isC || injected) return false;
        if (!down) { cDown_ = false; return false; }
        if (cDown_) return false;
        cDown_ = true;
        return enabled && ctrl && !shift && !alt && !win;
    }
private:
    bool cDown_{};
};
}
