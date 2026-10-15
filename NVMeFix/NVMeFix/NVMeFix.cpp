//
// @file NVMeFix.cpp
//
// NVMeFix
//
// Copyright © 2019 acidanthera. All rights reserved.
//
// This program and the accompanying materials
// are licensed and made available under the terms and conditions of the BSD License
// which accompanies this distribution.  The full text of the license may be found at
// http://opensource.org/licenses/bsd-license.php
// THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
// WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

#include <IOKit/IOService.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/assert.h>
#include <libkern/c++/OSMetaClass.h>

#include <Headers/kern_api.hpp>
#include <Headers/kern_disasm.hpp>
#include <Headers/kern_iokit.hpp>
#include <Headers/hde64.h>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>

#include "Log.hpp"
#include "NVMeFixPlugin.hpp"

static NVMeFixPlugin plugin;

NVMeFixPlugin& NVMeFixPlugin::globalPlugin() {
	return plugin;
}

/**
 * This may be invoked before or after we get IOBSD mount notification, so in the both functions we
 * attempt to solve symbols and handle the controllers.
 */
void NVMeFixPlugin::processKext(void* that, KernelPatcher& patcher, size_t index, mach_vm_address_t,
								size_t) {
	auto plugin = static_cast<NVMeFixPlugin*>(that);
	assert(plugin);

	if (index != plugin->kextInfo.loadIndex)
		return;

	DBGLOG(Log::Plugin, "processKext %s", plugin->kextInfo.id);

	if (plugin->solveSymbols(patcher)) {
		if (!plugin->solveEarlyMask(patcher, index))
			SYSLOG(Log::Plugin, "Early interrupt mask is not available, PM981 timeouts are not handled");
		atomic_store_explicit(&plugin->solvedSymbols, true, memory_order_release);
		plugin->handleControllers();
	}
}

bool NVMeFixPlugin::solveSymbols(KernelPatcher& kp) {
	auto idx = plugin.kextInfo.loadIndex;
	bool res = true;
	res &= (kextFuncs.IONVMeController.IssueIdentifyCommandNew.solve(kp, idx) ||
			kextFuncs.IONVMeController.IssueIdentifyCommand.solve(kp, idx)) &&
	kextFuncs.IONVMeController.ProcessSyncNVMeRequest.solve(kp, idx) &&
	(kextFuncs.IONVMeController.GetRequestNew.solve(kp, idx) ||
	 kextFuncs.IONVMeController.GetRequest.solve(kp, idx)) &&
	kextFuncs.AppleNVMeRequest.BuildCommandGetFeatures.solve(kp, idx) &&
	kextFuncs.AppleNVMeRequest.BuildCommandSetFeaturesCommon.solve(kp, idx) &&
	kextFuncs.IONVMeController.ReturnRequest.solve(kp, idx) &&
	kextFuncs.AppleNVMeRequest.GetStatus.solve(kp, idx) &&
	kextFuncs.AppleNVMeRequest.GetOpcode.solve(kp, idx) &&
	kextFuncs.AppleNVMeRequest.GenerateIOVMSegments.solve(kp, idx) &&
	kextFuncs.IONVMeController.FilterInterruptRequest.solve(kp, idx);

	/* mov eax, [rdi+0xA8] */
	res &= kextMembers.AppleNVMeRequest.result.fromFunc(kextFuncs.AppleNVMeRequest.GetStatus.fptr,
														 0x8b, 0, 7, 4) &&
	/* movzx eax, byte ptr [rdi+0x10A] */
		kextMembers.AppleNVMeRequest.command.fromFunc(kextFuncs.AppleNVMeRequest.GetOpcode.fptr,
												  0xf, 0, 7) &&
	/* mov [r14+0xC0], r15 (14.0+) or mov [rbx+0xC0], r15 (11.3-13.x) or mov [rbx+0xC0], r12 (<=11.2) */
		(kextFuncs.IONVMeController.IssueIdentifyCommandNew.fptr ?
		kextMembers.AppleNVMeRequest.prpDescriptor.fromFunc(kextFuncs.IONVMeController.IssueIdentifyCommandNew.fptr, 0x89, 7, getKernelVersion() >= KernelVersion::Sonoma ? 14 : 3) :
		 kextMembers.AppleNVMeRequest.prpDescriptor.fromFunc(kextFuncs.IONVMeController.IssueIdentifyCommand.fptr, 0x89, 4, 3));

	/* cmp byte ptr [rdi+269h], 0 */
	kextMembers.IONVMeController.ANS2MSIWorkaround.fromFunc(kextFuncs.IONVMeController.FilterInterruptRequest.fptr,
																0x80, 7, 7, 0, 32);

	if (res)
		kextMembers.AppleNVMeRequest.controller.offs = kextMembers.AppleNVMeRequest.result.offs - 12;

	res &= PM.solveSymbols(kp);
	if (!res)
		DBGLOG(Log::Plugin, "Failed to solve symbols");
	return res;
}

