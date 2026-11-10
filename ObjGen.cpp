#include "ObjGen.h"

#include "Backend.h"
#include "utils.h"

#include <llvm/Analysis/RuntimeLibcallInfo.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/InitializePasses.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Pass.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include "utils.h"

#include <memory>
#include <optional>

void ObjGen(CodeGenContext& context, const std::string& filename) {
    // 前面已经报过错就别再生成目标文件了，免得产出一个半成品
    if (ErrorCount() > 0) {
        llvm::errs() << "error: 有 " << ErrorCount() << " 处错误，跳过目标代码生成\n";
        return;
    }

    // 只注册本机目标，这样链接时不用把所有后端都拖进来。
    // 注意：InitializeNativeTarget() 同时也会把 X86 的各个 pass 注册到
    // legacy PassRegistry 里，后面的代码生成要用到它们。
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // 注册与目标无关的代码生成 pass
    llvm::PassRegistry* registry = llvm::PassRegistry::getPassRegistry();
    llvm::initializeCore(*registry);
    llvm::initializeCodeGen(*registry);

    // 用当前机器的目标三元组
    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    context.theModule->setTargetTriple(triple);

    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, error);
    if (!target) {
        llvm::errs() << "error: 找不到目标平台 " << triple.str() << ": " << error << "\n";
        return;
    }

    llvm::TargetOptions options;
    std::optional<llvm::Reloc::Model> relocModel;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
        triple, "generic", "", options, relocModel));
    if (!machine) {
        llvm::errs() << "error: 无法创建 TargetMachine\n";
        return;
    }
    context.theModule->setDataLayout(machine->createDataLayout());

    // 先检查 IR 合法性，出问题早点报错
    if (llvm::verifyModule(*context.theModule, &llvm::errs())) {
        LogError("生成的 LLVM IR 不合法");
        return;
    }

    std::error_code ec;
    llvm::raw_fd_ostream out(filename, ec, llvm::sys::fs::OF_None);
    if (ec) {
        llvm::errs() << "error: 无法写入 " << filename << ": " << ec.message() << "\n";
        return;
    }

    // ------------------------------------------------------------------
    // 代码生成流水线
    //
    // 这里用的是 LLVM 的 legacy PassManager，和 llc 工具默认的做法一致。
    // （LLVM 22 里虽然新增了基于新 PassManager 的 buildCodeGenPipeline，
    //   但 X86 后端的 AsmPrinter 还没移植过去，走新 PassManager 会生成不出
    //   目标文件，所以仍然用 legacy 方式。）
    //
    // 流程：
    //   1. 准备两个必要的 pass：TargetLibraryInfo 和 RuntimeLibraryInfo
    //   2. addPassesToEmitFile 把「选择指令 -> 寄存器分配 -> 汇编输出」
    //      这一整串 pass 加进来
    //   3. run() 真正跑一遍，产出目标文件
    // ------------------------------------------------------------------
    llvm::TargetLibraryInfoImpl targetLibraryInfo(triple, machine->Options.VecLib);
    llvm::MachineModuleInfoWrapperPass* machineModuleInfo =
        new llvm::MachineModuleInfoWrapperPass(machine.get());

    llvm::legacy::PassManager passManager;
    passManager.add(new llvm::TargetLibraryInfoWrapperPass(targetLibraryInfo));
    passManager.add(new llvm::RuntimeLibraryInfoWrapper(
        triple, machine->Options.ExceptionModel, machine->Options.FloatABIType,
        machine->Options.EABIVersion, machine->Options.MCOptions.ABIName,
        machine->Options.VecLib));

    if (machine->addPassesToEmitFile(passManager, out, nullptr,
                                     llvm::CodeGenFileType::ObjectFile, true,
                                     machineModuleInfo)) {
        llvm::errs() << "error: 当前目标不支持生成这种目标文件\n";
        return;
    }

    passManager.run(*context.theModule);

    out.flush();
    llvm::outs() << "目标文件已写入 " << filename << "\n";
    llvm::outs().flush();
}
