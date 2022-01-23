//===-- ClangDocMain.cpp - ClangDoc -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tool for generating C and C++ documenation from source code
// and comments. Generally, it runs a LibTooling FrontendAction on source files,
// mapping each declaration in those files to its USR and serializing relevant
// information into LLVM bitcode. It then runs a pass over the collected
// declaration information, reducing by USR. There is an option to dump this
// intermediate result to bitcode. Finally, it hands the reduced information
// off to a generator, which does the final parsing from the intermediate
// representation to the desired output format.
//
//===----------------------------------------------------------------------===//

#include "BitcodeReader.h"
#include "BitcodeWriter.h"
#include "ClangDoc.h"
#include "Generators.h"
#include "Representation.h"
#include "clang/AST/AST.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchersInternal.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Execution.h"
#include "clang/Tooling/StandaloneExecution.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace clang;

static llvm::cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static llvm::cl::OptionCategory ClangDocCategory("clang-doc options");

static llvm::cl::opt<std::string>
    OutDirectory("output",
                 llvm::cl::desc("Directory for outputting generated files."),
                 llvm::cl::init("docs"), llvm::cl::cat(ClangDocCategory));

static llvm::cl::opt<bool>
    DumpMapperResult("dump-mapper",
                     llvm::cl::desc("Dump mapper results to bitcode file."),
                     llvm::cl::init(false), llvm::cl::cat(ClangDocCategory));

static llvm::cl::opt<bool> DumpIntermediateResult(
    "dump-intermediate",
    llvm::cl::desc("Dump intermediate results to bitcode file."),
    llvm::cl::init(false), llvm::cl::cat(ClangDocCategory));

static llvm::cl::opt<bool>
    PublicOnly("public", llvm::cl::desc("Document only public declarations."),
               llvm::cl::init(false), llvm::cl::cat(ClangDocCategory));

enum OutputFormatTy {
  yaml,
};

static llvm::cl::opt<OutputFormatTy> FormatEnum(
    "format", llvm::cl::desc("Format for outputted docs."),
    llvm::cl::values(clEnumVal(yaml, "Documentation in YAML format.")),
    llvm::cl::init(yaml), llvm::cl::cat(ClangDocCategory));

static llvm::cl::opt<bool> DoxygenOnly(
    "doxygen",
    llvm::cl::desc("Use only doxygen-style comments to generate docs."),
    llvm::cl::init(false), llvm::cl::cat(ClangDocCategory));

bool CreateDirectory(const Twine &DirName, bool ClearDirectory = false) {
  std::error_code OK;
  llvm::SmallString<128> DocsRootPath;
  if (ClearDirectory) {
    std::error_code RemoveStatus = llvm::sys::fs::remove_directories(DirName);
    if (RemoveStatus != OK) {
      llvm::errs() << "Unable to remove existing documentation directory for "
                   << DirName << ".\n";
      return true;
    }
  }
  std::error_code DirectoryStatus = llvm::sys::fs::create_directories(DirName);
  if (DirectoryStatus != OK) {
    llvm::errs() << "Unable to create documentation directories.\n";
    return true;
  }
  return false;
}

bool DumpResultToFile(const Twine &DirName, const Twine &FileName,
                      StringRef Buffer, bool ClearDirectory = false) {
  std::error_code OK;
  llvm::SmallString<128> IRRootPath;
  llvm::sys::path::native(OutDirectory, IRRootPath);
  llvm::sys::path::append(IRRootPath, DirName);
  if (CreateDirectory(IRRootPath, ClearDirectory))
    return true;
  llvm::sys::path::append(IRRootPath, FileName);
  std::error_code OutErrorInfo;
  llvm::raw_fd_ostream OS(IRRootPath, OutErrorInfo, llvm::sys::fs::F_None);
  if (OutErrorInfo != OK) {
    llvm::errs() << "Error opening documentation file.\n";
    return true;
  }
  OS << Buffer;
  OS.close();
  return false;
}

llvm::Expected<llvm::SmallString<128>>
getPath(StringRef Root, StringRef Ext, StringRef Name,
        llvm::SmallVectorImpl<doc::Reference> &Namespaces) {
  std::error_code OK;
  llvm::SmallString<128> Path;
  llvm::sys::path::native(Root, Path);
  for (auto R = Namespaces.rbegin(), E = Namespaces.rend(); R != E; ++R)
    llvm::sys::path::append(Path, R->Name);

  if (CreateDirectory(Path))
    return llvm::make_error<llvm::StringError>("Unable to create directory.\n",
                                               llvm::inconvertibleErrorCode());

  llvm::sys::path::append(Path, Name + Ext);
  return Path;
}

