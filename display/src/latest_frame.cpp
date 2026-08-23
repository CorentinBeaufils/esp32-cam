#include "disp/latest_frame.hpp"

// ---------------------------------------------------------------------------
// LatestFrame : contrat dans disp/latest_frame.hpp.
//
// Le seul enjeu ici est la surete entre threads (handoff reseau -> affichage).
//
// Objectif ThreadSanitizer PROPRE :
//   cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
//   cmake --build build-tsan && ctest --test-dir build-tsan -R display
// ---------------------------------------------------------------------------
namespace disp {

void LatestFrame::store(std::shared_ptr<const cam::Frame> frame) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (slot_) ++dropped_;
    slot_ = std::move(frame);

}

std::shared_ptr<const cam::Frame> LatestFrame::take() {
    std::lock_guard<std::mutex> lock(mutex_);

    return std::move(slot_);
}

std::uint64_t LatestFrame::dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

} // namespace disp
