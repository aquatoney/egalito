#include "inferlinks.h"
#include "chunk/dump.h"
#include "disasm/makesemantic.h"
#include "log/log.h"

void InferLinksPass::visit(Module *module) {
    this->module = module;
    recurse(module);
}

void InferLinksPass::visit(Instruction *instruction) {
    auto semantic = instruction->getSemantic();
    if(auto v = dynamic_cast<DisassembledInstruction *>(semantic)) {
        if(v->getLink()) return;
        auto assembly = v->getAssembly();
        if(!assembly) return;

#ifdef ARCH_X86_64
        // see if this instruction has any operands that need links
        // (can return NULL if not)
        auto linked = LinkedInstruction::makeLinked(module, instruction, assembly);
        if(linked) {
            instruction->setSemantic(linked);
            delete v;
        }
#elif defined(ARCH_AARCH64)
        auto linked = LinkedInstruction::makeLinked(module, instruction, assembly);
        if(linked) {
            instruction->setSemantic(linked);
            delete v;
        }
#endif
    }
}