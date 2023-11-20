#ifndef CONCURRENCYPROCEDUREANALYSIS_H
#define CONCURRENCYPROCEDUREANALYSIS_H

#include "llvm/ThreadRegions/Nodes/Nodes.h"
#include <llvm/IR/Value.h>
#include <memory>
#include <unordered_map>

class ControlFlowGraphAnalysis;

/* The purpose of this class is to determine the concurrency
 * behaviour of the procedures used in the program. This
 * helps keep thread regions smaller, and makes MHP analysis
 * easier.
 */
class ConcurrencyProcedureAnalysis {
    class ProcedureInfo {
        // present on some paths from entry to exit
        std::set<const Node *> usedNodes_;

        std::set<const EntryNode *> directlyCalledProcedures_;
        std::set<const EntryNode *> recursivelyCalledProcedures_;

        std::set<const ForkNode *> directlyCalledForks_;
        std::set<const ForkNode *> calledForks_;

        bool isInteresting_ = false;

        friend ConcurrencyProcedureAnalysis;
    };

    std::unordered_map<const EntryNode *, std::unique_ptr<ProcedureInfo>>
            procedureInfos_;

  public:
    ConcurrencyProcedureAnalysis() = default;
    void run(const std::set<const EntryNode *> &procedures);

    // An interesting function calls a pthread* function, or
    // calls an interesting function
    bool isInteresting(const EntryNode *procedure) const;

    const std::set<const EntryNode *> &
    mayCallProcedures(const EntryNode *procedure) const;

    const std::set<const ForkNode *> &
    mayCallForks(const EntryNode *procedure) const;

  private:
    void findUsedNodes();

    // these functions figure out which functions may be called by other
    // functions, first directly, then recursively, until a fixed point
    // is reached
    void findDirectlyCalledProcedures();
    void findRecursivelyCalledProcedures();

    void setInitialInteresting();
    void setInteresting();

    // we also need to find via which FORKS this function may cause
    // a thread to be created; we need to consider recursion and
    // forks from inside of forks, we also iterate until a fixed point
    void findDirectlyCalledForks();
    void findAllCalledForks();
};

#endif // CONCURRENCYPROCEDUREANALYSIS_H
