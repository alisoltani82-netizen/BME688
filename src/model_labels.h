#pragma once

// Class names for the Bosch FieldAir_HandSanitizer classification model.
// GAS_ESTIMATE_1 = Field Air probability  [0..1]
// GAS_ESTIMATE_2 = Hand Sanitizer probability [0..1]
static const char* kModelLabels[4] = {
    "Field Air",
    "Hand Sanitizer",
    "Undefined 3",
    "Undefined 4"
};
