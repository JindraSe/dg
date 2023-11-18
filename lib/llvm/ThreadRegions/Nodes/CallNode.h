#ifndef CALLNODE_H
#define CALLNODE_H

#include "Node.h"

class CallNode : public Node {
    std::set<Node *> directSuccessors_;

  public:
    CallNode(const llvm::Instruction *instruction = nullptr,
             const llvm::CallInst *callInst = nullptr);

    bool addDirectSuccessor(Node *callSuccessor);

    // FIXME: this is ugly; refactor the entire graph creation

    bool isExtern() const;

    std::set<Node *> directSuccessors() const override;

    EntryNode *getEntryNode() const;
};

#endif // CALLNODE_H
