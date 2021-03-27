#if __GNUC__ >= 9
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif

#include <llvm/Object/Archive.h>
#include <llvm/Object/ArchiveWriter.h>
#include <llvm/Object/COFF.h>
#include <llvm/Object/COFFImportFile.h>
#include <llvm/Object/COFFModuleDefinition.h>
#include <llvm/Support/Host.h>
#include <lld/Common/Driver.h>

#include "llvm_api.h"

#if __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif

using namespace llvm;

// if (Triple(sys::getProcessTriple()).getOS() == Triple::Win32)

OSType LLVMGetHostOSType() {
	return (OSType)Triple(sys::getProcessTriple()).getOS();
}

OSType LLVMParseOS(const char* osname) {
	if (strcmp(osname, "macos") == 0)
		return OSMacOSX;
	return (OSType)Triple("", "", osname).getOS();
}

const char* LLVMGetOSTypeName(OSType os) {
	const char* name = (const char*)Triple::getOSTypeName((Triple::OSType)os).bytes_begin();
	if (strcmp(name, "macosx") == 0)
		return "macos";
	return name;
}


bool LLVMWriteArchive(const char *archive_name, const char **file_names, size_t file_name_count,
		OSType os_type)
{
	object::Archive::Kind kind;
	switch (os_type) {
		case OSWin32:
			// For some reason llvm-lib passes K_GNU on windows.
			// See lib/ToolDrivers/llvm-lib/LibDriver.cpp:168 in libDriverMain
			kind = object::Archive::K_GNU;
			break;
		case OSLinux:
			kind = object::Archive::K_GNU;
			break;
		case OSMacOSX:
		case OSDarwin:
		case OSIOS:
			kind = object::Archive::K_DARWIN;
			break;
		case OSOpenBSD:
		case OSFreeBSD:
			kind = object::Archive::K_BSD;
			break;
		default:
			kind = object::Archive::K_GNU;
	}
	SmallVector<NewArchiveMember, 4> new_members;
	for (size_t i = 0; i < file_name_count; i += 1) {
		Expected<NewArchiveMember> new_member = NewArchiveMember::getFile(file_names[i], true);
		Error err = new_member.takeError();
		if (err) return true;
		new_members.push_back(std::move(*new_member));
	}
	Error err = writeArchive(archive_name, new_members, true, kind, true, false, nullptr);
	if (err) return true;
	return false;
}

// See lld/Common/Driver.h for lld api
// See lls/tools/lld/lld.cpp for the lld program main

int LLDLinkCOFF(int argc, const char **argv, bool can_exit_early) {
	std::vector<const char *> args(argv, argv + argc);
	return lld::coff::link(args, can_exit_early, llvm::outs(), llvm::errs());
}

int LLDLinkELF(int argc, const char **argv, bool can_exit_early) {
	std::vector<const char *> args(argv, argv + argc);
	return lld::elf::link(args, can_exit_early, llvm::outs(), llvm::errs());
}

int LLDLinkMachO(int argc, const char **argv, bool can_exit_early) {
	// Note: there's both lld::mach_o and lld::macho -- the latter is a newer linker
	// which is work in progress (as of Feb 2021)
	std::vector<const char *> args(argv, argv + argc);
	// return lld::macho::link(args, can_exit_early, llvm::outs(), llvm::errs()); // WIP
	return lld::mach_o::link(args, can_exit_early, llvm::outs(), llvm::errs());
}

int LLDLinkWasm(int argc, const char **argv, bool can_exit_early) {
	std::vector<const char *> args(argv, argv + argc);
	return lld::wasm::link(args, can_exit_early, llvm::outs(), llvm::errs());
}

