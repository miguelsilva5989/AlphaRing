#include "BuildManifest.h"

#include <offset_mcc.h>

namespace AlphaRing::Compatibility {
    const BuildManifest& CurrentBuild() {
        static constexpr BuildManifest manifest {
            "MCC-Win64-Shipping.exe",
            "1.3528.0.0",
            {
                0x4000BA0,
                0x3F7B190,
                0x4001B78,
                OFFSET_MCC_PF_DELTA_TIME,
                0x4000B9F,
                0x4000BC8,
                0x38A09C,
            },
        };
        return manifest;
    }
}
