#include "disp/latest_frame.hpp"

#include <utility>

// ---------------------------------------------------------------------------
// TP-P1c — corrigé commenté (LatestFrame).
// ---------------------------------------------------------------------------
namespace disp {

void LatestFrame::store(std::shared_ptr<const cam::Frame> frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Si une trame attendait déjà sans avoir été consommée, on la jette : elle
    // est périmée. On la compte (télémétrie) puis on la remplace.
    if (slot_) {
        ++dropped_;
    }
    slot_ = std::move(frame);
}

std::shared_ptr<const cam::Frame> LatestFrame::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    // On récupère la trame et on VIDE le slot : un take() suivant renverra
    // nullptr tant qu'une nouvelle trame n'aura pas été déposée. Le move évite
    // une copie du shared_ptr et laisse slot_ à nullptr d'un seul geste.
    return std::move(slot_);
}

std::uint64_t LatestFrame::dropped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

} // namespace disp
