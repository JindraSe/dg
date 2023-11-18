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
    // every node that is not an exit node has a direct successor;
    // if this is empty, this means that the direct successors
    // are the actual successors
    if (isExtern()) {
        return nullptr;
    }

    return static_cast<EntryNode *>(*successors().begin());
}
