//
/// Copyright (C) 2010-2015, Dependable Systems Laboratory, EPFL
/// All rights reserved.
///
/// Licensed under the Cyberhaven Research License Agreement.
///

#include "NLPPeripheralModel.h"
#include <s2e/ConfigFile.h>
#include <s2e/S2E.h>
#include <s2e/SymbolicHardwareHook.h>
#include <s2e/Utils.h>
#include <s2e/cpu.h>
#include <sys/shm.h>
#include <time.h>
#include <algorithm>
#include <random>

namespace s2e {
namespace plugins {

S2E_DEFINE_PLUGIN(NLPPeripheralModel, "NLP Peripheral Model With Auto Timer", "NLPPeripheralModel");

class NLPPeripheralModelState : public PluginState {
private:
    RegMap state_map;
    //std::map<uint32_t, DMA> dma_map;
    std::map<int, int> exit_interrupt; // interrupt id, num
    std::map<uint32_t, uint32_t> interrupt_freq;
    uint32_t fork_point_count;
    bool instruction;
    // std::map<int, bool> exit_interrupt;
    uint32_t cur_dp_addr;

public:
    NLPPeripheralModelState() {
        interrupt_freq.clear();
        fork_point_count = 0;
        instruction = false;
    }

    virtual ~NLPPeripheralModelState() {
    }

    static PluginState *factory(Plugin *, S2EExecutionState *) {
        return new NLPPeripheralModelState();
    }

    NLPPeripheralModelState *clone() const {
        return new NLPPeripheralModelState(*this);
    }

    bool pending_interrupt() {
        for (auto irq : exit_interrupt) {
            if (irq.second > 0 && interrupt_freq[irq.first] < 2) {
                return true;
            }
        }
        return false;
    }
    bool get_exit_interrupt(uint32_t num) {
        return exit_interrupt[num] > 0;
        // return exit_interrupt[num];
    }

    void set_exit_interrupt(uint32_t num, int cur) {
        exit_interrupt[num] += cur;
        // exit_interrupt[num] = cur;
    }

    RegMap get_state_map() {
        return state_map;
    }

    void insert_reg_map(uint32_t phaddr, PeripheralReg reg) {
        state_map[phaddr] = reg;
    }

    void write_ph_value(uint32_t phaddr, uint32_t value) {
        state_map[phaddr].cur_value = value;
    }

    uint32_t get_ph_value(uint32_t phaddr) {
        return state_map[phaddr].cur_value;
    }

    bool check_instruction() {
        return instruction;
    }
    void receive_instruction(uint32_t phaddr) {
        if (instruction && state_map[phaddr].r_value.empty()) {
            instruction = false;
            state_map[phaddr].t_value = 0;
        }
    }

    void write_dr_value(uint32_t phaddr, uint32_t value, uint32_t width, bool fork_point) {
        //state_map[phaddr].t_value = value;
        state_map[phaddr].t_value = (state_map[phaddr].t_value << width * 8) + value;
        state_map[phaddr].t_size = 0; // width;
        if (state_map[phaddr].t_value == 0xAAFA && !fork_point) {
            std::queue<uint8_t> tmp;
            tmp.push(0x4F);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            tmp.push(0x4B);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            tmp.push(0x0D);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            tmp.push(0x0A);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            state_map[phaddr].r_value = tmp;
            state_map[phaddr].r_size = 32; // * 4;
            instruction = true;
        } else if (phaddr == 0x40028014 && (state_map[phaddr].t_value & 0x8000)) {
            std::queue<uint8_t> tmp;
            tmp.push(0x4);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            state_map[phaddr].r_value = tmp;
            state_map[phaddr].r_size = 8; //32;
            instruction = true;
        } else if (phaddr == 0x40028014 && (state_map[phaddr].t_value & 0x1000)) {
            std::queue<uint8_t> tmp;
            tmp.push(0x20);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            state_map[phaddr].r_value = tmp;
            state_map[phaddr].r_size = 8; //32;
            instruction = true;
        } else if (phaddr == 0x40005410 && state_map[phaddr].t_value == 0x84) {
            std::queue<uint8_t> tmp;
            tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            tmp.push(0x16);
            //tmp.push(0x0);
            //tmp.push(0x0);
            //tmp.push(0x0);
            for (uint32_t i = 0; i < 64; i++) {
                tmp.push(0x1);
            }
            state_map[phaddr].r_value = tmp;
            state_map[phaddr].r_size = 66 * 8; //576;
            instruction = true;
        //} else if (phaddr == 0x4001300c || phaddr == 0x4000380c || phaddr == 0x40011004) {
        } //else if (phaddr == 0x4001300c || phaddr == 0x4000380c) {
	//	state_map[phaddr].r_value.push(value);
	//	state_map[phaddr].r_size += width*8;
	//}
    }

    void rx_push_to_fix_size(uint32_t phaddr, int size) {
        if (state_map[phaddr].r_value.size() < size) {
            for (unsigned j = 0; j < size - state_map[phaddr].r_value.size(); j++) {
                state_map[phaddr].r_value.push(0);
            }
        }
    }

    void clear_rx(uint32_t phaddr) {
        state_map[phaddr].r_value = {};
        state_map[phaddr].r_size = 0;
    }

    uint8_t get_dr_value(uint32_t phaddr, uint32_t width) {
        width *= 8;
        state_map[phaddr].r_size -= width;
        if (state_map[phaddr].r_value.empty()) {
            state_map[phaddr].r_size = 0;
            return 0;
        }
        uint8_t cur_value = state_map[phaddr].r_value.front();
        state_map[phaddr].r_value.pop();
        return cur_value;
    }

    uint8_t get_rx_size(uint32_t phaddr) {
        return state_map[phaddr].r_size;
    }

    void hardware_write_to_receive_buffer(uint32_t phaddr, std::queue<uint8_t> value, uint32_t width) {
        if (!state_map[phaddr].r_value.empty()) return;
	state_map[phaddr].r_size = width * 8;
        //state_map[phaddr].front_left = 8;     //front size
        state_map[phaddr].r_value = value;
    }

    void inc_irq_freq(uint32_t irq_no) {
        interrupt_freq[irq_no]++;
    }

    uint32_t get_irq_freq(uint32_t irq_no) {
        return interrupt_freq[irq_no];
    }

    void clear_irq_freq(uint32_t irq_no) {
        interrupt_freq[irq_no] = 0;
    }

    std::map<uint32_t, uint32_t> get_irqs_freq() {
        return interrupt_freq;
    }

    void inc_fork_count() {
        fork_point_count++;
    }

    uint32_t get_fork_point_count() {
        return fork_point_count;
    }
    // cur description loc
    void insert_cur_dp_addr(uint32_t mem_addr) {
        cur_dp_addr = mem_addr;
    }