/**
 * apple dropped ANS2MSIWorkaround in 11.0, so the early mask that fixed pm981 timeouts on 10.15 is
 * gone and FilterInterruptRequest checks the completion queues before masking the interrupt.
 * we put the 10.15 order back: mask first, unmask through HandleSpuriousInterrupt when nothing is
 * pending, otherwise signal the event source so HandleInterruptRequest runs (and unmasks when done).
 * every offset is read from the code at boot and the queue check must match byte for byte.
 * apple's code is never patched: handleController swaps the filter pointer of the controller's own
 * event source, and only when it still points at apple's FilterInterruptRequest.
 */
bool NVMeFixPlugin::solveEarlyMask(KernelPatcher& kp, size_t idx) {
	auto& funcs = kextFuncs.IONVMeController;
	auto& members = kextMembers.IONVMeController;

	/* 10.15 and older still have apple's own workaround, which handleController enables */
	if (members.ANS2MSIWorkaround.has())
		return false;

	if (!funcs.FilterInterruptRequest.solve(kp, idx) || !funcs.HandleSpuriousInterrupt.solve(kp, idx) ||
		!funcs.ClearMSIXInterrupts.solve(kp, idx) || !funcs.WriteRegister32.solve(kp, idx))
		return false;

	hde64s dis;

	/* test byte ptr [rdi+flags], msix at the start of HandleSpuriousInterrupt */
	auto addr = funcs.HandleSpuriousInterrupt.fptr;
	for (size_t i = 0; i < 4 && !members.interruptFlags.has(); i++) {
		auto sz = Disassembler::hdeDisasm(addr, &dis);
		if (dis.flags & F_ERROR)
			break;
		if (dis.opcode == 0xf6 && dis.modrm_reg == 0 && dis.modrm_rm == 7 && dis.rex_b == 0 && dis.modrm_mod == 2) {
			members.interruptFlags.offs = dis.disp.disp32;
			msixFlag = dis.imm.imm8;
		}
		addr += sz;
	}

	static constexpr uint8_t ioQueueCheck[] {
		0x0f, 0xb7, 0x48, 0x48, 0x48, 0x8b, 0x50, 0x50, 0xc1, 0xe1, 0x04, 0x0f, 0xb7, 0x4c, 0x0a, 0x0e,
		0x8b, 0x50, 0x58, 0xd1, 0xea, 0x31, 0xca, 0xf6, 0xc2, 0x01, 0x74
	};
	static constexpr uint8_t adminQueueCheck[] {
		0x0f, 0xb7, 0x48, 0x18, 0x48, 0x8b, 0x50, 0x20, 0xc1, 0xe1, 0x04, 0x0f, 0xb7, 0x4c, 0x0a, 0x0e,
		0x8b, 0x40, 0x28, 0xd1, 0xe8, 0x31, 0xc8, 0xa8, 0x01
	};
	static constexpr uint8_t signalCall[] {0x48, 0x8b, 0x07, 0xff, 0x90};

	bool filterTestsFlags {false};
	addr = funcs.FilterInterruptRequest.fptr;
	for (size_t i = 0; i < 96; i++) {
		auto sz = Disassembler::hdeDisasm(addr, &dis);
		/* stay inside apple's filter */
		if ((dis.flags & F_ERROR) || dis.opcode == 0xc3)
			break;
		auto next = reinterpret_cast<const uint8_t*>(addr + sz);
		auto rbxDisp32 = dis.modrm_rm == 3 && dis.rex_b == 0 && dis.modrm_mod == 2;

		/* mov rax, [rbx+queues] followed by the exact completion queue phase check we reimplement */
		if (dis.opcode == 0x8b && dis.rex_w && dis.modrm_reg == 0 && rbxDisp32 &&
			!memcmp(next, ioQueueCheck, sizeof(ioQueueCheck)) &&
			!memcmp(next + sizeof(ioQueueCheck) + 1, adminQueueCheck, sizeof(adminQueueCheck)))
			members.completionQueues.offs = dis.disp.disp32;

		/* test byte ptr [rbx+flags], msix must pick the mask method the same way HandleSpuriousInterrupt does */
		if (dis.opcode == 0xf6 && dis.modrm_reg == 0 && rbxDisp32)
			filterTestsFlags = dis.disp.disp32 == members.interruptFlags.offs && dis.imm.imm8 == msixFlag;

		/* mov rdi, [rbx+source]; mov rax, [rdi]; call [rax+0x238] (IOFilterInterruptEventSource::signalInterrupt) */
		if (dis.opcode == 0x8b && dis.rex_w && dis.modrm_reg == 7 && rbxDisp32 && !memcmp(next, signalCall, sizeof(signalCall)) &&
			*reinterpret_cast<const uint32_t*>(next + sizeof(signalCall)) == 0x238) {
			members.eventSource.offs = dis.disp.disp32;
			break;
		}
		addr += sz;
	}

	if (!members.interruptFlags.has() || !msixFlag || !members.completionQueues.has() || !filterTestsFlags ||
		!members.eventSource.has()) {
		SYSLOG(Log::Plugin, "Unknown FilterInterruptRequest layout (flags 0x%llx/0x%x/%d, queues 0x%llx, source 0x%llx)",
			   members.interruptFlags.offs, msixFlag, filterTestsFlags, members.completionQueues.offs,
			   members.eventSource.offs);
		return false;
	}

	DBGLOG(Log::Plugin, "Early mask flags 0x%llx/0x%x queues 0x%llx source 0x%llx", members.interruptFlags.offs,
		   msixFlag, members.completionQueues.offs, members.eventSource.offs);

	earlyMaskReady = true;
	return true;
}

