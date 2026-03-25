#pragma once

#include <Arduino.h>

// Replace src/config/model/bsec_selectivity.txt with your AI-Studio export.
const uint8_t bsec_config[] = {
#include "config/model/bsec_selectivity.txt"
};