    uint32_t get_cur_dp_addr() {
        return cur_dp_addr;
    }
};

bool NLPPeripheralModel::parseConfig(void) {
    ConfigFile *cfg = s2e()->getConfig();
    hw::PeripheralMmioRanges nlpphs;
    std::stringstream ss;
    ss << getConfigKey();
    getDebugStream() << "config " << ss.str() << "\n";
    if (!parseRangeList(cfg, ss.str() + ".nlp_mmio", nlpphs)) {
        return false;
    }

    for (auto nlpph : nlpphs) {
        getInfoStream() << "Adding nlp ph range " << hexval(nlpph.first) << " - "
                        << hexval(nlpph.second) << "\n";
        nlp_mmio.push_back(nlpph);
    }

    return true;
}

template <typename T>
bool NLPPeripheralModel::parseRangeList(ConfigFile *cfg, const std::string &key, T &result) {
    bool ok;

    int ranges = cfg->getListSize(key, &ok);
    if (!ok) {
        getWarningsStream() << "Could not parse ranges: " << key << "\n";
        return false;
    }

    for (int i = 0; i < ranges; ++i) {
        std::stringstream ss;
        ss << key << "[" << (i + 1) << "]";
        uint64_t start = cfg->getInt(ss.str() + "[1]", 0, &ok);
        if (!ok) {
            getWarningsStream() << "Could not parse start address: " << ss.str() + "[1]"
                                << "\n";
            return false;
        }

        uint64_t end = cfg->getInt(ss.str() + "[2]", 0, &ok);
        if (!ok) {
            getWarningsStream() << "Could not parse end address: " << ss.str() + "[2]"
                                << "\n";
            return false;
        }

        if (!(start <= end)) {
            getWarningsStream() << hexval(start) << " is greater than " << hexval(end) << "\n";
            return false;
        }

        result.push_back(std::make_pair(start, end));
    }

    return true;
}

void NLPPeripheralModel::initialize() {
    NLPfileName = s2e()->getConfig()->getString(getConfigKey() + ".NLPfileName", "all.txt");
    getDebugStream() << "NLP firmware name is " << NLPfileName << "\n";

    if (!parseConfig()) {
        getWarningsStream() << "Could not parse NLP range config\n";
        exit(-1);
    }

    hw::SymbolicPeripherals *symbolicPeripheralConnection = s2e()->getPlugin<hw::SymbolicPeripherals>();
    symbolicPeripheralConnection->onSymbolicNLPRegisterReadEvent.connect(
        sigc::mem_fun(*this, &NLPPeripheralModel::onPeripheralRead));
    symbolicPeripheralConnection->onSymbolicNLPRegisterWriteEvent.connect(
        sigc::mem_fun(*this, &NLPPeripheralModel::onPeripheralWrite));
    s2e()->getCorePlugin()->onEngineShutdown.connect(sigc::mem_fun(*this, &NLPPeripheralModel::onStatistics));

    bool ok;
    fork_point = s2e()->getConfig()->getInt(getConfigKey() + ".forkPoint", 0x0, &ok);
    getInfoStream() << "set fork_point phaddr = " << hexval(fork_point) << "\n";
    enable_fuzzing = s2e()->getConfig()->getBool(getConfigKey() + ".useFuzzer", false);
    begin_irq_flag = false;
    if (enable_fuzzing) {
        init_dr_flag = false;
        fork_point_flag = false;
        s2e()->getCorePlugin()->onTranslateBlockEnd.connect(
            sigc::mem_fun(*this, &NLPPeripheralModel::onTranslateBlockEnd));
        begin_point = s2e()->getConfig()->getInt(getConfigKey() + ".beginPoint", 0x0, &ok);
        if (!ok || begin_point == 0x0) {
            getWarningsStream() << " begin_point will be same as fork_point!\n";
            begin_point = fork_point;
        } else {
            getInfoStream() << "begin point = " << hexval(begin_point) << "\n";
        }
    } else {
        s2e()->getCorePlugin()->onTranslateBlockEnd.connect(
            sigc::mem_fun(*this, &NLPPeripheralModel::onTranslateBlockEnd));
        onInvalidStateDectionConnection = s2e()->getPlugin<InvalidStatesDetection>();
        onInvalidStateDectionConnection->onReceiveExternalDataEvent.connect(
            sigc::mem_fun(*this, &NLPPeripheralModel::onEnableReceive));
    }

    s2e()->getCorePlugin()->onTranslateBlockStart.connect(
        sigc::mem_fun(*this, &NLPPeripheralModel::onTranslateBlockStart));
    s2e()->getCorePlugin()->onExceptionExit.connect(sigc::mem_fun(*this, &NLPPeripheralModel::onExceptionExit));
    rw_count = 0;
    srand(time(NULL));
}

uint32_t NLPPeripheralModel::get_reg_value(S2EExecutionState *state, RegMap &state_map, Field a) {
    uint32_t res, phaddr;
    uint32_t cur_value;
    std::vector<long> bits;
    int start = 0;
    if (a.type == "T") {
        return state_map[a.phaddr].t_size;
    } else if (a.type == "R") {
        return state_map[a.phaddr].r_size;
    } else if (a.type == "L") {
        if (a.bits[0] >= 32) {
            for (auto b : a.bits) {
                bits.push_back(b % 32);
            }
            start = a.bits[0] / 32 * 32;
        } else
            bits = a.bits;
        phaddr = state_map[a.phaddr].cur_value;
        state->mem()->read(phaddr + start, &cur_value, sizeof(cur_value));
    } else {
        phaddr = a.phaddr;
        bits = a.bits;
        cur_value = state_map[phaddr].cur_value;
    }
    getDebugStream() << "get_reg_value phaddr " << hexval(phaddr) << " cur_value " << hexval(cur_value) << "\n";
    if (bits[0] == -1) {
        return cur_value;
    } else {
        res = 0;
        for (int i = 0; i < bits.size(); ++i) {
            int tmp = bits[i];
            res = (res << 1) + (cur_value >> tmp & 1);
        }
    }
    return res;
}

void NLPPeripheralModel::set_reg_value(S2EExecutionState *state, RegMap &state_map, Field a, uint32_t value) {
    uint32_t phaddr, cur_value;
    std::vector<long> bits;
    int start = 0;
    if (a.type == "L") {
        if (a.bits[0] >= 32) {
            for (auto b : a.bits) {
                bits.push_back(b % 32);
            }
            start = a.bits[0] / 32 * 32;
        } else
            bits = a.bits;
        phaddr = state_map[a.phaddr].cur_value;
        state->mem()->read(phaddr + start, &cur_value, sizeof(cur_value));
    } else {
        phaddr = a.phaddr;
        cur_value = state_map[phaddr].cur_value;
        bits = a.bits;
    }
    for (int i = 0; i < bits.size(); ++i) {
        int tmp = bits[i];
        int a2 = (value >> (bits.size() - 1 - i)) & 1;
        if (a2 == 1) {
            cur_value |= (1 << tmp);
        } else {
            cur_value &= ~(1 << tmp);
        }
    }
    if (a.type == "L") {
        state->mem()->write(phaddr + start, &cur_value, sizeof(cur_value));
    } else {
        if (bits[0] == -1) {
            state_map[phaddr].cur_value = value;
        } else {
            state_map[phaddr].cur_value = cur_value;
            getDebugStream() << "set_reg_value phaddr " << hexval(phaddr) << " cur_value " << hexval(cur_value) << "\n";
        }
    }
}

void NLPPeripheralModel::write_to_descriptor(S2EExecutionState *state, std::queue<uint8_t> buffer_input) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    //Fuzz
    uint32_t cur_dp_addr = plgState->get_cur_dp_addr();
    int count = buffer_input.size() / 1524;
    uint32_t frame_size = 0;
    if (count >= 4) {
        count = 3;
    }
    for (int i = 0; i <= count; ++i) {
        uint32_t RDES0 = 0, RDES1 = 0, RDES2 = 0, RDES3 = 0;
        bool ok = state->mem()->read(cur_dp_addr, &RDES0, sizeof(RDES0));
        ok &= state->mem()->read(cur_dp_addr + 4, &RDES1, sizeof(RDES1));
        ok &= state->mem()->read(cur_dp_addr + 8, &RDES2, sizeof(RDES2));
        ok &= state->mem()->read(cur_dp_addr + 12, &RDES3, sizeof(RDES3));
        getInfoStream() << ok << " \n";
        getInfoStream() << "phaddr " << hexval(cur_dp_addr) << " start descriptor RDES0 " << hexval(RDES0) << " RDES1 " << hexval(RDES1) << " RDES2 " << hexval(RDES2) << " RDES3 " << hexval(RDES3) << "\n";
        if (RDES0 >> 31 == 0) {
            return;
        }
        //ETH_DMARXDESC_OWN
        RDES0 &= 0x7FFFFFFF;
        if (i == count) {
            //ETH_DMARXDESC_LS bit 8
            RDES0 |= 0x100;
            //RER:Receive end of ring bit 15
            RDES1 |= 0x8000;
        }
        if (i == 0) {
            //ETH_DMARXDESC_FS bit 9
            RDES0 |= 0x200;
            //buffer 1 maximun size
            //maximun_size = 0x1FFF & RDES1;
            //ETH_DMARXDESC_FL add 32 to current frame
            frame_size = buffer_input.size();
        }
        //RCH: Second address chained bit 14
        RDES1 |= 0x4000;
        RDES0 = RDES0 + (frame_size << 16);
        if (buffer_input.size() > 1524) {
            //RBS1:Receive buffer 1 size
            RDES1 = RDES1 & 0xFFFFE5F4; //cur desc size 32
        } else {
            RDES1 = (RDES1 & 0xFFFFE000) + buffer_input.size();
        }
        //buffer content
        //Fuzz
        int cnt = 0;
        if (buffer_input.size() > 1524) {
            cnt = 1524;
        } else {
            cnt = buffer_input.size();
        }
        for (uint32_t j = 0; j < cnt; j++) {
            uint8_t content = buffer_input.front();
            state->mem()->write(RDES2 + j, &content, sizeof(content));
            buffer_input.pop();
        }
        state->mem()->write(cur_dp_addr, &RDES0, sizeof(RDES0));
        state->mem()->write(cur_dp_addr + 4, &RDES1, sizeof(RDES1));
        getInfoStream() << " write end descriptor RDES0 " << hexval(RDES0) << " RDES1 " << hexval(RDES1) << " RDES2 " << hexval(RDES2) << " RDES3 " << hexval(RDES3) << "\n";
        cur_dp_addr = RDES3;
    }
    plgState->insert_cur_dp_addr(cur_dp_addr);
}

bool NLPPeripheralModel::EmitDMA(S2EExecutionState *state, uint32_t irq_no) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    RegMap state_map = plgState->get_state_map();
    for (int i = 0; i < all_dmas.size(); ++i) {
        if (all_dmas[i].dma_irq != irq_no || all_dmas[i].state == 0) continue;
        getInfoStream() << "peri_dr = " << hexval(all_dmas[i].peri_dr.phaddr) << " memo reg:" << hexval(all_dmas[i].memory_addr.phaddr) << "\n";
        uint32_t rx_addr = get_reg_value(state, state_map, all_dmas[i].peri_dr);
        uint32_t memo_addr = get_reg_value(state, state_map, all_dmas[i].memory_addr);
        getInfoStream() << "DMA Request! phaddr =" << hexval(rx_addr) << " " << hexval(memo_addr) << " " << all_dmas[i].state << "\n";
        if (rx_addr == 0 || memo_addr == 0)
            break;
        if (all_dmas[i].state == 1) {
            // DMA request
            // at least 64B
            plgState->rx_push_to_fix_size(rx_addr, 64);
            getInfoStream() << "DMA Request!\n";
            for (unsigned i = 0; i < 32; ++i) {
                //uint8_t b = plgState->get_dr_value(rx_addr, 1);
                //if (!state->mem()->write(memo_addr + i, &b, sizeof(b))) {
                //    getWarningsStream(state) << "Can not write memory"
                //                             << " at " << hexval(memo_addr + i) << '\n';
                //    exit(-1);
                //}
            }
            getInfoStream() << "DMA Request! update1: " << hexval(all_dmas[i].HTIF.phaddr) << "\n";
            set_reg_value(state, state_map, all_dmas[i].HTIF, 1);
            plgState->insert_reg_map(all_dmas[i].HTIF.phaddr, state_map[all_dmas[i].HTIF.phaddr]);
            if (all_dmas[i].GIF.type != "N/A") {
                set_reg_value(state, state_map, all_dmas[i].GIF, 1);
                plgState->insert_reg_map(all_dmas[i].GIF.phaddr, state_map[all_dmas[i].GIF.phaddr]);
            }
            EmitIRQ(state, all_dmas[i].dma_irq);
            all_dmas[i].state = 2;
            return true;
        } else if (all_dmas[i].state == 2) {
            for (unsigned i = 32; i < 64; ++i) {
                //uint8_t b = plgState->get_dr_value(rx_addr, 1);
                //if (!state->mem()->write(memo_addr + i, &b, sizeof(b))) {
                //    getWarningsStream(state) << "Can not write memory"
                //                             << " at " << hexval(memo_addr + i) << '\n';
                //    exit(-1);
                //}
            }
            getInfoStream() << "DMA Request! update2: " << hexval(all_dmas[i].TCIF.phaddr) << "\n";
            set_reg_value(state, state_map, all_dmas[i].HTIF, 0);
            set_reg_value(state, state_map, all_dmas[i].TCIF, 1);
            plgState->insert_reg_map(all_dmas[i].HTIF.phaddr, state_map[all_dmas[i].HTIF.phaddr]);
            plgState->insert_reg_map(all_dmas[i].TCIF.phaddr, state_map[all_dmas[i].TCIF.phaddr]);
            if (all_dmas[i].GIF.type != "N/A") {
                set_reg_value(state, state_map, all_dmas[i].GIF, 1);
                plgState->insert_reg_map(all_dmas[i].GIF.phaddr, state_map[all_dmas[i].GIF.phaddr]);
            }
            plgState->clear_rx(rx_addr);
            EmitIRQ(state, all_dmas[i].dma_irq);
            all_dmas[i].state = 0;
            return true;
        }
    }
    return false;
}