/* masks the interrupt, then reports whether a completion is waiting. safe in primary interrupt context */
bool NVMeFixPlugin::maskAndCheckPending(void* controller) {
	auto& plugin = globalPlugin();
	auto& funcs = plugin.kextFuncs.IONVMeController;
	auto& members = plugin.kextMembers.IONVMeController;

	/* 0xc is INTMS, writing 1 masks vector 0 */
	if (members.interruptFlags.get(controller) & plugin.msixFlag)
		funcs.ClearMSIXInterrupts(controller);
	else
		funcs.WriteRegister32(controller, 0xc, 1);

	/*
	 * same check as apple's filter for both completion queues it looks at (+0x40 and +0x10):
	 * an entry is new when its phase tag (bit 0 of the status word) equals the expected phase
	 */
	auto queues = members.completionQueues.get(controller);
	uint8_t* completionQueues[] {queues + 0x40, queues + 0x10};
	bool pending {false};
	for (auto cq : completionQueues) {
		auto head = *reinterpret_cast<volatile uint16_t*>(cq + 0x08);
		auto entries = *reinterpret_cast<uint8_t* volatile*>(cq + 0x10);
		auto phase = *reinterpret_cast<volatile uint32_t*>(cq + 0x18);
		auto status = *reinterpret_cast<volatile uint16_t*>(entries + static_cast<uint32_t>(head) * 16 + 0x0e);
		pending |= ((status ^ (phase >> 1)) & 1) == 0;
	}

	/* nothing to hand over, so unmask again */
	if (!pending)
		funcs.HandleSpuriousInterrupt(controller);
	return pending;
}

