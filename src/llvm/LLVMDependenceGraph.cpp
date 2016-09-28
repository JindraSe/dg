/// XXX add licence
//

#ifndef HAVE_LLVM
# error "Need LLVM for LLVMDependenceGraph"
#endif

#ifndef ENABLE_CFG
 #error "Need CFG enabled for building LLVM Dependence Graph"
#endif

#include <utility>
#include <unordered_map>
#include <set>

#include <llvm/Config/llvm-config.h>
#if (LLVM_VERSION_MINOR < 5)
 #include <llvm/Support/CFG.h>
#else
 #include <llvm/IR/CFG.h>
#endif

#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/Support/raw_ostream.h>

#include "LLVMDGVerifier.h"
#include "LLVMDependenceGraph.h"
#include "LLVMNode.h"
#include "Utils.h"
#include "llvm-debug.h"

#include "llvm/analysis/PointsTo/PointsTo.h"
#include "llvm/analysis/ControlExpression.h"

using llvm::errs;
using std::make_pair;

namespace dg {

namespace debug {
    // this is a wrapper for the cases when we do not have
    // LLVM compiled with debug information, thus
    // debugger do not know the dump() method
    void dumpllvm(const llvm::Value *val)
    {
        val->dump();
    }

    // gdb I currently have has problems with inheritance...
    void dumpllvm(const llvm::Instruction *Inst)
    {
        Inst->dump();
    }

    void dumpllvm(const llvm::Argument *Inst)
    {
        Inst->dump();
    }

    void dumpllvm(const llvm::CallInst *Inst)
    {
        Inst->dump();
    }