std::string getFormatString(OutputFormatTy Ty) {
  switch (Ty) {
  case yaml:
    return "yaml";
  }
  llvm_unreachable("Unknown OutputFormatTy");
}

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  std::error_code OK;

  // Fail early if an invalid format was provided.
  std::string Format = getFormatString(FormatEnum);
  auto G = doc::findGeneratorByName(Format);
  if (!G) {
    llvm::errs() << toString(G.takeError()) << "\n";
    return 1;
  }

  auto Exec = clang::tooling::createExecutorFromCommandLineArgs(
      argc, argv, ClangDocCategory);

  if (!Exec) {
    llvm::errs() << toString(Exec.takeError()) << "\n";
    return 1;
  }

  ArgumentsAdjuster ArgAdjuster;
  if (!DoxygenOnly)
    ArgAdjuster = combineAdjusters(
        getInsertArgumentAdjuster("-fparse-all-comments",
                                  tooling::ArgumentInsertPosition::END),
        ArgAdjuster);

  // Mapping phase
  llvm::outs() << "Mapping decls...\n";
  clang::doc::ClangDocContext CDCtx = {Exec->get()->getExecutionContext(),
                                       PublicOnly};
  auto Err =
      Exec->get()->execute(doc::newMapperActionFactory(CDCtx), ArgAdjuster);
  if (Err) {
    llvm::errs() << toString(std::move(Err)) << "\n";
    return 1;
  }

  if (DumpMapperResult) {
    bool Err = false;
    Exec->get()->getToolResults()->forEachResult(
        [&](StringRef Key, StringRef Value) {
          Err = DumpResultToFile("bc", Key + ".bc", Value);
        });
    if (Err)
      llvm::errs() << "Error dumping map results.\n";
    return Err;
  }

  // Collect values into output by key.
  llvm::outs() << "Collecting infos...\n";
  llvm::StringMap<std::vector<std::unique_ptr<doc::Info>>> MapOutput;

  // In ToolResults, the Key is the hashed USR and the value is the
  // bitcode-encoded representation of the Info object.
  Exec->get()->getToolResults()->forEachResult([&](StringRef Key,
                                                   StringRef Value) {
    llvm::BitstreamCursor Stream(Value);
    doc::ClangDocBitcodeReader Reader(Stream);
    auto Infos = Reader.readBitcode();
    for (auto &I : Infos) {
      auto R =
          MapOutput.try_emplace(Key, std::vector<std::unique_ptr<doc::Info>>());
      R.first->second.emplace_back(std::move(I));
    }
  });

  // Reducing and generation phases
  llvm::outs() << "Reducing " << MapOutput.size() << " infos...\n";
  llvm::StringMap<std::unique_ptr<doc::Info>> ReduceOutput;
  for (auto &Group : MapOutput) {
    auto Reduced = doc::mergeInfos(Group.getValue());
    if (!Reduced)
      llvm::errs() << llvm::toString(Reduced.takeError());

    if (DumpIntermediateResult) {
      SmallString<4096> Buffer;
      llvm::BitstreamWriter Stream(Buffer);
      doc::ClangDocBitcodeWriter Writer(Stream);
      Writer.dispatchInfoForWrite(Reduced.get().get());
      if (DumpResultToFile("bc", Group.getKey() + ".bc", Buffer))
        llvm::errs() << "Error dumping to bitcode.\n";
      continue;
    }

    // Create the relevant ostream and emit the documentation for this decl.
    doc::Info *I = Reduced.get().get();
    auto InfoPath = getPath(OutDirectory, "." + Format, I->Name, I->Namespace);
    if (!InfoPath) {
      llvm::errs() << toString(InfoPath.takeError()) << "\n";
      continue;
    }
    std::error_code FileErr;
    llvm::raw_fd_ostream InfoOS(InfoPath.get(), FileErr, llvm::sys::fs::F_None);
    if (FileErr != OK) {
      llvm::errs() << "Error opening index file: " << FileErr.message() << "\n";
      continue;
    }

    if (G->get()->generateDocForInfo(I, InfoOS))
      llvm::errs() << "Unable to generate docs for info.\n";
  }

  return 0;
}
