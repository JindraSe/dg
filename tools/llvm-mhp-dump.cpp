#include "dg/llvm/PointerAnalysis/PointerAnalysis.h"
#include "dg/llvm/ThreadRegions/ControlFlowGraph.h"
#include "dg/llvm/ThreadRegions/MayHappenInParallel.h"
#include "dg/llvm/ThreadRegions/ThreadRegion.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_os_ostream.h>

#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Bitcode/BitcodeReader.h>
#else
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include <fstream>
#include <functional>
#include <iostream>
#include <ostream>
#include <string>

enum class OutputFormat { dot, text, csv };

struct CommandLineOptions {
    std::string inputFile;
    std::string outputFile;
    OutputFormat outputFormat;

    std::ofstream outputFileStream;

    CommandLineOptions(std::string &inputFile, std::string &outputFile,
                       OutputFormat outputFormat)
            : inputFile(inputFile), outputFile(outputFile),
              outputFormat(outputFormat),
              outputFileStream(
                      (outputFile.empty() ? std::ofstream()
                       : (outputFormat == OutputFormat::csv)
                               ? std::ofstream(outputFile, std::ios_base::app)
                               : std::ofstream(outputFile))) {}

    std::ostream &getOstream() {
        if (outputFile.empty()) {
            return std::cout;
        }

        return outputFileStream;
    }
};

int main(int argc, char *argv[]) {
    using namespace llvm;

    cl::opt<std::string> outputFile("o", cl::desc("Specify output filename"),
                                    cl::value_desc("filename"), cl::init(""));

    cl::opt<std::string> inputFile(cl::Positional, cl::Required,
                                   cl::desc("<input file>"), cl::init(""));

    cl::opt<OutputFormat> outputFormat(
            "of",
            llvm::cl::desc("Specify which output format you want this tool to "
                           "generate"),
            llvm::cl::values(
                    clEnumValN(OutputFormat::dot, "dot",
                               "generates a graphviz graph"),
                    clEnumValN(OutputFormat::text, "text",
                               "print statistics about regions and MHP"),
                    clEnumValN(OutputFormat::csv, "csv",
                               "append MHP statistics to a CSV file")),
            cl::init(OutputFormat::dot));

    cl::ParseCommandLineOptions(argc, argv);

    auto opts = CommandLineOptions(inputFile, outputFile, outputFormat);

    llvm::LLVMContext context;
    llvm::SMDiagnostic SMD;

    std::unique_ptr<Module> M = llvm::parseIRFile(opts.inputFile, SMD, context);

    if (!M) {
        llvm::errs() << "Failed parsing '" << opts.inputFile << "' file:\n";
        SMD.print(argv[0], errs());
        return 1;
    }

    dg::DGLLVMPointerAnalysis pointsToAnalysis(M.get(), "main",
                                               dg::Offset::UNKNOWN, true);
    pointsToAnalysis.run();

    ControlFlowGraph controlFlowGraph(&pointsToAnalysis);
    controlFlowGraph.buildFunction(M->getFunction("main"));

    MayHappenInParallel mhp =
            MayHappenInParallel(controlFlowGraph.mainEntryRegion());
    mhp.run();

    std::ostream &stream = opts.getOstream();

    if (opts.outputFormat == OutputFormat::dot) {
        controlFlowGraph.printWithRegions(stream, &mhp);
        return 0;
    }

    std::set<ThreadRegion *> regions = controlFlowGraph.allRegions();

    size_t regionCount = regions.size();
    size_t maximumRelationCount = regionCount * (regionCount + 1) / 2;
    size_t actualRelationCount = mhp.countRelations();
    double percentageRemoved = 100.0 - 100.0 * (double) actualRelationCount /
                                               (double) maximumRelationCount;

    std::map<const ThreadRegion *, size_t> regionInstructionCount;
    size_t instructionCount = 0;
    size_t actualInstructionRelationCount = 0;

    for (auto *region : regions) {
        regionInstructionCount[region] = region->llvmInstructions().size();
        instructionCount += regionInstructionCount[region];
    }

    size_t maximumInstructionRelationCount =
            instructionCount * (instructionCount + 1) / 2;

    for (auto *region : regions) {
        for (const auto *mhpRegion : mhp.parallelRegions(region)) {
            if (region < mhpRegion) {
                continue;
            }

            if (region == mhpRegion) {
                actualInstructionRelationCount +=
                        regionInstructionCount[region] *
                        (regionInstructionCount[region] + 1) / 2;
            } else {
                actualInstructionRelationCount +=
                        regionInstructionCount[mhpRegion] *
                        regionInstructionCount[region];
            }
        }
    }

    double percentageRemovedInstructions =
            100.0 - 100.0 * (double) actualInstructionRelationCount /
                            (double) maximumInstructionRelationCount;

    if (opts.outputFormat == OutputFormat::text) {
        stream << "Regions:\n"
               << "Regions constructed: " << regionCount << '\n'
               << "Maximum possible number of MHP relations between regions: "
               << maximumRelationCount << '\n'
               << "Number of MHP relations calculated: " << actualRelationCount
               << '\n'
               << "Percentage of MHP relations between regions removed: "
               << percentageRemoved << "\n\n"
               << "Instructions:\n"
               << "Number of instructions: " << instructionCount << '\n'
               << "Maximum possible number of MHP relations between "
                  "instructions: "
               << maximumInstructionRelationCount << '\n'
               << "Number of MHP relations between instructions calculated: "
               << actualInstructionRelationCount << '\n'
               << "Percentage of MHP relations between instructions removed: "
               << percentageRemovedInstructions << '\n';
    } else {
        stream << regionCount << ',' << maximumRelationCount << ','
               << actualRelationCount << ',' << percentageRemoved << ','
               << instructionCount << ',' << maximumInstructionRelationCount
               << ',' << actualInstructionRelationCount << ','
               << percentageRemovedInstructions << '\n';
    }

    return 0;
}
