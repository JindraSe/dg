#ifndef THREADREGION_H
#define THREADREGION_H

#include <iosfwd>
#include <set>
#include <vector>

class Node;
class ForkNode;
class ControlFlowGraph;

namespace llvm {
class Instruction;
}

class ThreadRegion {
    int id_;
    std::vector<const Node *> nodes_;

    std::vector<const ThreadRegion *> directSuccessors_;
    std::vector<const ThreadRegion *> calledSuccessors_;
    std::vector<const ThreadRegion *> forkedSuccessors_;
    const ThreadRegion *interestingCallSuccessor_ = nullptr;

    static int lastId;

  public:
    ThreadRegion();

    int id() const;

    // there are four types of successors based on which MHP
    // information will be propagated to them

    void addDirectSuccessor(const ThreadRegion *region);
    void addCallSuccessor(const ThreadRegion *region);
    void addForkedSuccessor(const ThreadRegion *region);
    void setInterestingCallSuccessor(const ThreadRegion *region);

    // the direct successors are in the same procedure as
    // the our region; the MHP data propagates to them,
    // and they may get new MHP relations, depending on
    // other successors
    const std::vector<const ThreadRegion *> &directSuccessors() const;

    // the called successors are the uninteresting calls from
    // inside of the region; only the MHP information from this
    // node propagates to them directly
    const std::vector<const ThreadRegion *> &calledSuccessors() const;

    // the called fork is a special kind of successor, as it
    // is not a thread region, but a node instead; it signifies
    // a fork which may be invoked at the end of this region
    // without certainly being joined
    const std::vector<const ThreadRegion *> &forkedSuccessors() const;

    // ROOM FOR IMPROVEMENT: there can be fewer regions if I consider all CALL
    // successors of a CALL_PTR node to be part of the same region; in that
    // case, the number of interesting call successors can be greater than one

    // returns the entry region of a procedure called in the last
    // node of this region if the call is interesting (calls a fork recursively)
    const ThreadRegion *interestingCallSuccessor() const;

    bool insertNode(const Node *node);

    // the first node dominates all nodes in the region
    const Node *firstNode() const;

    // the last node post dominates all nodes in the region
    const Node *lastNode() const;

    void printNodes(std::ostream &ostream);
    void printEdges(std::ostream &ostream);

    std::string dotName() const;

    /**
     * @brief returns all llvm instructions contained in the thread region
     * @return
     */
    std::set<const llvm::Instruction *> llvmInstructions() const;
};

#endif // THREADREGION_H