/* runs in primary interrupt context: no locks, no allocations, no logging */
bool NVMeFixPlugin::filterInterruptRequest(OSObject* controller, IOFilterInterruptEventSource* source) {
	/* already masked when pending, so signal like apple's filter does: HandleInterruptRequest runs and unmasks */
	if (maskAndCheckPending(controller))
		source->signalInterrupt();
	return false;
}

/* runs on the controller's workloop: picks up a completion whose interrupt never arrived */
void NVMeFixPlugin::watchdogTick(OSObject* controller, IOTimerEventSource* timer) {
	auto& plugin = globalPlugin();
	if (maskAndCheckPending(controller))
		plugin.watchdogSource->signalInterrupt();
	timer->setTimeoutMS(watchdogPeriodMs);
}

/**
 * This handler will be invoked when a media (whole disk or a partition) BSD node becomes registered.
 * We need to do two things now:
 * 1. Discover any undetected NVMe controllers.
 * 2. Try and solve symbols. If the relevant partition for symbol solving is not available, the call
 * will fail and we may succeed at next mount.
 * If we have all the symbols ready, we can proceed working with controllers.
 */
bool NVMeFixPlugin::matchingNotificationHandler(void* that, void* , IOService* service,
												IONotifier* notifier) {
	auto plugin = static_cast<NVMeFixPlugin*>(that);
	assert(plugin);
	assert(service);

	IOLockLock(plugin->lck);

	DBGLOG("nvmef", "matchingNotificationHandler for %s", service->getName());

	auto parent = service->getProvider();

	/* Typical depth is 9 on real setups */
	for (int i = 0; parent && i < controllerSearchDepth; i++) {
//		DBGLOG("nvmef", "Parent %s", parent->getName());

		if (parent->metaCast("IONVMeController")) {
			bool has = false;

			for (size_t i = 0; i < plugin->controllers.size(); i++)
				if (plugin->controllers[i]->controller == parent) {
					has = true;
					break;
				}

			if (!has) {
				auto entry = new ControllerEntry(parent);
				if (!entry) {
					SYSLOG(Log::Plugin, "Failed to allocate ControllerEntry memory");
					break;
				}
				if (!plugin->controllers.push_back(entry)) {
					SYSLOG(Log::Plugin, "Failed to insert ControllerEntry memory");
					ControllerEntry::deleter(entry);
					break;
				}
				break;
			}
		}

		parent = parent->getProvider();
	}

//	DBGLOG("nvmef", "Discovered %u controllers", plugin->controllers.size());

	IOLockUnlock(plugin->lck);

	if (atomic_load_explicit(&plugin->solvedSymbols, memory_order_acquire))
		plugin->handleControllers();

	return true;
}

void NVMeFixPlugin::handleControllers() {
	DBGLOG("nvmef", "handleControllers for %u controllers", controllers.size());
	for (size_t i = 0; i < controllers.size(); i++) {
		IOLockLock(controllers[i]->lck);
		controllers[i]->controller->retain();
		handleController(*controllers[i]);
		controllers[i]->controller->release();
		IOLockUnlock(controllers[i]->lck);
	}
}

