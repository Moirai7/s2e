#include <s2e/ConfigFile.h>
#include <s2e/S2E.h>
#include <s2e/SymbolicHardwareHook.h>
#include <s2e/Utils.h>
#include <llvm/Support/CommandLine.h>
#include "SymbolPeripheral.h"

namespace {
llvm::cl::opt<bool> DebugSymbHw("debug-symbolic-hardware", llvm::cl::init(false));
}

namespace s2e {
namespace plugins {
namespace hw {

extern "C" {
static bool symbhw_is_mmio_symbolic(struct MemoryDesc *mr, uint64_t physaddr, uint64_t size, void *opaque);}
static klee::ref<klee::Expr> symbhw_symbread(struct MemoryDesc *mr, uint64_t physaddress,
                                             const klee::ref<klee::Expr> &value, SymbolicHardwareAccessType type,
                                             void *opaque);

static void symbhw_symbwrite(struct MemoryDesc *mr, uint64_t physaddress, const klee::ref<klee::Expr> &value,
                             SymbolicHardwareAccessType type, void *opaque);


S2E_DEFINE_PLUGIN(SymbolicPeripheral, "SymbolicPeripheral S2E plugin", "", );

void SymbolicPeripheral::initialize() {
    if (!parseConfigIoT()) {
        getWarningsStream() << "Could not parse config\n";
        exit(-1);
    }

    //g_symbolicPortHook = SymbolicPortHook(symbhw_is_symbolic, symbhw_symbportread, symbhw_symbportwrite, this);
    g_symbolicMemoryHook = SymbolicMemoryHook(symbhw_is_mmio_symbolic, symbhw_symbread, symbhw_symbwrite, this);
}
template <typename T, typename U> inline bool SymbolicPeripheral::isSymbolic(T ports, U port) {
    for (auto &p : ports) {
        if (port >= p.first && port <= p.second) {
            return true;
        }
    }

    return false;
}

///
/// \brief myplugin::parseConfig
///
/// pluginsConfig.SymbolicHarwdare {
///     dev1 = {
///         ports = {
///             {0x100, 0x101},
///             {0x100, 0x103},
///         }
///
///         mem = {
///             {0xfffc0000, 0xfffcffff},
///         }
///     }
/// }
///
/// \return true if parsing was successful
///
bool SymbolicPeripheral::parseConfigIoT(void) {
    ConfigFile *cfg = s2e()->getConfig();
    auto keys = cfg->getListKeys(getConfigKey());

    SymbolicMmioRange m;

    // ARM MMIO range 0x40000000-0x60000000
    m.first = 0x40000000;
    m.second = 0x5fffffff;

    getDebugStream() << "Adding symbolic mmio range: " << hexval(m.first) << " - " << hexval(m.second) << "\n";
    m_mmio.push_back(m);

    return true;
}
bool SymbolicPeripheral::isMmioSymbolic(uint64_t physAddr) {
    return isSymbolic(m_mmio, physAddr);
}
static bool symbhw_is_mmio_symbolic(struct MemoryDesc *mr, uint64_t physaddr, uint64_t size, void *opaque) {
    SymbolicPeripheral *hw = static_cast<SymbolicPeripheral *>(opaque);
    return hw->isMmioSymbolic(physaddr);
}

static void SymbHwGetConcolicVector(uint64_t in, unsigned size, ConcreteArray &out) {
    union {
        // XXX: assumes little endianness!
        uint64_t value;
        uint8_t array[8];
    };

    value = in;
    out.resize(size);
    for (unsigned i = 0; i < size; ++i) {
        out[i] = array[i];
    }
}

klee::ref<klee::Expr> SymbolicPeripheral::createExpression(S2EExecutionState *state, SymbolicHardwareAccessType type,
                                                         uint64_t address, unsigned size, uint64_t concreteValue) {
    bool createVariable = true;
    onSymbolicRegisterRead.emit(state, type, address, size, &createVariable);

    std::stringstream ss;
    switch (type) {
        case SYMB_MMIO:
            ss << "iommuread_";
            break;
        case SYMB_DMA:
            ss << "dmaread_";
            break;
	case SYMB_PORT:
            ss << "portread_";
            break;
    }

    ss << hexval(address) << "@" << hexval(state->regs()->getPc());
    getDebugStream(g_s2e_state) << ss.str() << " size " << hexval(size) << " value=" << hexval(concreteValue)
                                << " sym=" << (createVariable ? "yes" : "no") << "\n";

    ConcreteArray concolicValue;
    SymbHwGetConcolicVector(concreteValue, size, concolicValue);
    return state->createSymbolicValue(ss.str(), size * 8, concolicValue);
}
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

// XXX: remove MemoryDesc
static klee::ref<klee::Expr> symbhw_symbread(struct MemoryDesc *mr, uint64_t physaddress,
                                             const klee::ref<klee::Expr> &value, SymbolicHardwareAccessType type,
                                             void *opaque) {
    SymbolicPeripheral *hw = static_cast<SymbolicPeripheral *>(opaque);

    if (DebugSymbHw) {
        hw->getDebugStream(g_s2e_state) << "reading mmio " << hexval(physaddress) << " value: " << value << "\n";
    }

    unsigned size = value->getWidth() / 8;
    uint64_t concreteValue = g_s2e_state->toConstantSilent(value)->getZExtValue();
    return hw->createExpression(g_s2e_state, SYMB_MMIO, physaddress, size, concreteValue);
}

static void symbhw_symbwrite(struct MemoryDesc *mr, uint64_t physaddress, const klee::ref<klee::Expr> &value,
                             SymbolicHardwareAccessType type, void *opaque) {
    SymbolicPeripheral *hw = static_cast<SymbolicPeripheral *>(opaque);
	uint32_t curPc = g_s2e_state->regs()->getPc();
    if (DebugSymbHw) {
        hw->getDebugStream(g_s2e_state) << "writing mmio " << hexval(physaddress) << " value: " << value
                                        << " pc: " << hexval(curPc) << "\n";
    }

    // TODO: return bool to not call original handler, like for I/O
}


} // namespace hw
}// namespace plugins
} // namespace s2e
