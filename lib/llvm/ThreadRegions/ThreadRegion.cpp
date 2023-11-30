#include "dg/llvm/ThreadRegions/ThreadRegion.h"

#include "llvm/ThreadRegions/Nodes/Node.h"

#include <iostream>

int ThreadRegion::lastId = 0;

ThreadRegion::ThreadRegion() : id_(lastId++) {}

int ThreadRegion::id() const { return id_; }

void ThreadRegion::addDirectSuccessor(const ThreadRegion *region) {
    directSuccessors_.push_back(region);
}

void ThreadRegion::addCallSuccessor(const ThreadRegion *region) {
    if (calledSuccessors_.find(region) == calledSuccessors_.end()) {
        calledSuccessors_.insert(region);
    }
}

void ThreadRegion::addForkedSuccessor(const ThreadRegion *region) {
    forkedSuccessors_.push_back(region);
}

void ThreadRegion::setInterestingCallSuccessor(const ThreadRegion *region) {
    interestingCallSuccessor_ = region;
}

const std::vector<const ThreadRegion *> &
ThreadRegion::directSuccessors() const {
    return directSuccessors_;
}

const std::set<const ThreadRegion *> &
ThreadRegion::calledSuccessors() const {
    return calledSuccessors_;
}

const std::vector<const ThreadRegion *> &
ThreadRegion::forkedSuccessors() const {
    return forkedSuccessors_;
}

const ThreadRegion *ThreadRegion::interestingCallSuccessor() const {
    return interestingCallSuccessor_;
}

bool ThreadRegion::insertNode(const Node *node) {
    nodes_.push_back(node);
    return false;
}

const Node *ThreadRegion::firstNode() const {
    if (nodes_.empty()) {
        return nullptr;
    }

    return nodes_[0];
};

const Node *ThreadRegion::lastNode() const {
    if (nodes_.empty()) {
        return nullptr;
    }

    return nodes_.back();
};

void ThreadRegion::printNodes(std::ostream &ostream) {
    ostream << "subgraph " << dotName() << " {\n";
    ostream << "color = blue\n style = rounded\n";
    for (const auto &node : nodes_) {
        ostream << node->dump();
    }
    ostream << "}\n";
}

void ThreadRegion::printEdges(std::ostream &ostream) {
    for (const auto &successor : directSuccessors_) {
        ostream << this->firstNode()->dotName() << " -> "
                << successor->firstNode()->dotName()
                << " [ltail = " << this->dotName()
                << " lhead = " << successor->dotName()
                << ", color = blue, style = bold]\n";
    }

    for (const auto &successor : forkedSuccessors_) {
        ostream << this->firstNode()->dotName() << " -> "
                << successor->firstNode()->dotName()
                << " [ltail = " << this->dotName()
                << " lhead = " << successor->dotName()
                << ", color = darkorchid, fontcolor = darkorchid, style = bold"
                << ", label=\"forks\"]";
    }

    for (const auto &successor : calledSuccessors_) {
        ostream << this->firstNode()->dotName() << " -> "
                << successor->firstNode()->dotName()
                << " [ltail = " << this->dotName()
                << " lhead = " << successor->dotName()
                << ", color = chocolate, fontcolor = chocolate, style = bold"
                << ", label=\"calls\"]\n";
    }

    if (interestingCallSuccessor_ != nullptr) {
        ostream << this->firstNode()->dotName() << " -> "
                << interestingCallSuccessor_->firstNode()->dotName()
                << " [ltail = " << this->dotName()
                << " lhead = " << interestingCallSuccessor_->dotName()
                << ", color = chocolate4, fontcolor = chocolate4, style = bold"
                << ", label=\"calls (interesting)\"]";
    }
}

std::string ThreadRegion::dotName() const {
    return "cluster_" + std::to_string(id_);
}

std::set<const llvm::Instruction *> ThreadRegion::llvmInstructions() const {
    std::set<const llvm::Instruction *> llvmValues;
    for (const auto &node : nodes_) {
        if (!node->isArtificial()) {
            llvmValues.insert(node->llvmInstruction());
        }
    }
    return llvmValues;
}