void NVMeFixPlugin::forceEnableASPM(IOService *device) {
	IOPCIDevice *pci = static_cast<IOPCIDevice *>(device->metaCast("IOPCIDevice"));
	if (!pci) return;

	uint32_t aspm = 0;
	auto prop = device->getProperty("pci-aspm-default");
	if (prop) {
		auto num = OSDynamicCast(OSNumber, prop);
		if (num != nullptr) {
			aspm = num->unsigned32BitValue();
		} else {
			auto data = OSDynamicCast(OSData, prop);
			if (data != nullptr && data->getLength() == sizeof(aspm))
				lilu_os_memcpy(&aspm, data->getBytesNoCopy(), sizeof(aspm));
		}
	}

	DBGLOG(Log::Plugin, "Activating ASPM on %s, currently %X", safeString(device->getName()), aspm);

	// Do not repeat what is already done.
	if ((aspm & ASPM_Mask) == ASPM_L1EntryEnabled) return;

	UInt8 offset = 0;
	if (!pci->findPCICapability(kIOPCICapabilityIDPCIExpress, &offset)) {
		SYSLOG(Log::Plugin, "NO PCIe capability support on %s", safeString(device->getName()));
		return;
	}

	offset += 0x10; // link control offset.

	IOPCIAddressSpace space = pci->space;
	space.es.registerNumExtended = 0;

	auto linkControl = pci->configRead16(space, offset);
	pci->configWrite16(space, offset, (linkControl & ~ASPM_Mask) | ASPM_L1EntryEnabled);
	auto newLinkControl = pci->configRead16(space, offset);
	DBGLOG(Log::Plugin, "ASPM transition on %s from %X to %X", safeString(device->getName()), linkControl, newLinkControl);
	pci->setProperty("pci-aspm-custom", newLinkControl, 32);
}