void NLPPeripheralModel::onExceptionExit(S2EExecutionState *state, uint32_t irq_no) {
    if (irq_no <= 15) return;
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    // interrupt vector+16
    // if (irq_no > 15)
    // plgState->set_exit_interrupt(irq_no - 16, false);
    if (irq_no > 15)
        irq_no -= 16;

    getInfoStream() << "EXIT Interrupt IRQ" << irq_no << " exit_inter = " << plgState->get_exit_interrupt(irq_no)
                    << "\n";
    // flip timer flag
    if (timer)
        UpdateFlag(1);

    if (!EmitDMA(state, irq_no)) {
        for (auto dma : all_dmas) {
            if (irq_no == dma.dma_irq && plgState->get_exit_interrupt(dma.dma_irq)) {
                plgState->set_exit_interrupt(dma.dma_irq, -1);
                //plgState->set_exit_interrupt(dma.peri_irq, -1);
                //getInfoStream() << "EXIT Interrupt IRQ" << dma.dma_irq << " " << dma.peri_irq << " exit_inter = " << plgState->get_exit_interrupt(dma.peri_irq) << " " << plgState->get_exit_interrupt(dma.dma_irq)
                //                << "\n";
            }
        }
        plgState->set_exit_interrupt(irq_no, -1);
        // fuzzing mode, if exit irq, check out if the rx is still empty
        if (enable_fuzzing) {
            UpdateGraph(g_s2e_state, Rx, 0);
        }
    }
}

// void NLPPeripheralModel::onInvalidStatesDetection(S2EExecutionState *state, uint32_t pc, InvalidStatesType type,
//                                                       uint64_t tb_num) {
// UpdateFlag();
//}

void NLPPeripheralModel::onEnableReceive(S2EExecutionState *state, uint32_t pc, uint64_t tb_num) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    // Write a value to DR
    if (!enable_fuzzing && !plgState->check_instruction()) {
        getWarningsStream() << " write init dr value 0x2D! phaddr =  \n";

        for (auto phaddr : data_register) {
	    //if (phaddr == 0x4001300c || phaddr == 0x4000380c || phaddr == 0x40011004) continue;
	    //if (phaddr == 0x4001300c || phaddr == 0x4000380c) continue;
            std::queue<uint8_t> tmp;
	    tmp.push(0x2D);
		    /*tmp.push(0x31);
tmp.push(0x39);
tmp.push(0x39);
tmp.push(0x35);
tmp.push(0xa);*/
		getInfoStream() << hexval(phaddr)<<" \n";
            plgState->hardware_write_to_receive_buffer(phaddr, tmp, tmp.size());
        }
	//needed to change gpio flag
        UpdateFlag(0);
    }
    UpdateGraph(g_s2e_state, Unknown, 0);
}

void NLPPeripheralModel::UpdateFlag(uint32_t phaddr) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, g_s2e_state);
    RegMap state_map = plgState->get_state_map();
    if (rw_count > 1) {
        FlagList _allFlags;
        if (phaddr <= 1) {
            _allFlags = allFlags;
            getInfoStream() << "Update ALL Flag size: " << allFlags.size() << "\n";
        } else {
            for (auto loc : Flags_range) {
                auto range = loc.first;
                auto flags = loc.second;
                if (range.find(phaddr) != range.end()) {
                    _allFlags = flags;
                    break;
                }
            }
        }

        for (auto c : _allFlags) {
            if (c.a.type == "S" && phaddr == 0) {
                statistics[c.id] += 1;
                timer = true;
                set_reg_value(g_s2e_state, state_map, c.a, c.value[0]);
                getDebugStream() << "Specific Flag" << state_map[c.a.phaddr].cur_value << " bits " << c.a.bits[0]
                                 << "\n";
            } else if (c.a.type == "S" && phaddr == 1) {
                statistics[c.id] += 1;
                timer = false;
                set_reg_value(g_s2e_state, state_map, c.a, 0);
                getDebugStream() << "Flip Specific Flag" << state_map[c.a.phaddr].cur_value << " bits " << c.a.bits[0]
                                 << "\n";
            } else if (c.a.type == "O" && phaddr > 1) {
                getDebugStream() << "old Flag" << state_map[c.a.phaddr].cur_value << " bits " << c.a.bits[0]
                                 << "\n";
                int tmp = 0;
                //tmp = c.value[std::rand() % c.value.size()];
                if (c.value.size() > 1 && c.value[1] > 0xf) {
                    tmp = rand() % 0xffffffff;
                    getInfoStream() << c.value.size() << "mutiple bits values!!" << c.value[1] << "\n";
                } else {
                    tmp = c.value[std::rand() % c.value.size()];
                }
                auto old_value = state_map[c.a.phaddr].cur_value;
                set_reg_value(g_s2e_state, state_map, c.a, tmp);
                if (state_map[c.a.phaddr].cur_value == old_value)
                    continue;
                statistics[c.id] += 1;
                // set_reg_value(state_map, c.a, c.value);
                //getWarningsStream() <<_idx<< " Flag " << hexval(c.a.phaddr) <<" bit "<<c.a.bits[0]<< " value " << tmp << " size " << c.value.size()
                //                 << " " << std::rand() << "\n";
                getDebugStream() << "Flag " << hexval(c.a.phaddr) << " value " << c.value[0] << "\n";
            } else if (c.a.type == "F") {
                auto old_value = get_reg_value(g_s2e_state, state_map, c.a);
                uint32_t tmp = 0;
                tmp = (old_value << 1) + 1;
                getDebugStream() << "count value old  " << old_value << " new " << tmp << "\n";
                if (tmp > c.value[0] || tmp == old_value)
                    tmp = 0;
                statistics[c.id] += 1;
                //if (c.freq > 0) {
                //    tmp = (old_value << 1) + 1;
                //    if (tmp > c.value[0])
                //        tmp = 0;
                //} else {
                //    tmp = old_value >> 1;
                //    if (tmp < c.value[0])
                //        tmp = c.value[0];
                //}
                set_reg_value(g_s2e_state, state_map, c.a, tmp);
            }

            plgState->insert_reg_map(c.a.phaddr, state_map[c.a.phaddr]);
            //auto tmp = plgState->get_state_map();
            //getDebugStream() << "new Flag" << tmp[c.a.phaddr].cur_value << "\n";
        }
    }
}

/*void NLPPeripheralModel::onForceIRQCheck(S2EExecutionState *state, uint32_t pc, uint64_t re_tb_num) {*/
// DECLARE_PLUGINSTATE(NLPPeripheralModelState, g_s2e_state);
// getDebugStream() << "Force IRQ Check "<< hexval(re_tb_num) << "\n";
// for (auto phaddr: data_register) {
// if (disable_init_dr_value_flag[phaddr] != 1) {
// getWarningsStream() << " write init dr value 0x2D! phaddr = "<< hexval(phaddr) << "\n";
// plgState->hardware_write_to_receive_buffer(phaddr, 0x2D, 4);
//}
//}
// UpdateGraph(state, Write, 0);
/*}*/

bool NLPPeripheralModel::ExistInMMIO(uint32_t tmp) {
    bool check = false;
    for (auto nlpph : nlp_mmio) {
        if (tmp >= nlpph.first && tmp <= nlpph.second) {
            check = true;
            break;
        }
    }
    return check;
}

bool NLPPeripheralModel::readNLPModelfromFile(S2EExecutionState *state, std::string fileName) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    std::ifstream fNLP;
    std::string line;
    fNLP.open(fileName, std::ios::in);
    if (!fNLP) {
        getWarningsStream() << "Could not open cache nlp file: " << fileName << "\n";
        exit(-1);
        return false;
    }

    std::string peripheralcache;
    uint32_t start = 0x40000000, SR = 0;
    std::vector<uint32_t> curDR;
    while (getline(fNLP, peripheralcache)) {
        if (peripheralcache == "==")
            break;
        PeripheralReg reg;
        if (getMemo(peripheralcache, reg)) {
            if ((reg.type == "R" || reg.type == "T") && data_register.find(reg.phaddr) == data_register.end()) {
                data_register.insert(reg.phaddr);
                disable_init_dr_value_flag[reg.phaddr] = 0;
                curDR.push_back(reg.phaddr);
            } else if (reg.type == "L") {
                RXdescriptor = reg.phaddr;
            }
            if (start == 0x40000000)
                start = reg.phaddr;

            getDebugStream() << "current start:" << hexval(start) << " " << hexval(reg.phaddr) << "\n";
            if (reg.phaddr >= start + 0x100 || reg.phaddr <= start - 0x100) {
                for (auto dr : curDR) {
                    if (std::abs(int(SR - dr)) <= 0x100)
                        DR2SR[dr] = SR;
                    getDebugStream() << "DR2SR " << hexval(dr) << " : " << hexval(SR) << " " << hexval(DR2SR[dr]) << "\n";
                }
                SR = 0;
                start = reg.phaddr;
                curDR.clear();
                getDebugStream() << "update start address"
                                 << "\n";
            }
            plgState->insert_reg_map(reg.phaddr, reg);
        } else {
            return false;
        }
        if (reg.type == "S") {
            SR = reg.phaddr;
        }
    }

    TAMap _allTAs;
    int _idx = 0;
    std::set<uint32_t> addrs;
    while (getline(fNLP, peripheralcache)) {
        if (peripheralcache == "==")
            break;
        if (peripheralcache == "--") {
            TA_range[addrs] = _allTAs;
            _allTAs.clear();
            addrs.clear();
            continue;
        }
        EquList trigger;
        EquList action;
        if (getTApairs(peripheralcache, trigger, action)) {
            trigger[0].id = ++_idx;
            _allTAs.push_back(std::make_pair(trigger, action));
            for (auto equ : trigger) {
                if (equ.eq == "*")
                    continue;
                addrs.insert(equ.a1.phaddr);
            }
            for (auto equ : action) {
                addrs.insert(equ.a1.phaddr);
            }
            allTAs.push_back(_allTAs.back());
            statistics[_idx] = 0;
        } else {
            return false;
        }
    }

    ta_numbers = _idx;
    FlagList _allFlags;
    addrs.clear();
    while (getline(fNLP, peripheralcache)) {
        if (peripheralcache == "==")
            break;
        if (peripheralcache == "--") {
            Flags_range.push_back(std::make_pair(addrs, _allFlags));
            _allFlags.clear();
            addrs.clear();
            continue;
        }
        Flag count;
        if (extractFlag(peripheralcache, count)) {
            count.id = ++_idx;
            _allFlags.push_back(count);
            addrs.insert(count.a.phaddr);
            if (count.a.type == "S")
                allFlags.push_back(count);
            statistics[_idx] = 0;
        } else {
            return false;
        }
    }
    flags_numbers = _idx - ta_numbers;

    //TODO DMA FIELD STRUCTURE
    while (getline(fNLP, peripheralcache)) {
        if (peripheralcache == "==")
            break;
        DMA dma;
        if (extractDMA(peripheralcache, dma)) {
            all_dmas.push_back(dma);
        }
    }

    while (getline(fNLP, peripheralcache)) {
        if (peripheralcache == "==")
            break;
        Field field;
        if (!extractConstraints(peripheralcache, field)) {
            return false;
        }
    }

    return true;
}

