#include "ConcurrencyProcedureAnalysis.h"
#include <vector>

void ConcurrencyProcedureAnalysis::run(
        const std::set<const EntryNode *> &procedures) {
    for (const auto *procedure : procedures) {
        procedureInfos_[procedure] = std::make_unique<ProcedureInfo>();
    }

    findUsedNodes();

    findDirectlyCalledProcedures();
    findRecursivelyCalledProcedures();

    setInteresting();

    findDirectlyCalledForks();
    findAllCalledForks();
}

void ConcurrencyProcedureAnalysis::findUsedNodes() {
    for (auto &pair : procedureInfos_) {
        std::set<const Node *> &usedNodes = pair.second->usedNodes_;
        std::vector<const Node *> worklist = {pair.first};

        while (!worklist.empty()) {
            auto successors = worklist.back()->directSuccessors();
            worklist.pop_back();

            for (auto *successor : successors) {
                if (usedNodes.find(successor) == usedNodes.end()) {
                    usedNodes.insert(successor);
                    worklist.push_back(successor);
                }
            }
        }
    }
}

void ConcurrencyProcedureAnalysis::findDirectlyCalledProcedures() {
    for (auto &pair : procedureInfos_) {
        for (const auto *node : pair.second->usedNodes_) {
            if (node->getType() == NodeType::CALL) {
                const auto *callNode = static_cast<const CallNode *>(node);
                const auto *entryNode = callNode->getEntryNode();

                // the function is extern
                if (entryNode == nullptr) {
                    continue;
                }

                pair.second->directlyCalledProcedures_.insert(entryNode);
            }
        }
    }
}

void ConcurrencyProcedureAnalysis::findRecursivelyCalledProcedures() {
    for (auto &pair : procedureInfos_) {
        pair.second->recursivelyCalledProcedures_.insert(
                pair.second->directlyCalledProcedures_.begin(),
                pair.second->directlyCalledProcedures_.end());
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (auto &pair : procedureInfos_) {
            std::set<const EntryNode *> newlyAdded;
            auto &current = pair.second->recursivelyCalledProcedures_;

            for (const auto *calledProcedure : current) {
                const auto &recCalledProcedures =
                        procedureInfos_[calledProcedure]
                                ->recursivelyCalledProcedures_;
                for (const auto *recCalled : recCalledProcedures) {
                    if (current.find(recCalled) == current.end()) {
                        newlyAdded.insert(recCalled);
                    }
                }
            }

            if (!newlyAdded.empty()) {
                changed = true;
                current.insert(newlyAdded.begin(), newlyAdded.end());
            }
        }
    }
}

void ConcurrencyProcedureAnalysis::setInitialInteresting() {
    for (auto &pair : procedureInfos_) {
        for (const auto *node : pair.second->usedNodes_) {
            // IMPORTANT: if we want to consider a more complete MHP analysis,
            // more nodes must be considered interesting, notably the join, lock
            // and unlock nodes
            if (node->getType() == NodeType::FORK) {
                pair.second->isInteresting_ = true;
                break;
            }
        }
    }
}

// FIXME: refactor, maybe find first by which procedures procedures
// are called; that should lower the complexity of this method
void ConcurrencyProcedureAnalysis::setInteresting() {
    setInitialInteresting();

    bool changed = true;

    while (changed) {
        changed = false;

        for (auto &pair : procedureInfos_) {
            if (pair.second->isInteresting_) {
                continue;
            }

            const auto &recCalled = pair.second->recursivelyCalledProcedures_;

            for (const auto &calledProc : recCalled) {
                if (isInteresting(calledProc)) {
                    changed = true;
                    pair.second->isInteresting_ = true;
                    break;
                }
            }
        }
    }
}

void ConcurrencyProcedureAnalysis::findDirectlyCalledForks() {
    for (auto &pair : procedureInfos_) {
        if (!pair.second->isInteresting_) {
            continue;
        }

        for (const auto *node : pair.second->usedNodes_) {
            if (node->getType() == NodeType::FORK) {
                pair.second->directlyCalledForks_.insert(
                        static_cast<const ForkNode *>(node));
            }
        }
    }
}

// FIXME: this method has a high cognitive complexity, it could be refatctored
void ConcurrencyProcedureAnalysis::findAllCalledForks() {
    for (auto &pair : procedureInfos_) {
        if (!pair.second->isInteresting_) {
            continue;
        }

        pair.second->calledForks_.insert(
                pair.second->directlyCalledForks_.begin(),
                pair.second->directlyCalledForks_.end());
    }

    for (auto &pair : procedureInfos_) {
        if (!pair.second->isInteresting_) {
            continue;
        }

        for (const auto *calledProcedure :
             pair.second->recursivelyCalledProcedures_) {
            pair.second->calledForks_.insert(
                    procedureInfos_[calledProcedure]->calledForks_.begin(),
                    procedureInfos_[calledProcedure]->calledForks_.end());
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (auto &pair : procedureInfos_) {
            if (!pair.second->isInteresting_) {
                continue;
            }

            std::set<const ForkNode *> newlyAdded;
            auto &current = pair.second->calledForks_;

            for (const auto *node : current) {
                const auto *forkNode = static_cast<const ForkNode *>(node);

                for (const auto *entryNode : forkNode->forkSuccessors()) {
                    for (const auto *otherFork :
                         procedureInfos_[entryNode]->calledForks_) {
                        if (current.find(otherFork) == current.end()) {
                            newlyAdded.insert(otherFork);
                        }
                    }
                }
            }

            if (!newlyAdded.empty()) {
                changed = true;
                current.insert(newlyAdded.begin(), newlyAdded.end());
            }
        }
    }
}

bool ConcurrencyProcedureAnalysis::isInteresting(
        const EntryNode *procedure) const {
    return procedureInfos_.at(procedure)->isInteresting_;
}

const std::set<const ForkNode *> &
ConcurrencyProcedureAnalysis::mayCallForks(const EntryNode *procedure) const {
    return procedureInfos_.at(procedure)->calledForks_;
}
