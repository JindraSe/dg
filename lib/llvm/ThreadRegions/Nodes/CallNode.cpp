#include "CallNode.h"

#include <iostream>

#include "EntryNode.h"

CallNode::CallNode(const llvm::Instruction *instruction,
                   const llvm::CallInst *callInst)
        : Node(NodeType::CALL, instruction, callInst) {}

bool CallNode::addDirectSuccessor(Node *callSuccessor) {
    directSuccessors_.insert(callSuccessor);
    callSuccessor->addCallPredecessor(this);
    return true;
}

// there is something wrong with the graph creation;
// this is just a quick fix, further refactoring may be needed
bool CallNode::isExtern() const {
    if (directSuccessors_.empty()) {
        return true;
    }

    if (successorsNumber() > 1) {
        return true;
    }

    if ((*successors().begin())->getType() != NodeType::ENTRY) {
        return true;
    }

    return false;
}

std::set<Node *> CallNode::directSuccessors() const {
    if (isExtern()) {
        return successors();
    }

    return directSuccessors_;
}

EntryNode *CallNode::getEntryNode() const {
    // extern functions are not modelled in the threaded CFG
    if (isExtern()) {
        return nullptr;
    }

    // if the function is not extern, then it has one successor,
    // and it is an entry node
    return static_cast<EntryNode *>(*successors().begin());
}