void NLPPeripheralModel::SplitString(const std::string &s, std::vector<std::string> &v, const std::string &c) {
    std::string::size_type pos1, pos2;
    getDebugStream() << s <<" "<<s.empty()<< "\n";
    pos2 = s.find(c);
    pos1 = 0;
    while (std::string::npos != pos2) {
        v.push_back(s.substr(pos1, pos2 - pos1));
        pos1 = pos2 + c.size();
        pos2 = s.find(c, pos1);
    }
    if (pos1 != s.length())
        v.push_back(s.substr(pos1));
}

void NLPPeripheralModel::SplitStringToInt(const std::string &s, std::vector<long> &v, const std::string &c, int dtype) {
    std::string::size_type pos1, pos2;
    getDebugStream() << s << "\n";
    pos2 = s.find(c);
    pos1 = 0;
    while (std::string::npos != pos2) {
        v.push_back(std::strtol(s.substr(pos1, pos2 - pos1).c_str(), NULL, dtype));
        getDebugStream() << s.substr(pos1, pos2 - pos1) << " "
                         << std::strtol(s.substr(pos1, pos2 - pos1).c_str(), NULL, dtype) << "\n";
        pos1 = pos2 + c.size();
        pos2 = s.find(c, pos1);
    }
    if (pos1 != s.length()) {
        v.push_back(std::strtol(s.substr(pos1).c_str(), NULL, dtype));
        getDebugStream() << s.substr(pos1) << " " << std::strtol(s.substr(pos1).c_str(), NULL, dtype) << "\n";
    }
}

bool NLPPeripheralModel::getMemo(std::string peripheralcache, PeripheralReg &reg) {
    boost::smatch what;
    getDebugStream() << peripheralcache << "\n";
    if (!boost::regex_match(peripheralcache, what, MemoRegEx)) {
        getWarningsStream() << "getMemo match false\n";
        exit(0);
        return false;
    }

    if (what.size() != 2) {
        getWarningsStream() << "getMemo wrong size = " << what.size() << "\n";
        exit(0);
        return false;
    }

    std::vector<std::string> v;
    SplitString(what[1], v, "_");
    reg.type = v[0];
    reg.phaddr = std::stoull(v[1].c_str(), NULL, 16);
    reg.reset = std::stoull(v[2].c_str(), NULL, 16);
    reg.cur_value = reg.reset;
    if (v.size() == 4)
        reg.width = std::stoull(v[3].c_str(), NULL, 10);
    reg.t_size = 0;
    reg.r_size = 0;
    reg.t_value = 0;
    //reg.r_value = new std::queue<uint8_t>();
    getDebugStream() << "type = " << reg.type << " phaddr = " << reg.phaddr << " reset value = " << reg.reset << "\n";
    return true;
}

bool NLPPeripheralModel::getTApairs(std::string peripheralcache, EquList &trigger, EquList &action) {
    std::vector<std::string> v;
    SplitString(peripheralcache, v, ":");
    std::string trigger_str = v[0];
    std::string action_str = v[1];
    bool trigger_rel = true, action_rel = true;
    if (trigger_str.find('|', 0) != std::string::npos) {
        trigger_rel = false;
    }

    if (action_str.find('|', 0) != std::string::npos) {
        action_rel = false;
    }
    getDebugStream() << " trigger = " << trigger_str << " action = " << action_str << "\n";

    bool res = extractEqu(trigger_str, trigger, trigger_rel) && extractEqu(action_str, action, action_rel);
    //if (trigger.back().a1.type == "R" && trigger.back().eq != "=") {
    //rx_flags[action.back().a1.phaddr] = action.back().a1.bits[0];
    //}
    if (v.size() == 3) {
        action.back().interrupt = std::stoi(v[2].c_str(), NULL, 10);
        getDebugStream() << " trigger = " << trigger_str << " action = " << action_str
                         << " interrupt = " << action.back().interrupt << "\n";
    }
    return res;
}

bool NLPPeripheralModel::extractEqu(std::string peripheralcache, EquList &vec, bool rel) {
    boost::smatch what;
    getDebugStream() << peripheralcache << "\n";

    while (boost::regex_search(peripheralcache, what, TARegEx)) {
        std::string equ_str = what[0];
        std::vector<std::string> v;
        getDebugStream() << equ_str << "\n";
        SplitString(equ_str, v, ",");
        Equation equ;
        equ.rel = rel;
        equ.interrupt = -1;
        if (v[0] == "*") {
            equ.a1.type = v[0];
            equ.a1.bits = {-1};
            equ.a2.bits = {-1};
            equ.eq = "*";
            equ.type_a2 = "*";
        } else {
            equ.a1.type = v[0];
            equ.a1.phaddr = std::stoull(v[1].c_str(), NULL, 16);
            if (v[2] == "*")
                equ.a1.bits = {-1};
            else {
                SplitStringToInt(v[2], equ.a1.bits, "/", 10);
            }
            equ.eq = v[3];
            if (v[4] == "O" || v[4] == "C") {
                equ.type_a2 = "F";
                equ.value = 0;
                equ.a2.type = v[4];
                equ.a2.phaddr = std::stoull(v[5].c_str(), NULL, 16);
                if (v[6] == "*")
                    equ.a2.bits = {-1};
                else {
                    SplitStringToInt(v[6], equ.a2.bits, "/", 10);
                }
            } else if (v[4][0] != '*') {
                equ.type_a2 = "V";
                equ.value = std::stoull(v[4].c_str(), NULL, 2);
            } else {
                equ.value = 0;
                if (v[4] == "*") {
                    equ.type_a2 = v[4];
                } else {
                    equ.type_a2 = v[4][1];
                    equ.a2.type = v[4][1];
                    equ.a2.phaddr = std::stoull(v[4].substr(2, v[4].size() - 2).c_str(), NULL, 16);
                }
            }
        }
        getDebugStream() << "equ type = " << equ.a1.type << " equ phaddr = " << equ.a1.phaddr
                         << " equ bits = " << equ.a1.bits[0] << " equ = " << equ.eq << " type_a2 = " << equ.type_a2
                         << " value = " << equ.value << "\n";
        vec.push_back(equ);
        peripheralcache = what.suffix();
    }
    return true;
}

bool NLPPeripheralModel::extractConstraints(std::string peripheralcache, Field &field) {
    boost::smatch what;
    getDebugStream() << peripheralcache << "\n";
    if (!boost::regex_match(peripheralcache, what, FlagEx)) {
        getWarningsStream() << "extractFlag match false" << peripheralcache << "\n";
        exit(0);
        return false;
    }
    if (what.size() != 2) {
        getWarningsStream() << "extractFlag wrong size = " << what.size() << "\n";
        exit(0);
        return false;
    }
    std::vector<std::string> v;
    SplitString(what[1], v, ",");
    field.type = v[0];
    field.phaddr = std::stoull(v[1].c_str(), NULL, 16);
    if (v[2] == "*")
        field.bits = {-1};
    else {
        SplitStringToInt(v[2], field.bits, "/", 10);
    }
    if (constraints.find(field.phaddr) == constraints.end())
        constraints[field.phaddr] = {};
    constraints[field.phaddr].push_back(field);
    return true;
}

bool NLPPeripheralModel::extractFlag(std::string peripheralcache, Flag &flag) {
    boost::smatch what;
    getDebugStream() << peripheralcache << "\n";
    if (!boost::regex_match(peripheralcache, what, FlagEx)) {
        getWarningsStream() << "extractFlag match false" << peripheralcache << "\n";
        exit(0);
        return false;
    }
    if (what.size() != 2) {
        getWarningsStream() << "extractFlag wrong size = " << what.size() << "\n";
        exit(0);
        return false;
    }
    std::vector<std::string> v;
    SplitString(what[1], v, ",");
    flag.a.type = v[0];
    flag.a.phaddr = std::stoull(v[1].c_str(), NULL, 16);
    if (v[2] == "*")
        flag.a.bits = {-1};
    else {
        SplitStringToInt(v[2], flag.a.bits, "/", 10);
    }
    flag.freq = std::stoull(v[3].c_str(), NULL, 10);
    // flag.value = std::stoi(v[4].c_str(), NULL, 16);
    SplitStringToInt(v[4], flag.value, "/", 16);
    getDebugStream() << "extractFlag  " << hexval(flag.a.phaddr) << " " << flag.a.bits[0] << "\n";
    return true;
}

