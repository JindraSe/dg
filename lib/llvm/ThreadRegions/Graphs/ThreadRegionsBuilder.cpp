#include "ThreadRegionsBuilder.h"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "dg/llvm/ThreadRegions/ThreadRegion.h"
#include "llvm/ThreadRegions/Nodes/EntryNode.h"
#include "llvm/ThreadRegions/Nodes/Node.h"

void ThreadRegionsBuilder::build(
        EntryNode *mainEntry,
        const std::set<const EntryNode *> &procedureEntries) {
    concurrencyProcedureAnalysis_->run(procedureEntries);

    findOrCreateRegion(mainEntry);

    while (!worklist_.empty()) {
        auto *region = worklist_.back();
        worklist_.pop_back();

        const auto *lastNode = region->lastNode();
        auto successorNodes = lastNode->directSuccessors();

        while (!regionIsComplete(lastNode, successorNodes)) {
            // if the region is not complete, then there is only
            // one successor node
            insertNodeIntoRegion(region, *successorNodes.begin());

            lastNode = region->lastNode();
            successorNodes = lastNode->directSuccessors();
        }

        for (auto *successorNode : successorNodes) {
            auto *successorRegion = findOrCreateRegion(successorNode);
            region->addDirectSuccessor(successorRegion);
        }
    }
}

void ThreadRegionsBuilder::printNodes(std::ostream &ostream) const {
    for (const auto &threadRegion : threadRegions_) {
        threadRegion->printNodes(ostream);
    }
}

void ThreadRegionsBuilder::printEdges(std::ostream &ostream) const {
    for (const auto &threadRegion : threadRegions_) {
        threadRegion->printEdges(ostream);
    }
}

void ThreadRegionsBuilder::reserve(std::size_t size) {
    nodeToRegionMap_.reserve(size);
}

ThreadRegion *ThreadRegionsBuilder::entryRegion() const {
    if (threadRegions_.empty()) {
        return nullptr;
    }

    return threadRegions_[0].get();
}

std::set<ThreadRegion *> ThreadRegionsBuilder::allRegions() const {
    std::set<ThreadRegion *> res;

    for (const auto &region : threadRegions_) {
        res.insert(region.get());
    }

    return res;
}

// should be cleared automatically, unlike the previous implementation
void clear() {}

void ThreadRegionsBuilder::insertNodeIntoRegion(ThreadRegion *region,
                                                const Node *node) {
    region->insertNode(node);
    nodeToRegionMap_[node] = region;

    auto addForkedSuccessors = [&](const ForkNode *forkNode) {
        for (auto *forkedProcedureEntry : forkNode->forkSuccessors()) {
            auto *forkedRegion = findOrCreateRegion(forkedProcedureEntry);
            region->addForkedSuccessor(forkedRegion);
        }
    };

    if (node->getType() == NodeType::CALL) {
        const CallNode *callNode = static_cast<const CallNode *>(node);
        EntryNode *procedureEntry = callNode->getEntryNode();

        // it is an extern procedure, we do not analyze it
        if (procedureEntry == nullptr) {
            return;
        }

        auto *procedureRegion = findOrCreateRegion(procedureEntry);

        if (!concurrencyProcedureAnalysis_->isInteresting(procedureEntry)) {
            region->addCallSuccessor(procedureRegion);
            return;
        }

        region->setInterestingCallSuccessor(procedureRegion);

        for (const auto *forkNode :
             concurrencyProcedureAnalysis_->mayCallForks(procedureEntry)) {
            addForkedSuccessors(forkNode);
        }
    }

    else if (node->getType() == NodeType::FORK) {
        addForkedSuccessors(static_cast<const ForkNode *>(node));
    }
}

ThreadRegion *ThreadRegionsBuilder::findOrCreateRegion(const Node *node) {
    if (nodeToRegionMap_.find(node) != nodeToRegionMap_.end()) {
        return nodeToRegionMap_.at(node);
    }

    if (node->getType() == NodeType::ENTRY) {
        const EntryNode *entryNode = static_cast<const EntryNode *>(node);

        if (!concurrencyProcedureAnalysis_->isInteresting(entryNode)) {
            return buildUninterestingProcedure(entryNode);
        }
    }

    threadRegions_.push_back(std::make_unique<ThreadRegion>());
    ThreadRegion *regionPtr = threadRegions_.back().get();

    insertNodeIntoRegion(regionPtr, node);

    worklist_.push_back(regionPtr);
    return regionPtr;
}

bool ThreadRegionsBuilder::isInteresting(const Node *node) const {
    if (node->getType() == NodeType::CALL) {
        const auto *callNode = static_cast<const CallNode *>(node);

        if (callNode->isExtern()) {
            return false;
        }

        return concurrencyProcedureAnalysis_->isInteresting(
                callNode->getEntryNode());
    }

    // IMPORTANT: if we want to consider a more complete MHP analysis, more
    // nodes must be considered interesting, notably the join, lock and unlock
    // nodes
    return node->getType() == NodeType::FORK;
}

ThreadRegion *
ThreadRegionsBuilder::buildUninterestingProcedure(const EntryNode *entryNode) {
    threadRegions_.push_back(std::make_unique<ThreadRegion>());
    ThreadRegion *region = threadRegions_.back().get();

    std::set<Node *> seenNodes;
    std::vector<const Node *> unexplored = {entryNode};
    ExitNode *exitNode = nullptr;

    region->insertNode(entryNode);
    nodeToRegionMap_[entryNode] = region;

    while (!unexplored.empty()) {
        const auto *current = unexplored.back();
        unexplored.pop_back();

        for (auto *successor : current->directSuccessors()) {
            if (seenNodes.find(successor) != seenNodes.end()) {
                continue;
            }

            seenNodes.insert(successor);
            unexplored.push_back(successor);

            if (successor->getType() == NodeType::EXIT) {
                exitNode = static_cast<ExitNode *>(successor);
                continue;
            }

            region->insertNode(successor);
            nodeToRegionMap_[successor] = region;

            if (successor->getType() == NodeType::CALL) {
                CallNode *callNode = static_cast<CallNode *>(successor);
                auto *entry = callNode->getEntryNode();

                // this means the function is external
                if (entry == nullptr) {
                    continue;
                }

                // FIXME: this may lead to potentially unbounded recursion;
                // it is probably not an issue, but it should be fixed
                region->addCallSuccessor(findOrCreateRegion(entry));
            }
        }
    }

    // if there is an infinte cycle, the exit node is not reachable; we
    // do not need to consider it in the MHP analysis, as the node
    // never happens in parallel with anything
    if (exitNode == nullptr) {
        return region;
    }

    // the exit node must be pushed last, because it will be the last node
    region->insertNode(exitNode);
    nodeToRegionMap_[exitNode] = region;

    return region;
}

// IMPORTANT: if we start considering joins, then a new region
// will also need to be created before every join node
bool ThreadRegionsBuilder::regionIsComplete(
        const Node *lastNode, const std::set<Node *> &successors) const {
    if (isInteresting(lastNode)) {
        return true;
    }

    if (successors.empty()) {
        return true;
    }

    if (successors.size() > 1) {
        return true;
    }

    if ((*successors.begin())->directPredecessors().size() > 1) {
        return true;
    }

    return false;
}
