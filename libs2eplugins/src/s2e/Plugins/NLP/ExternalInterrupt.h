///
/// Copyright (C) 2010-2013, Dependable Systems Laboratory, EPFL
/// All rights reserved.
///
/// Licensed under the Cyberhaven Research License Agreement.
///

#ifndef S2E_PLUGINS_EXAMPLE_H
#define S2E_PLUGINS_EXAMPLE_H

#include <s2e/CorePlugin.h>
#include <s2e/Plugin.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/SymbolicHardwareHook.h>
#include <s2e/Plugins/NLP/NLPPeripheralModel.h>

namespace s2e {
namespace plugins {
class ExternalInterrupt : public Plugin {
    S2E_PLUGIN
public:
    ExternalInterrupt(S2E *s2e) : Plugin(s2e) {
    }
    void initialize();

private:
    NLPPeripheralModel *onNLPPeripheralModelConnection;
    /*uint32_t tb_interval;*/
    /*uint32_t tb_scale;*/
    bool systick_disable_flag; // used for state 0
    std::vector<uint32_t> disable_irqs;
    uint64_t systick_begin_point;

    void onExternelInterruptTrigger(S2EExecutionState *state, uint32_t irq_no);
};

} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_EXAMPLE_H