bool NLPPeripheralModel::extractDMA(std::string peripheralcache, DMA &dma) {
    getDebugStream() << peripheralcache << "\n";
    std::vector<std::string> v;
    SplitString(peripheralcache, v, ";");
    dma.dma_irq = std::stoull(v[0].c_str(), NULL, 10);
    dma.state = 0;

    std::vector<std::string> twomemory;
    SplitString(v[1], twomemory, "|");
    std::vector<std::string> field;
    SplitString(twomemory[0], field, ",");
    Field memory;
    memory.phaddr = std::stoull(field[1].c_str(), NULL, 16);
    memory.type = field[0];
    memory.bits = {-1};
    dma.memory_addr = memory;

    field.clear();
    SplitString(v[2], field, ",");
    Field peri;
    peri.phaddr = std::stoull(field[1].c_str(), NULL, 16);
    peri.type = field[0];
    peri.bits = {-1};
    dma.peri_dr = peri;

    field.clear();
    SplitString(v[4], field, ",");
    Field htif;
    htif.phaddr = std::stoull(field[1].c_str(), NULL, 16);
    htif.type = field[0];
    SplitStringToInt(field[2], htif.bits, "/", 10);
    dma.HTIF = htif;

    field.clear();
    SplitString(v[5], field, ",");
    Field tcif;
    tcif.phaddr = std::stoull(field[1].c_str(), NULL, 16);
    tcif.type = field[0];
    SplitStringToInt(field[2], tcif.bits, "/", 10);
    dma.TCIF = tcif;

    field.clear();
    Field gif;
    gif.type = "N/A";
    SplitString(v[6], field, ",");
    if (!field.empty()) {
        gif.phaddr = std::stoull(field[1].c_str(), NULL, 16);
        gif.type = field[0];
        SplitStringToInt(field[2], gif.bits, "/", 10);
    } 
    dma.GIF = gif;
    return true;
}

bool NLPPeripheralModel::compare(uint32_t a1, std::string sym, uint32_t a2) {
    // 1:= ; 2:>; 3:<; 4:>=; 5:<=
    if (sym == "*")
        return false;
    if (sym == "=")
        return a1 == a2;
    if (sym == ">")
        return a1 > a2;
    if (sym == "<")
        return a1 < a2;
    if (sym == ">=")
        return a1 >= a2;
    if (sym == "<=")
        return a1 <= a2;
    return false;
}

bool NLPPeripheralModel::EmitIRQ(S2EExecutionState *state, int irq) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    bool irq_triggered = false;
    onExternalInterruptEvent.emit(state, irq, &irq_triggered);
    if (irq_triggered) {
        getInfoStream() << "SUCCESS! emit irq: " << irq << "\n";
        plgState->inc_irq_freq(irq);
        plgState->set_exit_interrupt(irq, true);
    }
    getInfoStream() << "emit irq DATA IRQ Action trigger interrupt freq = " << plgState->get_irq_freq(irq) << " exit_interrupt = " << plgState->get_exit_interrupt(irq) << " irq = " << irq << "\n";
    return irq_triggered;
}

void NLPPeripheralModel::UpdateGraph(S2EExecutionState *state, RWType type, uint32_t phaddr) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    RegMap state_map = plgState->get_state_map();
    TAMap _allTAs;
    if (phaddr == 0) {
        _allTAs = allTAs;
        getInfoStream() << "Update ALL Graph size: " << _allTAs.size() << "\n";
    } else {
        for (auto loc : TA_range) {
            auto range = loc.first;
            auto TA = loc.second;
            if (range.find(phaddr) != range.end()) {
                _allTAs = TA;
                break;
            }
        }
    }
    std::map<std::pair<uint32_t, int>, uint32_t> prev_action;
    std::vector<uint32_t> irqs;
    std::vector<std::pair<uint32_t, uint32_t>> dmas;
    std::map<uint32_t, std::set<uint32_t>> missed_enabled;
    for (auto ta : _allTAs) {
        EquList trigger = ta.first;
        bool rel;
        std::vector<bool> trigger_res;
        for (auto equ : trigger) {
            rel = equ.rel;

            if (equ.a1.type == "*") {
                trigger_res.push_back(true);
            } else if (equ.type_a2 == "*" && equ.a1.type == "R") {
                if (type == Read && data_register.find(phaddr) != data_register.end()) {
                    trigger_res.push_back(true);
                } else
                    trigger_res.push_back(false);
            } else if (equ.type_a2 == "*" && equ.a1.type == "T") {
                if (type == Write && data_register.find(phaddr) != data_register.end()) {
                    trigger_res.push_back(true);
                } else
                    trigger_res.push_back(false);
            } else if (equ.a1.type == "F" && (type != Write || phaddr != equ.a1.phaddr)) {
                trigger_res.push_back(false);
            } else if (equ.a1.type == "K") {
                trigger_res.push_back((type != Read) & (phaddr != equ.a1.phaddr));
            } else {
                uint32_t a1, a2;
                a1 = get_reg_value(state, state_map, equ.a1);
                if (equ.type_a2 == "T") {
                    a2 = state_map[equ.a2.phaddr].t_size;
                } else if (equ.type_a2 == "R") {
                    a2 = state_map[equ.a2.phaddr].r_size;
                } else if (equ.type_a2 == "F") {
                    a2 = get_reg_value(state, state_map, equ.a2);
                } else if (equ.type_a2 == "V") {
                    a2 = equ.value;
                } else {
                    a2 = 0;
                    getDebugStream() << "ERROR " << a1 << " eq " << equ.eq << " \n";
                }
                if (equ.a1.type == "F") {
                    if (type == Write && a1 == a2 && phaddr == equ.a1.phaddr) {
                        getDebugStream() << "Write 1 to " << hexval(equ.a1.phaddr) << " eq " << equ.eq << " \n";
                        trigger_res.push_back(true);
                    } else
                        trigger_res.push_back(false);

                } else {
                    getDebugStream() << "intermediate trigger a1 " << hexval(equ.a1.phaddr)
                                     << " bit: " << equ.a1.bits[0] << " a1 " << a1 << " eq " << equ.eq << " a2 "
                                     << equ.value << " \n";

                    trigger_res.push_back(compare(a1, equ.eq, a2));
                }
            }
        }
        bool check = rel;
        if (rel == true) {
            for (bool idx : trigger_res) {
                // getDebugStream() << "idx = " << idx <<"\n";
                if (idx == false) {
                    check = false;
                    break;
                }
            }
        } else {
            for (auto idx : trigger_res) {
                if (idx == true) {
                    check = true;
                    break;
                }
            }
        }
        if (!check)
            continue;
        uint32_t _idx = trigger[0].id;
        statistics[_idx] += 1;
        if (!enable_fuzzing) {
            for (auto equ : trigger) {
                auto _tmp = std::make_pair(equ.a1.phaddr, equ.a1.bits[0]);
                if (prev_action.find(_tmp) != prev_action.end()) {
                    chain_freq[{prev_action[_tmp], _idx}] += 1;
                    getDebugStream() << "chain a1 " << prev_action[_tmp] << " a2 " << _idx << " \n";
                }
                getDebugStream() << "trigger a1 " << hexval(equ.a1.phaddr) << " bit: " << equ.a1.bits[0] << " eq " << equ.eq
                                 << " a2 " << equ.value << "statistics:" << _idx << " " << statistics[_idx] << " \n";
            }
        }

        EquList action = ta.second;
        for (auto equ : action) {
            uint32_t a2;
            if (equ.type_a2 == "T") {
                a2 = state_map[equ.a2.phaddr].t_size;
            } else if (equ.type_a2 == "R") {
                a2 = state_map[equ.a2.phaddr].r_size;
            } else if (equ.type_a2 == "F") {
                a2 = get_reg_value(state, state_map, equ.a2);
                getDebugStream() << "get by address, phaddr" << hexval(equ.a2.phaddr) << " bits " << equ.a2.bits[0] << " " << a2 << "\n";
            } else {
                a2 = equ.value;
            }

            if (!enable_fuzzing) {
                auto _tmp = std::make_pair(equ.a1.phaddr, equ.a1.bits[0]);
                prev_action[_tmp] = _idx;
            }

            if (equ.a1.type == "R") {
                getDebugStream() << "set receive value : phaddr =  " << hexval(equ.a1.phaddr)
                                 << " updated bit = " << equ.a1.bits[0]
                                 << " value = " << hexval(state_map[equ.a1.phaddr].cur_value) << " a2 = " << a2
                                 << "\n";
                state_map[equ.a1.phaddr].r_size = a2;
            } else if (equ.a1.type == "T") {
                getDebugStream() << "set transmit value : phaddr =  " << hexval(equ.a1.phaddr)
                                 << " updated bit = " << equ.a1.bits[0]
                                 << " value = " << hexval(state_map[equ.a1.phaddr].cur_value) << " a2 = " << a2
                                 << "\n";
                state_map[equ.a1.phaddr].t_size = a2;
            } else {
                set_reg_value(state, state_map, equ.a1, a2);
                if (type == Read) {
                    getDebugStream() << "Read Action: phaddr =  " << hexval(equ.a1.phaddr)
                                     << " updated bit = " << equ.a1.bits[0]
                                     << " value = " << hexval(state_map[equ.a1.phaddr].cur_value) << " a2 = " << a2
                                     << "\n";
                } else {
                    getDebugStream() << "Write Action: phaddr =  " << hexval(equ.a1.phaddr)
                                     << " updated bit = " << equ.a1.bits[0]
                                     << " value = " << hexval(state_map[equ.a1.phaddr].cur_value) << " a2 = " << a2
                                     << "\n";
                }
            }
            plgState->insert_reg_map(equ.a1.phaddr, state_map[equ.a1.phaddr]);
            getDebugStream() << "equ.interrupt = " << equ.interrupt
                             << " exit_inter = " << plgState->get_exit_interrupt(equ.interrupt) << "\n";

            if (equ.interrupt == -1)
                continue;
            //skip non rx interrupt
            //if (type == Rx && (rx_flags.find(equ.a1.phaddr) == rx_flags.end() || rx_flags[equ.a1.phaddr] != equ.a1.bits[0])) {
            //     getInfoStream() << "skip irq because non rx interrupt " << hexval(equ.a1.phaddr) << " bit " << equ.a1.bits[0] << "\n";
            //     continue;
            // }

            //no fuzzing mode, skip if the irq is triggered by writing to rx & interrupt_freq is more than once
            //if (!enable_fuzzing) {
            //if (plgState->get_irq_freq(equ.interrupt) > 2) {
            //getWarningsStream() << " 0 DATA IRQ Action trigger interrupt equ.interrupt = " << plgState->get_irq_freq(equ.interrupt) << "\n";
            //continue;
            //}
            //}
            //} else {
            /*if (plgState->get_irq_freq(equ.interrupt) > 10) {*/
            //getInfoStream() << " only trigger at most ten times DATA IRQ Action interrupt in fuzzing mode equ.interrupt = " << equ.interrupt << "\n";
            //continue;
            /*}*/
            //}
            //no fuzzing mode, skip if the irq is triggered by the phaddr that is in nlp_mmio
            if (!enable_fuzzing) {
                uint32_t tmp = equ.a1.phaddr;
                if (!ExistInMMIO(tmp))
                    continue;
            }

            //all mode, if the irq is not triggered, emit the irq. if successful emited, record the irq and wait for exiting
            if (!plgState->get_exit_interrupt(equ.interrupt)) {
                //interrupt_freq[equ.interrupt] += 1;
                //getWarningsStream() << "IRQ Action trigger interrupt equ.interrupt = " << equ.interrupt << "\n";

                if (enable_fuzzing) {
                    if (!begin_irq_flag) {
                        return;
                    }
                }
                if (!enable_fuzzing) {
                    getInfoStream() << "add irqs " << equ.interrupt << " " << hexval(equ.a1.phaddr) << " bit " << equ.a1.bits[0] << "\n";
                    for (auto tequ : trigger) {
                        if (tequ.a1.phaddr != equ.a1.phaddr)
                            missed_enabled[equ.interrupt].insert(tequ.a1.phaddr);
                    }
                }
                //DMA request
                if (equ.a1.type == "D") {
                    getInfoStream() << "add dma " << equ.interrupt << " " << all_dmas.size() << "\n";
                    dmas.push_back({equ.a1.phaddr, equ.interrupt});
                } else
                    irqs.push_back(equ.interrupt);
            }
            //else if (enable_fuzzing) {
            //    plgState->set_exit_interrupt(equ.interrupt, true);
            //}
        }
    }

    if (irqs.size() > 1) {
        std::shuffle(std::begin(irqs), std::end(irqs), std::default_random_engine());
    }
    for (auto interrupt : irqs) {
        if (plgState->get_exit_interrupt(interrupt)) continue;
        if (!EmitIRQ(state, interrupt)) {
            untriggered_irq[interrupt] = missed_enabled[interrupt];
        }
    }
    for (auto enabled : dmas) {
        for (int i = 0; i < all_dmas.size(); ++i) {
            if (plgState->get_exit_interrupt(enabled.second)) continue;
            getInfoStream() << "DMA irq:" << enabled.second << " " << all_dmas[i].dma_irq << " " << all_dmas[i].state << "\n";
            if (enabled.second == all_dmas[i].dma_irq && all_dmas[i].state == 0) {
                getInfoStream() << "SET DMA " << all_dmas[i].dma_irq << " " << enabled.second << "\n";
                all_dmas[i].state = 1;
                if (!EmitDMA(state, enabled.second))
                    all_dmas[i].state = 0;
            }
        }
    }
}

