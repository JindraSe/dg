#ifndef BLOCKGRAPH_H
#define BLOCKGRAPH_H

#include "llvm/ThreadRegions/Nodes/CallNode.h"
#include "llvm/ThreadRegions/Nodes/Node.h"
#include <map>
#include <ostream>
#include <set>

namespace llvm {
class BasicBlock;
}

class BlockGraph {
  private:
    const llvm::BasicBlock *llvmBlock_ = nullptr;

    Node *firstNode_ = nullptr;
    Node *lastNode_ = nullptr;
    CallNode *callNode_ = nullptr;

  public:
    BlockGraph(const llvm::BasicBlock *llvmBlock, Node *firstNode,
               Node *lastNode, CallNode *callNode = nullptr);

    BlockGraph(const BlockGraph &) = delete;

    BlockGraph &operator==(const BlockGraph &) = delete;

    const llvm::BasicBlock *llvmBlock() const;

    Node *firstNode() const;
    Node *lastNode() const;

    // in case the last node in the block is a call instruction,
    // we need to add the direct successor edges correctly
    Node *callNode() const;

    void addSuccessor(Node *successor) const;
    void addSuccessor(BlockGraph *blockGraph) const;

    void addPredecessor(Node *predecessor) const;
    void addPredecessor(BlockGraph *blockGraph) const;
};

#endif // BLOCKGRAPH_H
