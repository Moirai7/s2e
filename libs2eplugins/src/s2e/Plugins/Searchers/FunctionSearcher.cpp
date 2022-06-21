///
/// Copyright (C) 2017, Cyberhaven
/// All rights reserved.
///
/// Licensed under the Cyberhaven Research License Agreement.
///


#include <stack>
#include "FunctionSearcher.h"
#include <s2e/ConfigFile.h>
#include <cxxabi.h>
#include <s2e/S2E.h>
#include <s2e/Utils.h>
#include <fstream>
using namespace klee;

namespace s2e {
namespace plugins {
S2E_DEFINE_PLUGIN(MyMultiSearcher, "MyMultiSearcher S2E plugin", "MyMultiSearcher",
                   "ARMFunctionMonitor","MultiSearcher");
//----关键数据结构-------------------------------
uint32_t record_call_pc;
std::map<uint32_t, std::map<uint32_t,std::vector<S2EExecutionState *>>> fork_condition_class;
std::map<uint32_t, std::map<uint32_t,std::vector<S2EExecutionState *>> > fork_condition_class_copy;
///

void MyMultiSearcher::initialize() {
    m_searchers = s2e()->getPlugin<MultiSearcher>();
    m_top=new MyMultiSearcherclass(this,0);
    m_searchers->registerSearcher("FunctionSearcher", m_top);
    m_searchers->selectSearcher("FunctionSearcher");
    // s2e()->getCorePlugin()->onInitializationComplete.connect(sigc::mem_fun(*this, &MyMultiSearcher::onInitComplete));
    //s2e()->getExecutor()->setSearcher(this);
    onARMFunctionConnection = s2e()->getPlugin<ARMFunctionMonitor>();
    onARMFunctionConnection->onARMFunctionCallEvent.connect(
        sigc::mem_fun(*this, &MyMultiSearcher::onARMFunctionCall));
    onARMFunctionConnection->onARMFunctionReturnEvent.connect(
        sigc::mem_fun(*this, &MyMultiSearcher::onARMFunctionReturn));
    s2e()->getCorePlugin()->onStateSwitch.connect(
        sigc::mem_fun(*this, &MyMultiSearcher::onStateSwitch));

}

void MyMultiSearcher::update (klee::ExecutionState *current, const klee::StateSet &addedStates,
                          const klee::StateSet &removedStates) {
   m_top->update(current, addedStates, removedStates);
}

//update函数在remove和fork时有
void MyMultiSearcherclass::update(klee::ExecutionState *current, const klee::StateSet &addedStates,
                               const klee::StateSet &removedStates) {
    //getInfoStream() << "add state size =  " << addedStates.size() << " remove state size = " << removedStates.size() << "\n";

    foreach2 (ait, addedStates.begin(), addedStates.end()) {
        S2EExecutionState *addedState = dynamic_cast<S2EExecutionState *>(*ait);//现在的去除状态
        if (g_s2e_state != addedState && addedState->getID() != 0) {
            uint32_t PC;
            PC = g_s2e_state->regs()->getPc();
            fork_condition_class[record_call_pc][PC].push_back(g_s2e_state);
            fork_condition_class[record_call_pc][PC].push_back(addedState);
            fork_condition_class_copy[record_call_pc][PC].push_back(g_s2e_state);
            fork_condition_class_copy[record_call_pc][PC].push_back(addedState);
            getInfoStream() << "add state "<< addedState->getID() << " to fork condition map pc =  " << hexval(PC) << " caller_pc  = " << hexval(record_call_pc) << "\n";
            getWarningsStream() << " add state size = " << addedState->stack.size()
                                         << "\n";
            m_states.insert(addedState);
        }
    }

    foreach2 (rit, removedStates.begin(), removedStates.end()) {
        int remove_ID;
        S2EExecutionState *removedState = dynamic_cast<S2EExecutionState *>(*rit);//现在的去除状态
        m_states.erase(removedState);
        remove_ID=removedState->getID();
        std::map<uint32_t,std::vector<S2EExecutionState *>>::iterator it;
        for (it=fork_condition_class[record_call_pc].begin();it!=fork_condition_class[record_call_pc].end();it++) {
            std::vector<S2EExecutionState *> temp;
            temp=it->second;
            S2EExecutionState *f,*s;
            f=temp[0];
            s=temp[1];
            if (f == removedState) {
                (it->second).erase((it->second).begin());
            } else if (s == removedState) {
                (it->second).erase((it->second).begin()+1);
            }
        }
    }
}

klee::ExecutionState &MyMultiSearcherclass::selectState() {
    uint32_t PC;
    PC = g_s2e_state->regs()->getPc();//g_s2e
    //S2EExecutionState *return_s=nullptr;
    //如果condition下面的两个状态全部被遍历后，就直接删除map结构里面，因为condition的地址是不会发生变化的
    /*
    // std::pair<S2EExecutionState::ExecutionState *,S2EExecutionState::ExecutionState *> it2= mycondition_N_ID[record_call_pc][PC];
    // for(std::vector<S2EExecutionState::ExecutionState *>::iterator it= add_state_exc.begin();it!= add_state_exc.end();it++)
    // {
    //      S2EExecutionState::ExecutionState *first,*second;
    //     first=it2.first;
    //     second=it2.second;
    //     if(first==*it)
    //     {
    //         //klee::ExecutionState *addedState = static_cast<klee::ExecutionState *>(*it);
    //         return *addedState;
    //     }
    //     else if(second==*it)
    //     {
    //         //klee::ExecutionState *addedState = static_cast<klee::ExecutionState *>(*it);
    //         return *addedState;
    //     }
    // }*/
    //return出来的时候查看当前路径的ID是在哪一个conditionPC里面，找到它之后返回他的另一个，在大的map的循环,
    //return时候记录的state还有onfork的时候记录的PC
    std::map<uint32_t,std::vector<S2EExecutionState *>>::iterator it;
    for(it=fork_condition_class_copy[record_call_pc].begin();it!=fork_condition_class_copy[record_call_pc].end();it++)
    {
        std::vector<S2EExecutionState *> temp;
        temp=it->second;
        uint32_t conditionPC=it->first;
        if(temp.size()!=0){

            S2EExecutionState *f,*s;
            f=temp[0];
            s=temp[1];
            //还得查看对应的另外一项是否遍历过，新建一个备份的onforkstate，用size的大小查看是否访问过
            if(g_s2e_state == f) {
                std::vector<S2EExecutionState *> temp1;
                temp1=fork_condition_class[record_call_pc][conditionPC];
                if(temp1.size() == 2) {
                    temp1.clear();
                    getInfoStream() << "select state: " << hexval(s->getID())<< " selected state's PC= " << hexval(s->regs()->getPc())<<"\n";
                    return *s;
                } else{
                    //return_s=
                    //getDebugStream() << "select state: " << hexval(g_s2e->getID()) <<"\n";
                    return DFS(s,conditionPC);
                }
            } else if (g_s2e_state == s) {
                std::vector<S2EExecutionState *> temp1;
                temp1=fork_condition_class[record_call_pc][conditionPC];//map是从小到大的有序map
                if(temp1.size()==2) {
                    temp1.clear();
                    getInfoStream() << "select state: " << hexval(f->getID())<< " selected state's PC= " << hexval(f->regs()->getPc())<<"\n";
                    return *f;
                } else {
                    return DFS(f,conditionPC);
                }
            }
        } else {
            getWarningsStream() << "ERROR Happened: "  << "\n";
        }
    }
    return *g_s2e_state;
}

llvm::raw_ostream &MyMultiSearcherclass::getDebugStream(S2EExecutionState *state) const {
    if (m_plg->getLogLevel() <= LOG_DEBUG) {
        // TODO: find a way to move this to plugin class.
        int status;
        std::string name = typeid(*this).name();
        char *demangled = abi::__cxa_demangle(name.c_str(), 0, 0, &status);
        llvm::raw_ostream &ret = m_plg->getDebugStream(state) << demangled << "(" << hexval(this) << ") - ";
        free(demangled);
        return ret;
    } else {
        return m_plg->getNullStream();
    }
}

llvm::raw_ostream &MyMultiSearcherclass::getInfoStream(S2EExecutionState *state) const {
    if (m_plg->getLogLevel() <= LOG_DEBUG) {
        // TODO: find a way to move this to plugin class.
        int status;
        std::string name = typeid(*this).name();
        char *demangled = abi::__cxa_demangle(name.c_str(), 0, 0, &status);
        llvm::raw_ostream &ret = m_plg->getInfoStream(state) << demangled << "(" << hexval(this) << ") - ";
        free(demangled);
        return ret;
    } else {
        return m_plg->getNullStream();
    }
}

llvm::raw_ostream &MyMultiSearcherclass::getWarningsStream(S2EExecutionState *state) const {
    if (m_plg->getLogLevel() <= LOG_DEBUG) {
        // TODO: find a way to move this to plugin class.
        int status;
        std::string name = typeid(*this).name();
        char *demangled = abi::__cxa_demangle(name.c_str(), 0, 0, &status);
        llvm::raw_ostream &ret = m_plg->getWarningsStream(state) << demangled << "(" << hexval(this) << ") - ";
        free(demangled);
        return ret;
    } else {
        return m_plg->getNullStream();
    }
}


klee::ExecutionState &MyMultiSearcherclass::DFS(S2EExecutionState *state, uint32_t conditionPC) {
    //conditionPC是fork出来state的condition的地址，现在的任务是遍历map去寻找出是谁fork出来了现在condition
    std::map<uint32_t,std::vector<S2EExecutionState *>>::iterator it;
    //S2EExecutionState *return_s=nullptr;
    for(it=fork_condition_class_copy[record_call_pc].begin();it!=fork_condition_class_copy[record_call_pc].end();it++)
    {
        std::vector<S2EExecutionState *> temp;
        temp=it->second;
        S2EExecutionState *f,*s;
        f=temp[0];
        s=temp[1];
        uint32_t PC1,PC2;
        PC1=f->regs()->getPc();
        PC2=s->regs()->getPc();
        uint32_t temp_con_PC=it->first;
        if (PC1 == conditionPC) {
            std::vector<S2EExecutionState *> temp1;
            temp1=fork_condition_class[record_call_pc][temp_con_PC];
            if(temp1.size() == 2) {
                temp1.clear();
                getInfoStream() << "select state: " << hexval(s->getID())<< " selected state's PC= " << hexval(s->regs()->getPc())<<"\n";
                return *s;
            } else {
                conditionPC=temp_con_PC;
            }
        } else if(PC2 == conditionPC) {
            std::vector<S2EExecutionState *> temp1;
            temp1=fork_condition_class[record_call_pc][temp_con_PC];
            if (temp1.size() == 2) {
                temp1.clear();
                getInfoStream() << "select state: " << hexval(f->getID())<< " selected state's PC= " << hexval(f->regs()->getPc())<<"\n";
                return *f;
            } else
                conditionPC=temp_con_PC;

        }
    }
    return *state;
}


void MyMultiSearcher::onARMFunctionCall(S2EExecutionState *state, uint32_t caller_pc, uint64_t function_hash) {
    //DECLARE_PLUGINSTATE(MyMultiSearcherState, state);
    record_call_pc = caller_pc;
    std::ofstream ofs;
    getInfoStream() << "function's call    (caller_pc) address   is  "<<hexval(caller_pc) << "\n";
    getInfoStream() << "function's call (record_call_pc) address is  "<<hexval(record_call_pc) << "\n";
    /*ofs.open("/vagrant/Myinfo.txt",std::ios::app);*/
    //ofs<<"function's call    (caller_pc) address   is: "<<hexval(record_call_pc)<<std::endl;
    /*ofs.close();*/
    //getInfoStream() << "fork state condition PC: " << hexval(state->regs()->getPc()) <<"\n";
}

void MyMultiSearcher::onARMFunctionReturn(S2EExecutionState *state, uint32_t return_pc) {
   // DECLARE_PLUGINSTATE(MyMultiSearcherState, state);
    getInfoStream() << "function's returning          state ID   is: " << hexval(state->getID()) <<"\n";
    getInfoStream() << "function's return  (return_pc) address   is  "<<hexval(return_pc) << "\n";
    /*std::ofstream ofs;*/
    //ofs.open("/vagrant/Myinfo.txt",std::ios::app);
    //ofs<<"function's return  (return_pc) address   is: "<<hexval(return_pc)<<std::endl;
    /*ofs.close();*/
    if (fork_condition_class_copy[record_call_pc].size() > 0){
        getInfoStream()<<"select other state to execute  " <<"\n";
        s2e()->getExecutor()->setCpuExitRequest();
        //throw CpuExitException();
        //s2e()->getExecutor()->selectNextState(state);
    }
}

void MyMultiSearcher::onStateSwitch(S2EExecutionState *current, S2EExecutionState *next) {
    //DECLARE_PLUGINSTATE(MyMultiSearcherState, state);
    /*std::ofstream ofs;*/
    //ofs.open("/vagrant/Myinfo.txt",std::ios::app);
    //ofs<<"fork state condition PC: "<<hexval(state->regs()->getPc())<<std::endl;
    /*ofs.close();*/
    getInfoStream()<< " execute only this path and ID=  "<< hexval(next->getID()) <<"\n";
    //s2e()->getExecutor()->cleanupTranslationBlock(next);
    //getInfoStream() << "fork state condition PC: " << hexval(state->regs()->getPc()) <<"\n";
    //uint32_t PC;
    //PC = state->regs()->getPc();
    /*fork_condition_class[record_call_pc][PC].push_back(first);*/
    //fork_condition_class[record_call_pc][PC].push_back(second);
    //fork_condition_class_copy[record_call_pc][PC].push_back(first);
    /*fork_condition_class_copy[record_call_pc][PC].push_back(second);*/
}

} // namespace plugins
} // namespace s2e