void NLPPeripheralModel::onStatistics() {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, g_s2e_state);
    getInfoStream() << "write NLP files\n";
    std::string NLPstafileName = s2e()->getOutputDirectory() + "/" + "NLPStatistics.dat";
    std::ofstream fPHNLP;

    fPHNLP.open(NLPstafileName, std::ios::out | std::ios::trunc);

    fPHNLP << "nlp write: " << write_numbers << " nlp read: " << read_numbers << " ta_num: " << ta_numbers << "\n";

    uint32_t sum_ta = 0, sum_flag = 0, unique_ta = 0, unique_flag = 0, uncertain_flag = 0, unique_uncertain_flag = 0;
    uint32_t c1 = 0, c2 = 0, c3 = 0, a1 = 0, a2 = 0, a3 = 0;
    for (auto loc : TA_range) {
        for (auto ta : loc.second) {
            int idx = ta.first[0].id;
            fPHNLP << "TA : " << idx << " used ? " << (statistics[idx] != 0) << " ";
            for (auto equ : ta.first) {
                fPHNLP << " " << equ.a1.type << " " << hexval(equ.a1.phaddr) << " " << equ.a1.bits[0] << " " << equ.eq << " ";
            }
            fPHNLP << " " << ta.second[0].a1.type << " " << hexval(ta.second[0].a1.phaddr) << " " << ta.second[0].a1.bits[0] << "\n";
            if (statistics[idx] != 0) {
                if (ta.second.back().interrupt != -1) {
                    c3 += 1;
                    if (ta.second.back().a1.type == "D")
                        a3 += 1;
                    else
                        a2 += 1;
                } else {
                    if (ta.first[0].a1.type == "R" || ta.first[0].a1.type == "T")
                        c1 += 1;
                    else
                        c2 += 1;
                    a1 += 1;
                }
            }
        }
    }
    int _tmp1 = 0, _tmp2 = 0;
    for (auto ta : statistics) {
        if (ta.first <= ta_numbers) {
            sum_ta += ta.second;
            unique_ta += ta.second > 0;
            fPHNLP << "TA id: " << ta.first << " cnt: " << ta.second << "\n";
        } else {
            if (_tmp2 >= Flags_range[_tmp1].second.size()) {
                _tmp1++;
                _tmp2 = 0;
            }
            auto tmp = Flags_range[_tmp1].second[_tmp2].a.phaddr;
            bool uncertain = Flags_range[_tmp1].second[_tmp2].value.size() > 1;
            if (uncertain) {
                unique_uncertain_flag += ta.second > 0;
                uncertain_flag += ta.second;
            }
            if (ta.second != 0) {
                c1 += 1;
                a1 += 1;
            }
            sum_flag += ta.second;
            unique_flag += ta.second > 0;
            fPHNLP << "Flag uncertain? " << uncertain << " id: " << ta.first << " reg: " << hexval(tmp) << " bit: " << Flags_range[_tmp1].second[_tmp2].a.bits[0] << " cnt: " << ta.second << "\n";
            _tmp2++;
        }
    }
    for (auto write : write_action) {
        fPHNLP << "Write action " << hexval(write.first) << " times " << write.second << "\n";
        c2 += 1;
        a1 += 1;
    }
    auto interrupt_freq = plgState->get_irqs_freq();
    for (auto interrupt : interrupt_freq) {
        fPHNLP << "interrupt id: " << interrupt.first << " freq: " << interrupt.second << "\n";
    }
    int chain_num = 0;
    for (auto chain : chain_freq) {
        fPHNLP << "chain id1: " << chain.first.first << " id2: " << chain.first.second << " freq: " << chain.second << "\n";
        chain_num += chain.second;
    }
    fPHNLP << "Total TA rules: " << c1 << " " << c2 << " " << c3 << " " << a1 << " " << a2 << " " << a3 << "\n";
    fPHNLP << "Total ca: " << unique_ta + unique_flag << " unique chain " << chain_freq.size() << "\n";
    fPHNLP << "ta: " << sum_ta << "\\" << unique_ta << " flag: " << sum_flag << "\\" << unique_flag << " uncertain flag: " << uncertain_flag << "\\" << unique_uncertain_flag << " chain num: " << chain_num << "\n";
    fPHNLP << "-------Verification Results-------\n";
    for (auto irq : unenabled_flag) {
        fPHNLP << "type one unenabled_flag: " << irq.first;
        for (auto idx : irq.second) {
            fPHNLP << " ; " << hexval(idx);
        }
        fPHNLP << "\n";
    }
    for (auto irq : untriggered_irq) {
        if (interrupt_freq.find(irq.first) != interrupt_freq.end() && interrupt_freq[irq.first] != 0) continue;
        fPHNLP << "type two untriggered_irq: " << irq.first;
        for (auto idx : irq.second) {
            fPHNLP << " ; " << hexval(idx);
        }
        fPHNLP << "\n";
    }
    for (auto phaddr : read_unauthorized_freq) {
        if (DR2SR[phaddr.first] == 0)
            continue;
        fPHNLP << "type three read unauthorized_freq: " << hexval(phaddr.first) << " corresponding SR: " << hexval(DR2SR[phaddr.first]) << " at pc: ";
        for (auto pc : phaddr.second) {
            fPHNLP << " ; " << hexval(pc);
        }
        fPHNLP << "\n";
    }

    for (auto phaddr : write_unauthorized_freq) {
        if (DR2SR[phaddr.first] == 0)
            continue;
        fPHNLP << "type four write unauthorized_freq: " << hexval(phaddr.first) << " corresponding SR: " << hexval(DR2SR[phaddr.first]) << " at pc: ";
        for (auto pc : phaddr.second) {
            fPHNLP << " ; " << hexval(pc);
        }
        fPHNLP << "\n";
    }

    for (auto read : read_access_freq) {
        fPHNLP << "type five read access_freq: " << hexval(read.first.first) << " corresponding bit: " << read.first.second << " at pc: ";
        for (auto pc : read.second) {
            fPHNLP << " ; " << hexval(pc);
        }
        fPHNLP << "\n";
    }

    for (auto write : write_access_freq) {
        fPHNLP << "type six write access_freq: " << hexval(write.first.first) << " corresponding bit: " << write.first.second << " at pc: ";
        for (auto pc : write.second) {
            fPHNLP << " ; " << hexval(pc);
        }
        fPHNLP << "\n";
    }
    fPHNLP.close();
}

