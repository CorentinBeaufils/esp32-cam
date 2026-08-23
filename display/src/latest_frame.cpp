#include "disp/latest_frame.hpp"

// ---------------------------------------------------------------------------
// TP-P1c — à toi de jouer.   Contrat : disp/latest_frame.hpp   Corrigé : solution/
//
// Format allégé : pas de TODO pas-à-pas. Le contrat et les tests disent QUOI
// faire ; à toi le COMMENT. Le seul enjeu est la sûreté entre threads.
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