void NVMeFixPlugin::handleController(ControllerEntry& entry) {
	assert(entry.controller);

	if (entry.processed)
		return;

	/* No error signaling -- just ACK the discovery to notification handler */
	entry.processed = true;

	uint32_t vendor {};
	propertyFromParent(entry.controller, "vendor-id", vendor);
	if (vendor == 0x106b || entry.controller->metaCast("AppleNVMeController")) {
		SYSLOG(Log::Plugin, "Ignoring Apple controller");
		return;
	}

	/**
	 * Force enable ASPM when the user cannot provide device properties.
	 */
	if (checkKernelArgument("-nvmefaspm")) {
		auto ssd = OSDynamicCast(IOService, entry.controller->getParentEntry(gIOServicePlane));
		if (ssd) {
			forceEnableASPM(ssd);
			auto bridge = OSDynamicCast(IOService, ssd->getParentEntry(gIODTPlane));
			if (bridge)
				forceEnableASPM(bridge);
		}
	}

	/**
	 * Force enable ANS2MSIWorkaround.
	 *
	 * We would often get a panic with I/O Read command timeout on VMware and Samsung PM981.
	 * Some investigation showed that CQ head entry phase and CQ phase would mismatch.
	 * This implies there is a race such that CQ head gets updated to point to an entry with inverted
	 * phase.
	 * Since FilterIRQ detects phase mismatch, it does not schedule HandleIRQ at the workloop of NVMe
	 * controller, so a request is never handled and we get a timeout. If Filter and Handle IRQ calls can
	 * possibly race, this may happen because HandleIRQ does not manage to update CQ phase in time before
	 * FilterIRQ phase check is scheduled, observing the old phase. This is the case in situation where
	 * two FilterIRQ calls happen with no HandleIRQ in between where the controller was too slow to
	 * notice INTMS being set.
	 * Normally, IRQ is masked just before HandleIRQ is scheduled in FilterIRQ, and unmasked when
	 * HandleIRQ is done. IONVMeController::ANS2MSIWorkaround forces IRQ to be masked at the very
	 * start of FilterIRQ instead so that FilterIRQ does not race with itself. This seems to eliminate
	 * the timeouts.
	 **/
	if (kextMembers.IONVMeController.ANS2MSIWorkaround.has()) {
		kextMembers.IONVMeController.ANS2MSIWorkaround.get(entry.controller) = 1;
	} else if (earlyMaskReady && !strcmp(entry.controller->getMetaClass()->getClassName(), "IONVMeController")) {
		/* same controllers as the old workaround, minus apple's own subclasses */
		auto source = OSDynamicCast(IOFilterInterruptEventSource,
									static_cast<OSObject*>(kextMembers.IONVMeController.eventSource.get(entry.controller)));
		auto ours = reinterpret_cast<IOFilterInterruptEventSource::Filter>(filterInterruptRequest);

		/* swap only apple's own filter pointer on the controller's own source, anything unexpected stays untouched */
		if (source && source->*EventSourceAccess::ownerMember() == entry.controller &&
			reinterpret_cast<mach_vm_address_t>(source->*EventSourceAccess::filterMember()) ==
			kextFuncs.IONVMeController.FilterInterruptRequest.fptr) {
			__atomic_store_n(&(source->*EventSourceAccess::filterMember()), ours, __ATOMIC_RELEASE);
			SYSLOG(Log::Plugin, "Early interrupt mask enabled");
		} else if (!source || source->*EventSourceAccess::filterMember() != ours) {
			SYSLOG(Log::Plugin, "Early interrupt mask not enabled, unexpected interrupt source");
		}
		/* ioreg shows whether the filter is ours, recovery has no unified log to read the syslog line */
		bool earlyMask = source && source->*EventSourceAccess::filterMember() == ours;
		entry.controller->setProperty("early-mask", earlyMask);

		/* one watchdog, for the first early mask controller (a laptop has one nvme slot in use) */
		auto workLoop = earlyMask && !watchdog ? entry.controller->getWorkLoop() : nullptr;
		if (workLoop) {
			watchdog = IOTimerEventSource::timerEventSource(entry.controller, watchdogTick);
			if (watchdog && workLoop->addEventSource(watchdog) == kIOReturnSuccess) {
				entry.controller->retain();
				source->retain();
				watchdogController = entry.controller;
				watchdogSource = source;
				watchdog->setTimeoutMS(watchdogPeriodMs);
				SYSLOG(Log::Plugin, "Completion watchdog started");
			} else {
				SYSLOG(Log::Plugin, "Completion watchdog not started");
				OSSafeReleaseNULL(watchdog);
			}
			entry.controller->setProperty("watchdog", watchdog != nullptr);
		}
	} else {
		DBGLOG(Log::Plugin, "Ignoring ANS2 workaround patch on newer system");
	}

	/* First get quirks based on PCI device */
	entry.quirks = NVMe::quirksForController(entry.controller);
	propertyFromParent(entry.controller, "ps-max-latency-us", entry.ps_max_latency_us);

	IOBufferMemoryDescriptor* identifyDesc {nullptr};

	if (identify(entry, identifyDesc) != kIOReturnSuccess || !identifyDesc) {
		SYSLOG(Log::Plugin, "Failed to identify controller");
		return;
	}

	auto ctrl = reinterpret_cast<NVMe::nvme_id_ctrl*>(identifyDesc->getBytesNoCopy());
	if (!ctrl) {
		DBGLOG(Log::Plugin, "Failed to get identify buffer bytes");
		if (identifyDesc)
			identifyDesc->release();
		return;
	}

	entry.identify = identifyDesc;

	/* Get additional quirks based on identify data */
	entry.quirks |= NVMe::quirksForController(ctrl->vid, ctrl->mn, ctrl->fr);

	entry.controller->setProperty("quirks", OSNumber::withNumber(entry.quirks, 8 * sizeof(entry.quirks)));

#ifdef DEBUG
	char mn[40];
	lilu_os_memcpy(mn, ctrl->mn, sizeof(mn));
	mn[sizeof(mn) - 1] = '\0';

	DBGLOG(Log::Plugin, "Identified model %s (vid 0x%x)", mn, ctrl->vid);
#endif

	if (!enableAPST(entry, ctrl))
		SYSLOG(Log::APST, "Failed to enable APST");

	/* apst switched off on purpose (ps-max-latency-us = 0): keep the drive at full power, no host power state changes */
	if (!entry.apste && entry.ps_max_latency_us == 0)
		SYSLOG(Log::PM, "Power management skipped, drive stays at full power");
	else if (!PM.init(entry, ctrl, entry.apste))
		SYSLOG(Log::PM, "Failed to initialise power management");
}

