///
/// Copyright (C) 2017, Cyberhaven
/// All rights reserved.
///
/// Licensed under the Cyberhaven Research License Agreement.
///

#ifndef S2E_PLUGINS_MyMultiSearcher_H
#define S2E_PLUGINS_MyMultiSearcher_H
#include <klee/Searcher.h>
#include <s2e/CorePlugin.h>
#include <s2e/Plugin.h>
#include <s2e/Plugins/uEmu/ARMFunctionMonitor.h>
#include <s2e/Plugins/uEmu/InvalidStatesDetection.h>
#include <s2e/Plugins/Searchers/MultiSearcher.h>
#include <s2e/S2EExecutionState.h>
#include <s2e/S2E.h>
#include <s2e/S2EExecutor.h>
#include <s2e/cpu.h>
#include <chrono>
#include <random>
#include <deque>
#include <vector>
#include <map>
namespace s2e {
namespace plugins {

    //////////////////////------------------------------------/////
class MyMultiSearcher : public Plugin {
    S2E_PLUGIN
    friend class MyMultiSearcherclassClass;

private:
    klee::Searcher *m_top;
    MultiSearcher *m_searchers;
    InvalidStatesDetection *onInvalidStateDectionConnection;
    ARMFunctionMonitor *onARMFunctionConnection;

public:
    MyMultiSearcher(S2E *s2e) : Plugin(s2e) {
    }

    void initialize();
    void onStateSwitch(S2EExecutionState *current, S2EExecutionState *next);
    void updateState(S2EExecutionState *state);
    void onARMFunctionReturn(S2EExecutionState *state, uint32_t return_pc);
    void onARMFunctionCall(S2EExecutionState *state, uint32_t caller_pc, uint64_t function_hash);
    void update(klee::ExecutionState *current, const klee::StateSet &addedStates,
                               const klee::StateSet &removedStates);

};

class MyMultiSearcherclass : public klee::Searcher {
private:
    typedef std::set<S2EExecutionState *> StateSet;
    StateSet m_states;
protected:
    MyMultiSearcher *m_plg;
    unsigned m_level;
    std::map<uint64_t, std::unique_ptr<klee::Searcher>> m_searchers;
    llvm::raw_ostream &getInfoStream(S2EExecutionState *state = nullptr) const;
    llvm::raw_ostream &getDebugStream(S2EExecutionState *state = nullptr) const;
    llvm::raw_ostream &getWarningsStream(S2EExecutionState *state = nullptr) const;

public:
    MyMultiSearcherclass(MyMultiSearcher *plugin, unsigned level) : m_plg(plugin), m_level(level){};
    virtual ~MyMultiSearcherclass() {
    }

    virtual klee::ExecutionState &selectState();

    virtual void update(klee::ExecutionState *current, const klee::StateSet &addedStates,
                        const klee::StateSet &removedStates);
    klee::ExecutionState &DFS(S2EExecutionState * state,uint32_t conditionPC);

    virtual bool empty() {
        return m_states.empty();
    }
};
// namespace hw
} // namespace plugins
} // namespace s2e

#endif // S2E_PLUGINS_MyMultiSearcher_H

