#include <cstring>  // for memset
#include "profileinstrument.h"
#include "operation/addinline.h"
#include "operation/mutator.h"
#include "disasm/disassemble.h"
#include "chunk/module.h"
#include "instr/concrete.h"
#include "log/log.h"

void ProfileInstrumentPass::visit(Function *function) {
    if(function->getName() == "_init") return;
    if(function->getName() == "_fini") return;
    if(function->getName() == "__libc_csu_init") return;
    if(function->getName() == "__libc_csu_fini") return;

    auto module = static_cast<Module *>(function->getParent()->getParent());
    auto sectionPair = createDataSection(module);

    ChunkAddInline ai({}, [this, sectionPair, function] (unsigned int stackBytesAdded) {
        /*
           ff 05 f8 01 00 f0            incl   -0xffffe08(%rip)

           48 ff 05 f8 01 00 f0         incq   -0xffffe08(%rip)
        */

        //auto instr = Disassemble::instruction({0xff, 0x05, 0x00, 0x00, 0x00, 0x00});
        DisasmHandle handle(true);
        auto instr = new Instruction();
        auto sem = new LinkedInstruction(instr);
        sem->setAssembly(DisassembleInstruction(handle).makeAssemblyPtr(
            std::vector<unsigned char>{0x48, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00}));
        sem->setLink(addVariable(sectionPair.first, function));
        sem->setIndex(0);
        instr->setSemantic(sem);
        appendFunctionName(sectionPair.second, function->getName());

        return std::vector<Instruction *>{ instr };
    });
	auto block1 = function->getChildren()->getIterable()->get(0);
	auto instr1 = block1->getChildren()->getIterable()->get(0);
    ai.insertBefore(instr1, true);

    {
        ChunkMutator(function, true);
    }

	auto instr0 = block1->getChildren()->getIterable()->get(0);
    auto sem = static_cast<LinkedInstruction *>(instr0->getSemantic());
    sem->regenerateAssembly();
    LOG(0, "adding profiling to function [" << function->getName()
        << "] using global var " 
        << std::hex << sem->getLink()->getTargetAddress());
}

#define DATA_REGION_ADDRESS 0x30000000
#define DATA_NAMEREGION_ADDRESS 0x31000000
#define DATA_SECTION_NAME ".profiling"
#define DATA_NAMESECTION_NAME ".profiling.names"

std::pair<DataSection *, DataSection *> ProfileInstrumentPass
    ::createDataSection(Module *module) {

    auto regionList = module->getDataRegionList();
    if(auto section = regionList->findDataSection(DATA_SECTION_NAME)) {
        if(auto nameSection = regionList->findDataSection(DATA_NAMESECTION_NAME)) {
            return std::make_pair(section, nameSection);
        }
    }

    auto region = new DataRegion(DATA_REGION_ADDRESS);
    region->setPosition(new AbsolutePosition(DATA_REGION_ADDRESS));
    regionList->getChildren()->add(region);
    region->setParent(regionList);

    auto section = new DataSection();
    section->setName(DATA_SECTION_NAME);
    section->setAlignment(0x8);
    section->setPermissions(SHF_WRITE | SHF_ALLOC);
    section->setPosition(new AbsoluteOffsetPosition(section, 0));
    section->setType(DataSection::TYPE_DATA);
    region->getChildren()->add(section);
    section->setParent(region);

    auto nameRegion = new DataRegion(DATA_NAMEREGION_ADDRESS);
    nameRegion->setPosition(new AbsolutePosition(DATA_NAMEREGION_ADDRESS));
    regionList->getChildren()->add(nameRegion);
    nameRegion->setParent(regionList);

    auto nameSection = new DataSection();
    nameSection->setName(DATA_NAMESECTION_NAME);
    nameSection->setAlignment(0x1);
    nameSection->setPermissions(SHF_ALLOC);
    nameSection->setPosition(new AbsoluteOffsetPosition(nameSection, 0));
    nameSection->setType(DataSection::TYPE_DATA);
    nameRegion->getChildren()->add(nameSection);
    nameSection->setParent(nameRegion);

    return std::make_pair(section, nameSection);
}

Link *ProfileInstrumentPass::addVariable(DataSection *section, Function *function) {
    auto region = static_cast<DataRegion *>(section->getParent());
    auto offset = section->getSize();

    const size_t VAR_SIZE = 8;

    auto var = new GlobalVariable("__counter_" + function->getName());
    var->setPosition(new AbsolutePosition(section->getAddress()+section->getSize()));

    char *name = new char[var->getName().length() + 1];
    std::strcpy(name, var->getName().c_str());

    auto nsymbol = new Symbol(
        var->getAddress(), VAR_SIZE, name,
        Symbol::TYPE_OBJECT, Symbol::BIND_LOCAL, 0, 0);
    var->setSymbol(nsymbol);

    section->addGlobalVariable(var);

    LOG(0, name << " is a global symbol");

    section->setSize(section->getSize() + 8);
    region->setSize(region->getSize() + 8);

    return new DataOffsetLink(section, offset, Link::SCOPE_INTERNAL_DATA);
}

void ProfileInstrumentPass::appendFunctionName(DataSection *nameSection,
    const std::string &name) {

    auto region = static_cast<DataRegion *>(nameSection->getParent());
    region->setSize(region->getSize() + name.length() + 1);
    nameSection->setSize(nameSection->getSize() + name.length() + 1);

    auto bytes = region->getDataBytes();
    bytes.append(name.c_str(), name.length() + 1);
    region->saveDataBytes(bytes);
}
