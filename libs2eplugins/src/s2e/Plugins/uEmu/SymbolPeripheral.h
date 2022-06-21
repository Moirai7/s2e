#ifndef S2E_PLUGINS_SymbolicHardware_H
#define S2E_PLUGINS_SymbolicHardware_H

#include <inttypes.h>
#include <vector>

#include <s2e/CorePlugin.h>
#include <s2e/Plugin.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/SymbolicHardwareHook.h>

#include <llvm/ADT/SmallVector.h>

namespace s2e {
namespace plugins {
namespace hw {

typedef std::vector<uint8_t> ConcreteArray;
typedef std::pair<uint16_t, uint16_t> SymbolicPortRange;
typedef std::pair<uint64_t, uint64_t> SymbolicMmioRange;

typedef llvm::SmallVector<SymbolicPortRange, 4> SymbolicPortRanges;
typedef llvm::SmallVector<SymbolicMmioRange, 4> SymbolicMmioRanges;

class SymbolicPeripheral : public Plugin {
    S2E_PLUGIN

private:
    // TODO: make this per-state and per-device
    SymbolicPortRanges m_ports;
    SymbolicMmioRanges m_mmio;

    template <typename T> bool parseRangeList(ConfigFile *cfg, const std::string &key, T &result);

    bool parseConfigIoT();

    template <typename T, typename U> inline bool isSymbolic(T ports, U port);

public:
    ///
    /// \brief onSymbolicRegisterRead control whether
    /// a symbolic value should be created upon a read from a symbolic
    /// hardware region.
    ///
    sigc::signal<void, S2EExecutionState *, SymbolicHardwareAccessType /* type */, uint64_t /* physicalAddress */,
                 unsigned /* size */, bool * /* createSymbolicValue */>
        onSymbolicRegisterRead;

    SymbolicPeripheral(S2E *s2e) : Plugin(s2e) {
    }
    bool isMmioSymbolic(uint64_t physAddr);
    void initialize();
    klee::ref<klee::Expr> createExpression(S2EExecutionState *state, SymbolicHardwareAccessType type, uint64_t address,
                                           unsigned size, uint64_t concreteValue);
};

} // namespace hw
} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_SymbolicHardware_H