    void dumpllvm(const llvm::Function *Inst)
    {
        Inst->dump();
    }
}

/// ------------------------------------------------------------------
//  -- LLVMDependenceGraph
/// ------------------------------------------------------------------

// map of all constructed functions
std::map<llvm::Value *, LLVMDependenceGraph *> constructedFunctions;

const std::map<llvm::Value *,
               LLVMDependenceGraph *>& getConstructedFunctions()
{
    return constructedFunctions;
}

LLVMDependenceGraph::~LLVMDependenceGraph()
{
    // delete nodes
    for (auto I = begin(), E = end(); I != E; ++I) {
        LLVMNode *node = I->second;

        if (node) {
            for (auto subgraph : node->getSubgraphs()) {
                // graphs are referenced, once the refcount is 0
                // the graph will be deleted
                // Because of recursive calls, graph can be its
                // own subgraph. In that case we're in the destructor
                // already, so do not delete it
                    subgraph->unref(subgraph != this);
            }

            LLVMDGParameters *params = node->getParameters();
            if (params) {
                for (auto par : *params) {
                    delete par.second.in;
                    delete par.second.out;
                }

                delete params;
            }

            if (!node->getBBlock()
                && !llvm::isa<llvm::Function>(*I->first))
                DBG("WARN: Value " << *I->first << "had no BB assigned");

            delete node;
        } else {
            DBG("WARN: Value " << *I->first << "had no node assigned");
        }
    }

    // delete post-dominator tree root
    delete getPostDominatorTreeRoot();
}

static void addGlobals(llvm::Module *m, LLVMDependenceGraph *dg)
{
    for (auto I = m->global_begin(), E = m->global_end(); I != E; ++I)
        dg->addGlobalNode(new LLVMNode(&*I));
}

bool LLVMDependenceGraph::verify() const
{
    LLVMDGVerifier verifier(this);
    return verifier.verify();
}

bool LLVMDependenceGraph::build(llvm::Module *m, llvm::Function *entry)
{
    // get entry function if not given
    if (!entry)
        entry = m->getFunction("main");

    if (!entry) {
        errs() << "No entry function found/given\n";
        return false;
    }

    module = m;

    // add global nodes. These will be shared across subgraphs
    addGlobals(m, this);

    // build recursively DG from entry point
    build(entry);

    return true;
};

LLVMDependenceGraph *
LLVMDependenceGraph::buildSubgraph(LLVMNode *node)
{
    using namespace llvm;

    Value *val = node->getValue();
    CallInst *CInst = dyn_cast<CallInst>(val);
    assert(CInst && "buildSubgraph called on non-CallInst");
    Function *callFunc = CInst->getCalledFunction();

    return buildSubgraph(node, callFunc);
}

// FIXME don't duplicate the code
bool LLVMDependenceGraph::addFormalGlobal(llvm::Value *val)
{
    // add the same formal parameters
    LLVMDGParameters *params = getParameters();
    if (!params) {
        params = new LLVMDGParameters();
        setParameters(params);
    }

    // if we have this value, just return
    if (params->find(val))
        return false;

    LLVMNode *fpin = new LLVMNode(val);
    LLVMNode *fpout = new LLVMNode(val);
    fpin->setDG(this);
    fpout->setDG(this);
    params->addGlobal(val, fpin, fpout);

    LLVMNode *entry = getEntry();
    entry->addControlDependence(fpin);
    entry->addControlDependence(fpout);

    return true;
}

static bool addSubgraphGlobParams(LLVMDependenceGraph *parentdg,
                                  LLVMDGParameters *params)
{
    bool changed = false;
    for (auto it = params->global_begin(), et = params->global_end();
         it != et; ++it)
        changed |= parentdg->addFormalGlobal(it->first);

    // and add heap-allocated variables
    for (auto it : *params) {
        if (llvm::isa<llvm::CallInst>(it.first))
            changed |= parentdg->addFormalParameter(it.first);
    }

    return changed;
}

void LLVMDependenceGraph::addSubgraphGlobalParameters(LLVMDependenceGraph *subgraph)
{
    LLVMDGParameters *params = subgraph->getParameters();
    if (!params)
        return;

    // if we do not change anything, it means that the graph
    // has these params already, and so must the callers of the graph
    if (!addSubgraphGlobParams(this, params))
        return;

    // recursively add the formal parameters to all callers
    for (LLVMNode *callsite : this->getCallers()) {
        LLVMDependenceGraph *graph = callsite->getDG();
        graph->addSubgraphGlobalParameters(this);

        // update the actual parameters of the call-site
        callsite->addActualParameters(this);
    }
}

LLVMDependenceGraph *
LLVMDependenceGraph::buildSubgraph(LLVMNode *node, llvm::Function *callFunc)
{
    using namespace llvm;

    LLVMBBlock *BB;

    // if we don't have this subgraph constructed, construct it
    // else just add call edge
    LLVMDependenceGraph *&subgraph = constructedFunctions[callFunc];
    if (!subgraph) {
        // since we have reference the the pointer in
        // constructedFunctions, we can assing to it
        subgraph = new LLVMDependenceGraph();
        // set global nodes to this one, so that
        // we'll share them
        subgraph->setGlobalNodes(getGlobalNodes());
        subgraph->module = module;
        subgraph->PTA = PTA;
        // make subgraphs gather the call-sites too
        subgraph->gatherCallsites(gather_callsites, gatheredCallsites);

        // make the real work
        bool ret = subgraph->build(callFunc);

        // at least for now use just assert, if we'll
        // have a reason to handle such failures at some
        // point, we can change it
        assert(ret && "Building subgraph failed");

        // we built the subgraph, so it has refcount = 1,
        // later in the code we call addSubgraph, which
        // increases the refcount to 2, but we need this
        // subgraph to has refcount 1, so unref it
        subgraph->unref(false /* deleteOnZero */);
    }

    BB = node->getBBlock();
    assert(BB && "do not have BB; this is a bug, sir");
    BB->addCallsite(node);

    // add control dependence from call node
    // to entry node
    node->addControlDependence(subgraph->getEntry());

    // add globals that are used in subgraphs
    // it is necessary if this subgraph was creating due to function
    // pointer call
    addSubgraphGlobalParameters(subgraph);
    node->addActualParameters(subgraph, callFunc);

    return subgraph;
}

static bool
is_func_defined(const llvm::Function *func)
{
    if (!func || func->size() == 0)
        return false;

    return true;
}

bool LLVMDependenceGraph::addFormalParameter(llvm::Value *val)
{
    // add the same formal parameters
    LLVMDGParameters *params = getParameters();
    if (!params) {
        params = new LLVMDGParameters();
        setParameters(params);
    }

    // if we have this value, just return
    if (params->find(val))
        return false;

    LLVMNode *fpin = new LLVMNode(val);
    LLVMNode *fpout = new LLVMNode(val);
    fpin->setDG(this);
    fpout->setDG(this);
    params->add(val, fpin, fpout);

    LLVMNode *entry = getEntry();
    entry->addControlDependence(fpin);
    entry->addControlDependence(fpout);

    return true;
}

// FIXME copied from PointsTo.cpp, don't duplicate,
// add it to analysis generic
static bool isMemAllocationFunc(const llvm::Function *func)
{
    if (!func || !func->hasName())
        return false;

    const llvm::StringRef& name = func->getName();
    if (name.equals("malloc") || name.equals("calloc") || name.equals("realloc"))
        return true;

    return false;
}

// FIXME: factor this out, we use it even in PoinsTo
static bool callIsCompatible(const llvm::Function *F, const llvm::CallInst *CI)
{
    using namespace llvm;

    if (F->arg_size() > CI->getNumArgOperands())
        return false;

    if (!F->getReturnType()->canLosslesslyBitCastTo(CI->getType()))
        return false;

    int idx = 0;
    for (auto A = F->arg_begin(), E = F->arg_end(); A != E; ++A, ++idx) {
        Type *CTy = CI->getArgOperand(idx)->getType();
        Type *ATy = A->getType();

        // FIXME: we could check that the types are equal!
        if (!CTy->canLosslesslyBitCastTo(ATy))
            return false;
    }

    return true;
}



void LLVMDependenceGraph::handleInstruction(llvm::Value *val,
                                            LLVMNode *node)
{
    using namespace llvm;

    if (CallInst *CInst = dyn_cast<CallInst>(val)) {
        Value *strippedValue = CInst->getCalledValue()->stripPointerCasts();
        Function *func = dyn_cast<Function>(strippedValue);
        // if func is nullptr, then this is indirect call
        // via function pointer. If we have the points-to information,
        // create the subgraph
        if (!func && PTA) {
            using namespace analysis::pta;
            PSNode *op = PTA->getNode(val);
            for (const Pointer& ptr : op->pointsTo) {
                if (!ptr.isValid()) {
                    continue;
                }

                // vararg may introduce imprecision here, so we
                // must check that it is really pointer to a function
                if (!isa<Function>(ptr.target->getUserData<Value>()))
                    continue;

                Function *F = ptr.target->getUserData<Function>();
                if (F->size() == 0 || !callIsCompatible(F, CInst))
                    // incompatible prototypes or the function
                    // is only declaration
                    continue;

                LLVMDependenceGraph *subg = buildSubgraph(node, F);
                node->addSubgraph(subg);
            }
        }

        if (func && gather_callsites &&
            strcmp(func->getName().data(), gather_callsites) == 0) {
            gatheredCallsites->insert(node);
        }

        if (is_func_defined(func)) {
            LLVMDependenceGraph *subg = buildSubgraph(node, func);
            node->addSubgraph(subg);
        }

        // if we allocate a memory in a function, we can pass
        // it to other functions, so it is like global.
        // We need it as parameter, so that if we define it,
        // we can add def-use edges from parent, through the parameter
        // to the definition
        if (isMemAllocationFunc(CInst->getCalledFunction()))
                addFormalParameter(val);

        // no matter what is the function, this is a CallInst,
        // so create call-graph
        addCallNode(node);
    } else if (Instruction *Inst = dyn_cast<Instruction>(val)) {
        if (isa<LoadInst>(val) || isa<GetElementPtrInst>(val)) {
            Value *op = Inst->getOperand(0)->stripInBoundsOffsets();
             if (isa<GlobalVariable>(op))
                 addFormalGlobal(op);
        } else if (isa<StoreInst>(val)) {
            Value *op = Inst->getOperand(0)->stripInBoundsOffsets();
            if (isa<GlobalVariable>(op))
                addFormalGlobal(op);

            op = Inst->getOperand(1)->stripInBoundsOffsets();
            if (isa<GlobalVariable>(op))
                addFormalGlobal(op);
        }
    }
}

LLVMBBlock *LLVMDependenceGraph::build(llvm::BasicBlock& llvmBB)
{
    using namespace llvm;

    LLVMBBlock *BB = new LLVMBBlock();
    LLVMNode *node = nullptr;

    BB->setKey(&llvmBB);

    // iterate over the instruction and create node for every single one of them
    for (Instruction& Inst : llvmBB) {
        Value *val = &Inst;
        node = new LLVMNode(val);

        // add new node to this dependence graph
        addNode(node);

        // add the node to our basic block
        BB->append(node);

        // take instruction specific actions
        handleInstruction(val, node);
    }

    // check if this is the exit node of function
    // (node is now the last instruction in this BB)
    // if it is, connect it to one artificial return node
    Value *termval = node->getValue();
    if (isa<ReturnInst>(termval)) {
        // create one unified exit node from function and add control dependence
        // to it from every return instruction. We could use llvm pass that
        // would do it for us, but then we would lost the advantage of working
        // on dep. graph that is not for whole llvm
        LLVMNode *ext = getExit();
        if (!ext) {
            // we need new llvm value, so that the nodes won't collide
            ReturnInst *phonyRet
                = ReturnInst::Create(termval->getContext());
            if (!phonyRet) {
                errs() << "ERR: Failed creating phony return value "
                       << "for exit node\n";
                // XXX later we could return somehow more mercifully
                abort();
            }

            ext = new LLVMNode(phonyRet);
            addNode(ext);
            setExit(ext);

            LLVMBBlock *retBB = new LLVMBBlock(ext);
            setExitBB(retBB);
        }

        // add control dependence from this (return) node to EXIT node
        assert(node && "BUG, no node after we went through basic block");
        node->addControlDependence(ext);
        // 255 is maximum value of uint8_t which is the type of the label
        // of the edge
        BB->addSuccessor(getExitBB(), 255);
    }

    // sanity check if we have the first and the last node set
    assert(BB->getFirstNode() && "No first node in BB");
    assert(BB->getLastNode() && "No last node in BB");

    return BB;
}

static void createSingleExitBB(LLVMDependenceGraph *graph)
{
    llvm::UnreachableInst *ui
        = new llvm::UnreachableInst(graph->getModule()->getContext());
    LLVMNode *exit = new LLVMNode(ui, true);
    graph->addNode(exit);
    graph->setExit(exit);
    LLVMBBlock *exitBB = new LLVMBBlock(exit);
    exitBB->setDG(graph);
    graph->setExitBB(exitBB);

    // XXX should we add predecessors? If the function does not
    // return anything, we don't need propagate anything outside...
}

static void
addControlDepsToPHI(LLVMDependenceGraph *graph,
                    LLVMNode *node, const llvm::PHINode *phi)
{
    using namespace llvm;

    const BasicBlock *this_block = phi->getParent();
    auto CB = graph->getBlocks();

    for (auto I = phi->block_begin(), E = phi->block_end(); I != E; ++I) {
        BasicBlock *B = *I;

        if (B == this_block)
            continue;

        LLVMBBlock *our = CB[B];
        assert(our && "Don't have block constructed for PHI node");
        our->getLastNode()->addControlDependence(node);
    }
}

static void
addControlDepsToPHIs(LLVMDependenceGraph *graph)
{
    // some phi nodes just work like this
    //
    //  ; <label>:0
    //  %1 = load i32, i32* %a, align 4
    //  %2 = load i32, i32* %b, align 4
    //  %3 = icmp sgt i32 %1, %2
    //  br i1 %3, label %4, label %5
    //
    //  ; <label>:4                                       ; preds = %0
    //  br label %6
    //
    //  ; <label>:5                                       ; preds = %0
    //  br label %6
    //
    //  ; <label>:6                                       ; preds = %5, %4
    //  %p.0 = phi i32* [ %a, %4 ], [ %b, %5 ]
    //
    //  so we need to keep the blocks %5 and %6 even though it is empty

    // add control dependence to each block going to this phi
    // XXX: it is over-approximation, but we don't have nothing better now
    for (auto I = graph->begin(), E = graph->end(); I != E; ++I) {
        llvm::Value *val = I->first;
        if (llvm::PHINode *phi = llvm::dyn_cast<llvm::PHINode>(val)) {
            addControlDepsToPHI(graph, I->second, phi);
        }
    }
}

bool LLVMDependenceGraph::build(llvm::Function *func)
{
    using namespace llvm;

    assert(func && "Passed no func");

    // do we have anything to process?
    if (func->size() == 0)
        return false;

    // create entry node
    LLVMNode *entry = new LLVMNode(func);
    addGlobalNode(entry);
    // we want the entry node to have this DG set
    entry->setDG(this);
    setEntry(entry);
    BBlocksMapT& blocks = getBlocks();

    constructedFunctions.insert(make_pair(func, this));

    // add formal parameters to this graph
    addFormalParameters();

    // iterate over basic blocks
    for (llvm::BasicBlock& llvmBB : *func) {
        LLVMBBlock *BB = build(llvmBB);
        blocks[&llvmBB] = BB;

        // first basic block is the entry BB
        if (!getEntryBB())
            setEntryBB(BB);
    }

    // add CFG edges
    for (auto it : blocks) {
        BasicBlock *llvmBB = cast<BasicBlock>(it.first);
        LLVMBBlock *BB = it.second;
        BB->setDG(this);

        int idx = 0;
        for (succ_iterator S = succ_begin(llvmBB), SE = succ_end(llvmBB);
             S != SE; ++S) {
            LLVMBBlock *succ = blocks[*S];
            assert(succ && "Missing basic block");

            // don't let overflow the labels silently
            // if this ever happens, we need to change bit-size
            // of the label (255 is reserved for edge to
            // artificial single return value)
            if (idx >= 255) {
                errs() << "Too much of successors";
                abort();
            }

            BB->addSuccessor(succ, idx++);
        }
    }

    // if graph has no return inst, just create artificial exit node
    // and point there
    if (!getExit())
        createSingleExitBB(this);

    // check if we have everything
    assert(getEntry() && "Missing entry node");
    assert(getExit() && "Missing exit node");
    assert(getEntryBB() && "Missing entry BB");
    assert(getExitBB() && "Missing exit BB");

    addControlDepsToPHIs(this);

    // add CFG edge from entry point to the first instruction
    entry->addControlDependence(getEntryBB()->getFirstNode());

    return true;
}

bool LLVMDependenceGraph::build(llvm::Module *m,
                                LLVMPointerAnalysis *pts,
                                llvm::Function *entry)
{
    this->PTA = pts;
    return build(m, entry);
}

void LLVMDependenceGraph::addFormalParameters()
{
    using namespace llvm;

    LLVMNode *entryNode = getEntry();
    assert(entryNode && "No entry node when adding formal parameters");

    Function *func = dyn_cast<Function>(entryNode->getValue());
    assert(func && "entry node value is not a function");
    //assert(func->arg_size() != 0 && "This function is undefined?");
    if (func->arg_size() == 0)
        return;

    LLVMDGParameters *params = getParameters();
    if (!params) {
        params = new LLVMDGParameters();
        setParameters(params);
    }

    LLVMNode *in, *out;
    for (auto I = func->arg_begin(), E = func->arg_end();
         I != E; ++I) {
        Value *val = (&*I);

        in = new LLVMNode(val);
        out = new LLVMNode(val);
        in->setDG(this);
        out->setDG(this);
        params->add(val, in, out);

        // add control edges
        entryNode->addControlDependence(in);
        entryNode->addControlDependence(out);
    }

    if (func->isVarArg()) {
        Value *val = ConstantPointerNull::get(func->getType());
        val->setName("vararg");
        in = new LLVMNode(val, true);
        out = new LLVMNode(val, true);
        in->setDG(this);
        out->setDG(this);

        params->setVarArg(in, out);
        entryNode->addControlDependence(in);
        entryNode->addControlDependence(out);
        in->addDataDependence(out);
    }
}

static bool array_match(llvm::StringRef name, const char *names[])
{
    unsigned idx = 0;
    while(names[idx]) {
        if (name.equals(names[idx]))
            return true;
        ++idx;
    }

    return false;
}

static bool match_callsite_name(LLVMNode *callNode, const char *names[])
{
    using namespace llvm;

    // if the function is undefined, it has no subgraphs,
    // but is not called via function pointer
    if (!callNode->hasSubgraphs()) {
        const CallInst *callInst = cast<CallInst>(callNode->getValue());
        const Value *calledValue = callInst->getCalledValue();
        const Function *func = dyn_cast<Function>(calledValue->stripPointerCasts());
        // in the case we haven't run points-to analysis
        if (!func)
            return false;

        return array_match(func->getName(), names);
    } else {
        // simply iterate over the subgraphs, get the entry node
        // and check it
        for (LLVMDependenceGraph *dg : callNode->getSubgraphs()) {
            LLVMNode *entry = dg->getEntry();
            assert(entry && "No entry node in graph");

            const Function *func
                = cast<Function>(entry->getValue()->stripPointerCasts());
            return array_match(func->getName(), names);
        }
    }

    return false;
}

bool LLVMDependenceGraph::getCallSites(const char *name, std::set<LLVMNode *> *callsites)
{
    const char *names[] = {name, NULL};
    return getCallSites(names, callsites);
}

bool LLVMDependenceGraph::getCallSites(const char *names[],
                                       std::set<LLVMNode *> *callsites)
{
    for (auto F : constructedFunctions) {
        for (auto I : F.second->getBlocks()) {
            LLVMBBlock *BB = I.second;
            for (LLVMNode *n : BB->getNodes()) {
                if (llvm::isa<llvm::CallInst>(n->getValue())) {
                    if (match_callsite_name(n, names))
                        callsites->insert(n);
                }
            }
        }
    }

    return callsites->size() != 0;
}

void LLVMDependenceGraph::computeControlExpression(bool addCDs)
{
    LLVMCFABuilder builder;

    for (auto F : getConstructedFunctions()) {
        llvm::Function *func = llvm::cast<llvm::Function>(F.first);
        LLVMCFA cfa = builder.build(*func);

        CE = cfa.compute();

        if (addCDs) {
            // compute the control scope
            CE.computeSets();
            auto our_blocks = F.second->getBlocks();

            for (llvm::BasicBlock& B : *func) {
                LLVMBBlock *B1 = our_blocks[&B];

                // if this block is a predicate block,
                // we compute the control deps for it
                // XXX: for now we compute the control
                // scope, which is enough for slicing,
                // but may add some extra (transitive)
                // edges
                if (B.getTerminator()->getNumSuccessors() > 1) {
                    auto CS = CE.getControlScope(&B);
                    for (auto cs : CS) {
                        assert(cs->isa(LABEL));
                        auto lab = static_cast<CELabel<llvm::BasicBlock *> *>(cs);
                        LLVMBBlock *B2 = our_blocks[lab->getLabel()];
                        B1->addControlDependence(B2);
                    }
                }
            }
        }
    }
}

// the original algorithm from Ferrante & Ottenstein
// works with nodes that represent instructions, therefore
// there's no point in control dependence self-loops.
// However, we use basic blocks and having a 'node' control
// dependent on itself may be desired. If a block jumps
// on itself, the decision whether we get to that block (again)
// is made on that block - so we want to make it control dependent
// on itself.
void LLVMDependenceGraph::makeSelfLoopsControlDependent()
{
    for (auto F : getConstructedFunctions()) {
        auto blocks = F.second->getBlocks();

        for (auto it : blocks) {
            LLVMBBlock *B = it.second;

            if (B->successorsNum() > 1 && B->hasSelfLoop())
                // add self-loop control dependence
                B->addControlDependence(B);
        }
    }
}

} // namespace dg