IOReturn NVMeFixPlugin::identify(ControllerEntry& entry, IOBufferMemoryDescriptor*& desc) {
	IOReturn ret = kIOReturnSuccess;

	uint8_t* data {nullptr};
	bool prepared {false};

	desc = IOBufferMemoryDescriptor::withCapacity(sizeof(NVMe::nvme_id_ctrl), kIODirectionIn);

	if (!desc) {
		SYSLOG(Log::Plugin, "Failed to init descriptor");
		ret = kIOReturnNoResources;
		goto fail;
	}
	data = static_cast<uint8_t*>(desc->getBytesNoCopy());
	memset(data, '\0', desc->getLength());

	ret = desc->prepare();
	if (ret != kIOReturnSuccess) {
		SYSLOG(Log::Plugin, "Failed to prepare descriptor");
		goto fail;
	}
	prepared = true;

	if (kextFuncs.IONVMeController.IssueIdentifyCommandNew.fptr)
		ret = kextFuncs.IONVMeController.IssueIdentifyCommandNew(entry.controller, desc, 0, false);
	else
		ret = kextFuncs.IONVMeController.IssueIdentifyCommand(entry.controller, desc, nullptr, 0);
	if (ret != kIOReturnSuccess) {
		SYSLOG(Log::Plugin, "issueIdentifyCommand failed");
		goto fail;
	}

fail:
	if (prepared)
		desc->complete();
	if (ret != kIOReturnSuccess && desc)
		desc->release();
	return ret;
}

IOReturn NVMeFixPlugin::NVMeFeatures(ControllerEntry& entry, unsigned fid, unsigned* dword11,
										IOBufferMemoryDescriptor* desc, uint32_t* res, bool set) {
	auto ret = kIOReturnSuccess;

	bool prepared {false};

	if (desc) {
		ret = desc->prepare();
		prepared = ret == kIOReturnSuccess;
	}

	if (!desc || prepared) {
		void *req;
		if (kextFuncs.IONVMeController.GetRequestNew.fptr)
			req = kextFuncs.IONVMeController.GetRequestNew(entry.controller, 1, 0); /* Set 0b10 to tickle */
		else
			req = kextFuncs.IONVMeController.GetRequest(entry.controller, 1); /* Set 0b10 to tickle */

		if (!req) {
			DBGLOG(Log::Feature, "IONVMeController::GetRequest failed");
			ret = kIOReturnNoResources;
		} else if (desc)
			ret = reinterpret_cast<IODMACommand*>(req)->setMemoryDescriptor(desc);

		if (ret == kIOReturnSuccess) {
			if (set)
				kextFuncs.AppleNVMeRequest.BuildCommandSetFeaturesCommon(req, fid);
			else
				kextFuncs.AppleNVMeRequest.BuildCommandGetFeatures(req, fid);

			if (dword11)
				kextMembers.AppleNVMeRequest.command.get(req).features.dword11 = *dword11;
			if (desc) {
				kextMembers.AppleNVMeRequest.prpDescriptor.get(req) = desc;
				ret = reinterpret_cast<IODMACommand*>(req)->prepare(0, desc->getLength());
			}

			if (ret != kIOReturnSuccess)
				DBGLOG(Log::Feature, "Failed to prepare DMA command");
			else {
				if (desc)
					ret = kextFuncs.AppleNVMeRequest.GenerateIOVMSegments(req, 0,
																		  desc->getLength());

				if (ret != kIOReturnSuccess)
					DBGLOG(Log::Feature, "Failed to generate IO VM segments");
				else {
					kextMembers.AppleNVMeRequest.controller.get(req) = entry.controller;

					ret = kextFuncs.IONVMeController.ProcessSyncNVMeRequest(entry.controller,
																			req);
					if (ret != kIOReturnSuccess)
						DBGLOG(Log::Feature, "ProcessSyncNVMeRequest failed");
					else if (res)
						*res = kextMembers.AppleNVMeRequest.result.get(req);
				}
			}
			if (desc)
				reinterpret_cast<IODMACommand*>(req)->complete();
			kextFuncs.IONVMeController.ReturnRequest(entry.controller, req);
		}
	} else
		SYSLOG(Log::Feature, "Failed to prepare buffer");

	if (desc && prepared)
		desc->complete();

	return ret;
}