std::pair<uint32_t, uint32_t> NLPPeripheralModel::AddressCorrection(S2EExecutionState *state, uint32_t phaddr) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    RegMap state_map = plgState->get_state_map();
    if (state_map.find(phaddr) != state_map.end())
        return {phaddr, 0};
    auto uppper_node = state_map.upper_bound(phaddr);
    // uint32_t new_phaddr = phaddr & 0xFFFFFFFC;
    if (uppper_node != state_map.begin())
        uppper_node--;
    uint32_t new_phaddr = uppper_node->first;
    uint32_t offset = (phaddr - new_phaddr) * 8;
    if (offset != 0)
        getInfoStream() << "correction " << hexval(phaddr) << " new correction " << hexval(new_phaddr) << " \n";
    return {new_phaddr, offset};
}

void NLPPeripheralModel::onPeripheralRead(S2EExecutionState *state, SymbolicHardwareAccessType type, uint32_t phaddr,
                                          unsigned size, uint32_t *NLPsymbolicvalue, bool *flag) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    rw_count++;
    if (rw_count == 1) {
        readNLPModelfromFile(state, NLPfileName);
        if (!enable_fuzzing) {
            // Write a value to DR
            getInfoStream() << " write init dr value 0x2D!  \n";
            for (auto _phaddr : data_register) {
	    //if (_phaddr == 0x4001300c || _phaddr == 0x4000380c || _phaddr == 0x40011004) continue;
	    //if (_phaddr == 0x4001300c || _phaddr == 0x4000380c) continue;
                std::queue<uint8_t> tmp;
            //for (int i = 0; i < 129; ++i) {
	    tmp.push(0x2D);
/*tmp.push(0x31);//}
tmp.push(0x39);
tmp.push(0x39);
tmp.push(0x35);
tmp.push(0xa);*/
		getInfoStream() << hexval(_phaddr)<<" \n";
		plgState->hardware_write_to_receive_buffer(_phaddr, tmp, tmp.size());
            }
            UpdateGraph(g_s2e_state, Unknown, 0);
        }
    }
    read_numbers += 1;
    UpdateFlag(phaddr);
    auto correction = AddressCorrection(state, phaddr);
    phaddr = correction.first;
    *flag = false;
    if (!enable_fuzzing && correction.second == 0 && constraints.find(phaddr) != constraints.end()) {
        int bit = -2;
        for (auto constraint : constraints[phaddr]) {
            if (constraint.type == "W" && constraint.bits[0] == -1) {
                bit = -1;
            } else if (constraint.type == "W" && size >= constraint.bits.back()) {
                bit = constraint.bits.back();
            }
            if (bit != -2) {
                if (read_access_freq.find({phaddr, bit}) == read_access_freq.end()) {
                    std::set<uint64_t> tmp;
                    read_access_freq[{phaddr, bit}] = tmp;
                }
                getInfoStream() << "unavailable read access to reg: " << hexval(phaddr)
                                << " pc = " << hexval(state->regs()->getPc()) << "\n";
                read_access_freq[{phaddr, bit}].insert(state->regs()->getPc());
                break;
            }
        }
    }

    RegMap state_map = plgState->get_state_map();
    // unauthorized access check
    if (data_register.find(phaddr) != data_register.end()) {
        if (!enable_fuzzing && ExistInMMIO(phaddr) && checked_SR == false) {
            getInfoStream() << "unauthorized READ access to data register: " << hexval(phaddr)
                            << "pc = " << hexval(state->regs()->getPc()) << "\n";
            if (read_unauthorized_freq.find(phaddr) == read_unauthorized_freq.end()) {
                std::set<uint64_t> tmp;
                read_unauthorized_freq[phaddr] = tmp;
            }
            read_unauthorized_freq[phaddr].insert(state->regs()->getPc());
        }
        *flag = true;
        disable_init_dr_value_flag[phaddr] = 1;
        size = state_map[phaddr].width / 8;
	//if (phaddr == 0x4001300c || phaddr == 0x4000380c || phaddr == 0x40011004) size = 1;
	//if (phaddr == 0x4001300c || phaddr == 0x4000380c) size = 1;
        std::vector<unsigned char> data;
        for (uint32_t i = 0; i < size; i++) {
            data.push_back(plgState->get_dr_value(phaddr, 1));
        }
        if (size == 4) {
            *NLPsymbolicvalue = data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        } else if (size == 2) {
            *NLPsymbolicvalue = data[0] | ((uint32_t)data[1] << 8);
        } else {
            *NLPsymbolicvalue = data[0];
        }
        //if (!begin_irq_flag) {
        plgState->receive_instruction(phaddr);
        //}
        getInfoStream() << "Read data register " << hexval(phaddr) << " width " << size
                         << " rxsize = " << (uint32_t)plgState->get_rx_size(phaddr) << " pc = " << hexval(state->regs()->getPc()) << " value " << hexval(*NLPsymbolicvalue) << " " << (uint32_t)data[0] << " " << (uint32_t)data[1] << " " << (uint32_t)data[2] << " " << (uint32_t)data[3] << "\n";
    } else {
        *NLPsymbolicvalue = plgState->get_ph_value(phaddr);
    }

    checked_SR = false;
    if (state_map[phaddr].type == "S") {
        checked_SR = true;
    }
    updatedFuzzingReg(state, phaddr);
    UpdateGraph(g_s2e_state, Read, phaddr);
    getDebugStream() << "correction " << hexval(phaddr) << " value " << *NLPsymbolicvalue << " \n";
    if (correction.second != 0) {
        *NLPsymbolicvalue = *NLPsymbolicvalue >> correction.second;
    }
    getDebugStream() << "Read phaddr " << hexval(phaddr) << " value " << *NLPsymbolicvalue << " \n";
}

void NLPPeripheralModel::onPeripheralWrite(S2EExecutionState *state, SymbolicHardwareAccessType type, uint32_t phaddr,
                                           uint32_t writeconcretevalue) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    rw_count++;
    if (rw_count == 1) {
        readNLPModelfromFile(state, NLPfileName);
        if (!enable_fuzzing) {
            // Write a value to DR
            getInfoStream() << " write init dr value 0x2D! \n";
            for (auto _phaddr : data_register) {
	    //if (_phaddr == 0x4001300c || _phaddr == 0x4000380c) continue;
	    //if (_phaddr == 0x4001300c || _phaddr == 0x4000380c || _phaddr == 0x40011004) continue;
                std::queue<uint8_t> tmp;
            //for (int i = 0; i < 129; ++i) {
	    tmp.push(0x2D);
	    /*
tmp.push(0x31);//}
tmp.push(0x39);
tmp.push(0x39);
tmp.push(0x35);
tmp.push(0xa);*/
		getInfoStream() << hexval(_phaddr)<<" \n";
		plgState->hardware_write_to_receive_buffer(_phaddr, tmp, tmp.size());
            }
            UpdateGraph(g_s2e_state, Unknown, 0);
        }
    }
    write_numbers += 1;
    auto correction = AddressCorrection(state, phaddr);
    phaddr = correction.first;
    if (correction.second != 0) {
        writeconcretevalue = writeconcretevalue << correction.second;
    }

    RegMap state_map = plgState->get_state_map();
    if (!enable_fuzzing && correction.second == 0 && constraints.find(phaddr) != constraints.end()) {
        int bit = -2;
        uint32_t diff = 0;
        for (auto constraint : constraints[phaddr]) {
            if (constraint.type == "R" && constraint.bits[0] == -1 && writeconcretevalue != 0) {
                bit = -1;
            } else if (constraint.type == "R") {
                diff = state_map[phaddr].cur_value ^ writeconcretevalue;
                for (int i = 0; i < 32; ++i) {
                    if (((diff >> i) & 1)) {
                        if (std::find(constraint.bits.begin(), constraint.bits.end(), i) != constraint.bits.end()) {
                            bit = i;
                            break;
                        }
                    }
                }
            }
            if (bit != -2) {
                if (write_access_freq.find({phaddr, bit}) == write_access_freq.end()) {
                    std::set<uint64_t> tmp;
                    write_access_freq[{phaddr, bit}] = tmp;
                }
                getInfoStream() << "unavailable write access to reg: " << hexval(phaddr) << " old_value: " << hexval(state_map[phaddr].cur_value) << " new value: " << hexval(writeconcretevalue) << " diff: " << hexval(diff)
                                << " pc = " << hexval(state->regs()->getPc()) << "\n";
                write_access_freq[{phaddr, bit}].insert(state->regs()->getPc());
                break;
            }
        }
    }

    // unauthorized access check
    if (data_register.find(phaddr) != data_register.end()) {
        if (!enable_fuzzing && ExistInMMIO(phaddr) && checked_SR == false) {
            getInfoStream() << "unauthorized WRITE access to data register: " << hexval(phaddr)
                            << " pc = " << hexval(state->regs()->getPc()) << "\n";
            if (write_unauthorized_freq.find(phaddr) == write_unauthorized_freq.end()) {
                std::set<uint64_t> tmp;
                tmp.insert(state->regs()->getPc());
                write_unauthorized_freq[phaddr] = tmp;
            } else
                write_unauthorized_freq[phaddr].insert(state->regs()->getPc());
        }
        plgState->write_dr_value(phaddr, writeconcretevalue, 1, begin_irq_flag);
        getInfoStream() << "Write to data register " << hexval(phaddr) << " flag " << begin_irq_flag
                        << " value: " << hexval(writeconcretevalue) << " cur dr: " << hexval(state_map[phaddr].t_value) << " \n";
    } else {
        write_action[phaddr] += 1;
        plgState->write_ph_value(phaddr, writeconcretevalue);
        getInfoStream() << "Write to phaddr " << hexval(phaddr) << " value: " << hexval(writeconcretevalue) << " \n";
    }

    updatedFuzzingReg(state, phaddr);
    UpdateFlag(phaddr);
    UpdateGraph(g_s2e_state, Write, phaddr);
}

