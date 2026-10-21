#pragma once

namespace quota {

class QuotaService;

// Shows a modal dialog letting the user pick up to two providers to display.
// Returns true when the selection changed.
bool ShowProviderSelector(void* parent_window, QuotaService& service);

} // namespace quota