/* Notifications are serialized for a single controller, so we don't have to sync with removal */
bool NVMeFixPlugin::terminatedNotificationHandler(void* that, void* , IOService* service,
												IONotifier* notifier) {
	auto plugin = static_cast<NVMeFixPlugin*>(that);
	assert(plugin);
	assert(service && service->metaCast("IONVMeController"));

	/* Controller retain count should equal 0, so we don't need to hold its lock now */
	IOLockLock(plugin->lck);
	/* stop polling a controller that is going away, then drop our references */
	if (plugin->watchdog && plugin->watchdogController == service) {
		plugin->watchdog->cancelTimeout();
		if (auto workLoop = plugin->watchdog->getWorkLoop())
			workLoop->removeEventSource(plugin->watchdog);
		OSSafeReleaseNULL(plugin->watchdog);
		OSSafeReleaseNULL(plugin->watchdogSource);
		OSSafeReleaseNULL(plugin->watchdogController);
	}
	for (size_t i = 0; i < plugin->controllers.size(); i++)
		if (plugin->controllers[i]->controller == service) {
			plugin->controllers.erase(i);
			break;
	   }
	IOLockUnlock(plugin->lck);

	return false;
}

/**
 * NOTE: We are in kmod context, not IOService. This works fine as long as we publish our personality
 * in Info.plist to match something in ioreg, but specify a non-existing IOClass so that IOKit attempts
 * to load us anyway. It is otherwise unsafe to use matching notifications from kmod when we have a
 * living IOService.
 */
void NVMeFixPlugin::init() {
	LiluAPI::Error err;

	if (!(lck = IOLockAlloc())) {
		SYSLOG(Log::Plugin, "Failed to alloc lock");
		goto fail;
	}

	atomic_store_explicit(&solvedSymbols, false, memory_order_relaxed);

	matchingNotifier = IOService::addMatchingNotification(gIOPublishNotification,
							IOService::serviceMatching("IOMediaBSDClient"),
							matchingNotificationHandler,
						    this);
	if (!matchingNotifier) {
		SYSLOG(Log::Plugin, "Failed to register for matching notification");
		goto fail;
	}

	terminationNotifier = IOService::addMatchingNotification(gIOTerminatedNotification,
							IOService::serviceMatching("IONVMeController"),
							terminatedNotificationHandler,
						    this);
	if (!terminationNotifier) {
		SYSLOG(Log::Plugin, "Failed to register for termination notification");
		goto fail;
	}

	DBGLOG(Log::Plugin, "Registered for matching notifications");

	err = lilu.onKextLoad(&kextInfo, 1, NVMeFixPlugin::processKext, this);
	if (err != LiluAPI::Error::NoError) {
		SYSLOG(Log::Plugin, "Failed to register kext load cb");
		goto fail;
	}

	return;
fail:
	if (lck)
		IOLockFree(lck);
	if (matchingNotifier)
		matchingNotifier->remove();
	if (terminationNotifier)
		terminationNotifier->remove();
}

void NVMeFixPlugin::deinit() {
	/* This kext is not unloadable */
	panic("nvmef: deinit called");
}

NVMeFixPlugin::ControllerEntry* NVMeFixPlugin::entryForController(IOService* controller) const {
	for (size_t i = 0; i < controllers.size(); i++)
		if (controllers[i]->controller == controller)
			return controllers[i];
	return nullptr;
}

static const char *bootargOff[] {
	"-nvmefoff"
};

static const char *bootargDebug[] {
	"-nvmefdbg"
};

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	nullptr,
	0,
	KernelVersion::Mojave,
	KernelVersion::Tahoe,
	[]() {
		NVMeFixPlugin::globalPlugin().init();
	}
};
