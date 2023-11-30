#include "BlockGraph.h"

BlockGraph::BlockGraph(const llvm::BasicBlock *llvmBlock, Node *firstNode,
                       Node *lastNode, CallNode *callNode)
        : llvmBlock_(llvmBlock), firstNode_(firstNode), lastNode_(lastNode),
          callNode_(callNode)

{}

Node *BlockGraph::firstNode() const { return firstNode_; }

Node *BlockGraph::lastNode() const { return lastNode_; }

Node *BlockGraph::callNode() const { return callNode_; }

const llvm::BasicBlock *BlockGraph::llvmBlock() const { return llvmBlock_; }

void BlockGraph::addSuccessor(Node *successor) const {
    lastNode_->addSuccessor(successor);
    if (callNode_ != nullptr) {
        callNode_->addDirectSuccessor(successor);
    }
}

void BlockGraph::addSuccessor(BlockGraph *blockGraph) const {
    addSuccessor(blockGraph->firstNode_);
}

void BlockGraph::addPredecessor(Node *predecessor) const {
    predecessor->addSuccessor(firstNode_);
}

void BlockGraph::addPredecessor(BlockGraph *blockGraph) const {
    blockGraph->addSuccessor(firstNode_);
}
