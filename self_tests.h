#pragma once

class PickupDetector;

// Run once before acquisition. These tests reuse live buffers and leave them
// empty; running them after startup would destroy current detector state.
bool runPickupSelfTests(PickupDetector &detector);
bool runPickupEventSelfTests();