void NLPPeripheralModel::updatedFuzzingReg(S2EExecutionState *state, uint32_t phaddr) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    getInfoStream() << plgState->get_fork_point_count() << " ADD phaddr for future emulation: " << hexval(phaddr) << " ori size: "
                    << allFlags.size() << " " << allTAs.size()
                    << " " << todo_flags.size() << " " << todo_tas.size() << " ";

    if (updated_size && plgState->get_fork_point_count() > 10) {
        allFlags = todo_flags;
        allTAs = todo_tas;
        updated_size = false;
        getInfoStream() << "at fork point swap size: " << allFlags.size() << " " << allTAs.size()
                        << " " << todo_flags.size() << " " << todo_tas.size() << " "
                        << "\n";
    }
    if (!enable_fuzzing || !fork_point_flag || todo_registers.find(phaddr) != todo_registers.end()) return;

    uint32_t total_sz = todo_flags.size();
    for (auto loc : Flags_range) {
        auto range = loc.first;
        auto flags = loc.second;
        total_sz += flags.size();
        if (range.find(phaddr) != range.end()) {
            for (auto f : flags) {
                if (f.a.type == "S")
                    todo_flags.push_back(f);
            }
            todo_registers.insert(range.begin(), range.end());
            break;
        }
    }

    total_sz = todo_tas.size();
    for (auto loc : TA_range) {
        auto range = loc.first;
        auto TA = loc.second;
        total_sz += TA.size();
        todo_tas.reserve(total_sz);
        if (range.find(phaddr) != range.end()) {
            todo_tas.insert(todo_tas.end(), TA.begin(), TA.end());
            todo_registers.insert(range.begin(), range.end());
            break;
        }
    }
    updated_size = true;
    getInfoStream() << " new size: " << allFlags.size() << " " << allTAs.size() << " " << todo_flags.size() << " " << todo_tas.size() << " "
                    << "\n";
}

void NLPPeripheralModel::onTranslateBlockStart(ExecutionSignal *signal, S2EExecutionState *state, TranslationBlock *tb,
                                               uint64_t pc) {
    signal->connect(sigc::mem_fun(*this, &NLPPeripheralModel::onForkPoints));
}

void NLPPeripheralModel::CheckEnable(S2EExecutionState *state, std::vector<uint32_t> &irq_no) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    std::map<uint32_t, uint32_t> interrupt_freq = plgState->get_irqs_freq();
    for (auto irq : irq_no) {
        getInfoStream() << "received irq: " << irq << "\n";
        if (interrupt_freq.find(irq) == interrupt_freq.end()) {
            std::set<uint32_t> tmp;
            unenabled_flag[irq] = tmp;
        }
    }
    for (auto loc : TA_range) {
        auto _allTAs = loc.second;
        for (auto ta : _allTAs) {
            EquList action = ta.second;
            if (!enable_fuzzing) {
                uint32_t tmp = action.back().a1.phaddr;
                if (!ExistInMMIO(tmp))
                    continue;
            }
            auto interrupt = action.back().interrupt;
            if (unenabled_flag.find(interrupt) != unenabled_flag.end()) {
                EquList triggers = ta.first;
                for (auto trigger : triggers) {
                    if (action.back().a1.phaddr != trigger.a1.phaddr)
                        unenabled_flag[interrupt].insert(trigger.a1.phaddr);
                }
            }
        }
    }
}

void NLPPeripheralModel::onForkPoints(S2EExecutionState *state, uint64_t pc) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    if (!state->regs()->getInterruptFlag()) {
        tb_num++;
    }
    if (!enable_fuzzing && begin_irq_flag && tb_num % 500 == 0) {
        UpdateGraph(state, Unknown, 0);
    }
    if (!enable_fuzzing && pc == fork_point) {
        getInfoStream() << "begin: "<< hexval(begin_point) << " pc: "<< hexval(pc) << " fork point:" << hexval(fork_point) <<"\n";
        std::queue<uint8_t> return_value;
        //write_to_descriptor(state, return_value);
        plgState->inc_fork_count();
        //if (plgState->get_fork_point_count() < 4) {
        //    return;
        //}
    }
    if (pc == begin_point && begin_point != fork_point && !plgState->check_instruction()) {
        tb_num = 0;
        plgState->inc_fork_count();
        std::queue<uint8_t> return_value;
        std::queue<uint8_t> return_value2;
        uint32_t AFL_size = 0;
        if (plgState->get_fork_point_count() < 3) {
            int i = 0;
            for (auto dr : data_register) {
                if (i == 0) {
                    onBufferInput.emit(state, dr, &AFL_size, &return_value);
                    int cnt = return_value.size();
                    for (int j = 0; j < cnt; j++) {
                        return_value2.push(return_value.front());
                        return_value2.push(return_value.front());
                        return_value2.push(return_value.front());
                        return_value.pop();
                    }
                    plgState->hardware_write_to_receive_buffer(dr, return_value2, return_value2.size());
                    i = 1;
                } else {
                    plgState->hardware_write_to_receive_buffer(dr, return_value2, return_value2.size());
                }
                getInfoStream() << "write to init receiver buffer 56B " << hexval(dr)
                                << " return value: " << return_value2.size() << "\n";
            }
            UpdateFlag(0);
            UpdateGraph(state, Unknown, 0);
        } else {
            begin_irq_flag = true;
            init_dr_flag = true;
            RegMap state_map = plgState->get_state_map();
            uint32_t init_dp_addr = state_map[RXdescriptor].cur_value;
            plgState->insert_cur_dp_addr(init_dp_addr);
            getInfoStream(state) << "init dp addr = " << hexval(init_dp_addr) << "\n";
        }
    } else if (pc == fork_point) {
        begin_irq_flag = true;
        init_dr_flag = true;
        fork_point_flag = true;
        plgState->inc_fork_count();
        tb_num = 0;

        if (plgState->get_fork_point_count() == 1) {
            RegMap state_map = plgState->get_state_map();
            uint32_t init_dp_addr = state_map[RXdescriptor].cur_value;
            plgState->insert_cur_dp_addr(init_dp_addr);
            getInfoStream(state) << "init dp addr = " << hexval(init_dp_addr) << "\n";
        }
        if (!enable_fuzzing) {
            UpdateFlag(0);
            UpdateGraph(state, Unknown, 0);
            if (plgState->pending_interrupt())
                return;
            std::vector<uint32_t> irq_no;
            onEnableISER.emit(state, &irq_no);
            CheckEnable(state, irq_no);
            getWarningsStream() << "already go though Main Loop Point Count = " << plgState->get_fork_point_count() << "\n";
            getWarningsStream() << "===========unit test pass============\n";
            g_s2e->getCorePlugin()->onEngineShutdown.emit();
            // Flush here just in case ~S2E() is not called (e.g., if atexit()
            // shutdown handler was not called properly).
            g_s2e->flushOutputStreams();
            exit(0);
        }
    }
}

void NLPPeripheralModel::onTranslateBlockEnd(ExecutionSignal *signal, S2EExecutionState *state, TranslationBlock *tb,
                                             uint64_t pc, bool staticTarget, uint64_t staticTargetPc) {
    signal->connect(sigc::bind(sigc::mem_fun(*this, &NLPPeripheralModel::onBlockEnd), (unsigned)tb->se_tb_type));
}

/*
template <typename T> static bool getConcolicValue(S2EExecutionState *state, unsigned offset, T *value) {
    auto size = sizeof(T);
    klee::ref<klee::Expr> expr = state->regs()->read(offset, size * 8);
    if (isa<klee::ConstantExpr>(expr)) {
        klee::ref<klee::ConstantExpr> ce = dyn_cast<klee::ConstantExpr>(expr);
        *value = ce->getZExtValue();
        return true;
    } else {
        // evaluate symobolic regs
        klee::ref<klee::ConstantExpr> ce;
        ce = dyn_cast<klee::ConstantExpr>(state->concolics->evaluate(expr));
        *value = ce->getZExtValue();
        return false;
    }
}

static void PrintRegs(S2EExecutionState *state) {
    for (unsigned i = 0; i < 15; ++i) {
        unsigned offset = offsetof(CPUARMState, regs[i]);
        target_ulong concreteData;
        if (getConcolicValue(state, offset, &concreteData)) {
            g_s2e->getWarningsStream() << "Regs " << i << " = " << hexval(concreteData) << "\n";
        } else {
            g_s2e->getWarningsStream() << "Sym Regs " << i << " = " << hexval(concreteData) << "\n";
        }
    }
}
*/
void NLPPeripheralModel::onBlockEnd(S2EExecutionState *state, uint64_t cur_loc, unsigned source_type) {
    DECLARE_PLUGINSTATE(NLPPeripheralModelState, state);
    /*
	if (cur_loc == 0x8002288) {
		getInfoStream() << "change cr!!\n";
		plgState->write_ph_value(0x40005400,0x501);
	} else if (cur_loc == 0x8002316) {
		getInfoStream() << "change cr!!\n";
		plgState->write_ph_value(0x40005400,0x101);
	}
    */
    //RegMap state_map = plgState->get_state_map();
    //uint32_t init_dp_addr = state_map[0x40020044].cur_value;
    //g_s2e->getWarningsStream() << hexval(cur_loc) <<" "<<hexval(init_dp_addr)<<"\n";
    //PrintRegs(state);
    if (init_dr_flag == true && (!state->regs()->getInterruptFlag())) {
        std::queue<uint8_t> return_value;
        uint32_t AFL_size = 0;
        int i = 0;
        for (auto dr : data_register) {
            if (i == 0) {
                onBufferInput.emit(state, dr, &AFL_size, &return_value);
                plgState->hardware_write_to_receive_buffer(dr, return_value, return_value.size());
                i = 1;
            } else {
                plgState->hardware_write_to_receive_buffer(dr, return_value, return_value.size());
            }
            getInfoStream() << "write to receiver buffer " << hexval(dr)
                            << " return value size: " << return_value.size() << "\n";
        }
        if (fork_point_flag) {
            //write_to_descriptor(state, return_value);
        }
        //plgState->clear_irq_freq(37);
        UpdateFlag(0);
        UpdateGraph(state, Unknown, 0);
        init_dr_flag = false;
        //std::vector<uint32_t> irq_no;
        //onEnableISER.emit(state, &irq_no);
        //CheckEnable(state, irq_no);
        /*if (plgState->get_fork_point_count() > 100 && plgState->get_fork_point_count() % 500 == 0) {*/
        //if (!plgState->get_exit_interrupt(16)) {
        //getWarningsStream() << "emit irq 16\n";
        //EmitIRQ(state, 16);
        //}
        /*}*/
    }
}
}
} // namespace s2e::plugins
