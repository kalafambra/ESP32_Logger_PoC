#pragma once

class EnvironmentHistory;
class PickupDetector;

// Run once before acquisition. These tests reuse live buffers and leave them
// empty; running them after startup would destroy current detection/history.
bool runEnvironmentHistorySelfTests(EnvironmentHistory &history);
bool runPickupSelfTests(PickupDetector &detector);
bool runPickupEventSelfTests();
