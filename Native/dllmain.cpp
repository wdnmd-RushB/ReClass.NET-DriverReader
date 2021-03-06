#include <cstdint>

// Remove annoying error
#if (_MSC_VER >= 1915)
#define no_init_all deprecated
#endif


#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <algorithm>

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <ReClassNET_Plugin.hpp>
#include <experimental/filesystem>
#include "DriverReader.h"
namespace fs = std::experimental::filesystem;

// Variables required for the Kernel Driver Exploit ;)
uintptr_t directoryTableBase = 0;
uintptr_t pKProcess = 0;
uintptr_t pBaseAddress = 0;


enum class Platform
{
	Unknown,
	X86,
	X64
};

Platform GetProcessPlatform(HANDLE process)
{
	static USHORT processorArchitecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
	if (processorArchitecture == PROCESSOR_ARCHITECTURE_UNKNOWN)
	{
		SYSTEM_INFO info = {};
		GetNativeSystemInfo(&info);

		processorArchitecture = info.wProcessorArchitecture;
	}

	switch (processorArchitecture)
	{
	case PROCESSOR_ARCHITECTURE_INTEL:
		return Platform::X86;
	case PROCESSOR_ARCHITECTURE_AMD64:
		auto isWow64 = FALSE;
		if (IsWow64Process(process, &isWow64))
		{
			return isWow64 ? Platform::X86 : Platform::X64;
		}

#ifdef RECLASSNET64
		return Platform::X64;
#else
		return Platform::X86;
#endif
	}
	return Platform::Unknown;
}

std::string getFileName(const std::string& s)
{
	char sep = '/';

#ifdef _WIN32
	sep = '\\';
#endif

	size_t i = s.rfind(sep, s.length());
	if (i != std::string::npos) {
		return(s.substr(i + 1, s.length() - i));
	}
	return("");
}

/// <summary>Opens the remote process.</summary>
/// <param name="id">The identifier of the process returned by EnumerateProcesses.</param>
/// <param name="desiredAccess">The desired access.</param>
/// <returns>A handle to the remote process or nullptr if an error occured.</returns>
extern "C" RC_Pointer RC_CallConv OpenRemoteProcess(RC_Pointer id, ProcessAccess desiredAccess)
{
	// BEFORE: Open the remote process with the desired access rights and return the handle to use with the other functions.
	// NOW: We are just returning the ID to the process instead. Now each methods takes care of resolving this ID (PID) to the respective process.
	// We need to do this to stop using privileged HANDLEs to the process
	return id;
}

/// <summary>Queries if the process is valid.</summary>
/// <param name="id">The process handle (now PID) obtained by OpenRemoteProcess.</param>
/// <returns>True if the process is valid, false if not.</returns>
extern "C" bool RC_CallConv IsProcessValid(RC_Pointer id)
{
	/*
	if (handle == nullptr)
	{
		return false;
	}

	const auto retn = WaitForSingleObject(handle, 0);
	if (retn == WAIT_FAILED)
	{
		return false;
	}

	return retn == WAIT_TIMEOUT;
	*/

	// BEFORE: Check if the handle is valid.
	// NOW: If is not null it is enough, we are using the PID now instead of a HANDLE
	if (id == nullptr)
	{
		return false;
	}

	return true;
}

/// <summary>Closes the handle to the remote process.</summary>
/// <param name="handle">The process handle obtained by OpenRemoteProcess.</param>
extern "C" void RC_CallConv CloseRemoteProcess(RC_Pointer handle)
{
	// BEFORE: Close the handle to the remote process.
	// NOW: We don't have a HANDLE so it is just a fake function
	return;
	
	/*
	if (handle == nullptr)
	{
		return;
	}

	CloseHandle(handle);
	*/
}


/// <summary>Enumerate all processes on the system.</summary>
/// <param name="callbackProcess">The callback for a process.</param>
extern "C" void RC_CallConv EnumerateProcesses(EnumerateProcessCallback callbackProcess)
{
	// With this trick we'll be able to print content to the console.
	// I let this here, because if you are using this you will probably need to debug, trust me.
	AllocConsole();
	SetConsoleTitle("Debug");
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
	freopen("CONIN$", "r", stdin);
	
	// Enumerate all processes with the current plattform (x86/x64) and call the callback.
	if (callbackProcess == nullptr)
	{
		return;
	}

	const auto handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (handle != INVALID_HANDLE_VALUE)
	{
		PROCESSENTRY32W pe32 = {};
		pe32.dwSize = sizeof(PROCESSENTRY32W);
		if (Process32FirstW(handle, &pe32))
		{
			do
			{
				// TODO: Remove this so we don't need to open a HANDLE to the game, in this case it is just a HANDLE with very limited privileges. Most AC seem to allow this kind of HANDLE.
				// There should be a better way to figure out the platform of the process (x86/x64)
				const auto handle_limited = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION , FALSE, static_cast<DWORD>(pe32.th32ProcessID));

				if (handle_limited == nullptr || handle == INVALID_HANDLE_VALUE)
				{
					continue;
				}

				// if 0 or SYSTEM process, just skip it.
				if (pe32.th32ProcessID == 0 || pe32.th32ProcessID == 4)
					continue;

				if (pe32.th32ProcessID)
				{
					const auto platform = GetProcessPlatform(handle_limited);
					CloseRemoteProcess(handle_limited);
#ifdef RECLASSNET64
					if (platform == Platform::X64)
#else
					if (platform == Platform::X86)
#endif
					{
						EnumerateProcessData data = { };
						data.Id = pe32.th32ProcessID;

						const auto name = fs::path(pe32.szExeFile).filename().u16string();
						const auto path = fs::path(pe32.szExeFile).u16string();
						str16cpy(data.Name, name.c_str(), std::min<size_t>(name.length(), PATH_MAXIMUM_LENGTH - 1));
						str16cpy(data.Path, path.c_str(), std::min<size_t>(path.length(), PATH_MAXIMUM_LENGTH - 1));

						callbackProcess(&data);
					}

				}

				

			} while (Process32NextW(handle, &pe32));
		}

		CloseHandle(handle);
	}

}

// Confirm of prepare if we have all what we need to read a ring3 process memory.
bool CheckKernelStatus(RC_Pointer id)
{
if (id)
	{
		// I'm using a limited HANDLE to get the name of the executable, is this necessary? ;(
		// It is not so uncommon to do this, and AC allow it.
		const auto handle_limited = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION , FALSE, reinterpret_cast<DWORD>(id));

		if (handle_limited == nullptr)
		{
			std::cout << "[-] Unable to get executable name." << std::endl;
			return false;
		}

		// I was to lazy to find another way of getting this.
		if (GetProcessImageFileNameA(handle_limited, DriverReader::targetProc, sizeof(DriverReader::targetProc)))
		{
			strcpy(DriverReader::targetProc,getFileName(DriverReader::targetProc).c_str());
		}
		else
		{
			std::cout << "\t[.] targetProc failed: 0x" << std::hex << GetLastError() << std::endl;
		}
		CloseHandle(handle_limited);
	}

	// Have we selected a new process on ReClass?
	// In that case we have to retrieve all the process information we need.
	if (strcmp(DriverReader::targetProc, DriverReader::previousTargetProc) != 0)
	{
		std::cout << "[.] Process context changed." << std::endl;
		if (DriverReader::getDeviceHandle("\\\\.\\GIO"))
		{
			std::cout << "[-] Driver not loaded" << std::endl;
			return false;
		}

		// Let's store this process as the last one we used for the next time we run this method.
		strcpy(DriverReader::previousTargetProc, DriverReader::targetProc);
	
		// Obtaining a valid pointer to a KProcess.
		// We need this to traverse the linked list of processes and find our target.
		pKProcess = DriverReader::GetKProcess(directoryTableBase);

		// With this pKProcess we traverse it until we find what we want. Our target process.
		pBaseAddress = DriverReader::SearchKProcess(DriverReader::targetProc, directoryTableBase, pKProcess);

		// And now we get all the kernel information we need to work ;)
		if (!DriverReader::ObtainKProcessInfo(directoryTableBase, pBaseAddress))
		{
			std::cout << "[-] ObtainKProcessInfo failed" << std::endl;
			return false;
		}

	}
	return true;
}


/// <summary>Enumerate all sections and modules of the remote process.</summary>
/// <param name="process">The process handle obtained by OpenRemoteProcess.</param>
/// <param name="callbackSection">The callback for a section.</param>
/// <param name="callbackModule">The callback for a module.</param>
void RC_CallConv EnumerateRemoteSectionsAndModules(RC_Pointer id, EnumerateRemoteSectionsCallback callbackSection, EnumerateRemoteModulesCallback callbackModule)
{
	// Enumerate all sections and modules of the remote process and call the callback for them.
	if (callbackSection == nullptr && callbackModule == nullptr && !DriverReader::DTBTargetProcess)
	{
		std::cout << "[-] EnumerateRemoteSectionsAndModules failed" << std::endl;
		return;
	}

	// We need to be sure that everything is setup for this function to run properly. 
	// If not we set it up here
	if (!CheckKernelStatus(id))
	{
		std::cout << "[-] CheckKernelStatus failed" << std::endl;
		return;
	}

	// Reset variables from the last run, and init new ones.
	std::vector<EnumerateRemoteSectionData> sections;
	DriverReader::sections = {};
	DriverReader::modules = {};

	// WalkVadADLTree will do the magic and retrieve all the section information we need.
	DriverReader::WalkVadADLTree(directoryTableBase,DriverReader::pVadRootTargetProcess);

	// I wanted to reuse my previous code so I just reassigned to a local variable
	// You thought you were lazy?
	sections = DriverReader::sections;

	// We already god all the sections, now we nede the modules
	// Here we go.
	DriverReader::EnumRing3ProcessModules(directoryTableBase);


		if (callbackModule != nullptr)
		{
			for (auto&& module: DriverReader::modules)
			{
				// Let's notify to Reclass we got a new module.
				callbackModule(&module);


				// Now we can add additional information to the sections we already have on our vector sections.
				// This can be done parsing the headers from the PE as you can see below.
				if (callbackSection != nullptr)
				{
					auto it = std::lower_bound(std::begin(sections), std::end(sections), static_cast<LPVOID>(module.BaseAddress), [&sections](const auto& lhs, const LPVOID& rhs)
					{
						return lhs.BaseAddress < rhs;
					});

					IMAGE_DOS_HEADER DosHdr = {};
					IMAGE_NT_HEADERS NtHdr = {};

					// Reading the headers.
					DriverReader::ReadVirtualMemory(DriverReader::DTBTargetProcess, (uintptr_t)module.BaseAddress, &DosHdr, sizeof(IMAGE_DOS_HEADER), NULL);
					DriverReader::ReadVirtualMemory(DriverReader::DTBTargetProcess,  (uintptr_t)(module.BaseAddress) + DosHdr.e_lfanew, &NtHdr, sizeof(IMAGE_NT_HEADERS), NULL);
					
					std::vector<IMAGE_SECTION_HEADER> sectionHeaders(NtHdr.FileHeader.NumberOfSections);
					
					DriverReader::ReadVirtualMemory(DriverReader::DTBTargetProcess, (uintptr_t)(module.BaseAddress) + DosHdr.e_lfanew + sizeof(IMAGE_NT_HEADERS), sectionHeaders.data(), NtHdr.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER), NULL);
					std::cout << NtHdr.FileHeader.NumberOfSections << std::endl;
					for (auto i = 0; i < NtHdr.FileHeader.NumberOfSections; ++i)
					{
						auto&& sectionHeader = sectionHeaders[i];
						const auto sectionAddress = reinterpret_cast<size_t>(module.BaseAddress) + sectionHeader.VirtualAddress;

						for (auto j = it; j != std::end(sections); ++j)
						{
							// We add the path to the executable.
							std::memcpy(j->ModulePath, module.Path, PATH_MAXIMUM_LENGTH * sizeof(RC_UnicodeChar));
							break;
						}

					}
				}
			}
		}

		// Finally, let's notify ReClass about the sections.
		if (callbackSection != nullptr)
		{
			for (auto&& section : sections)
			{
				callbackSection(&section);
			}
		}
}



/// <summary>Reads memory of the remote process.</summary>
/// <param name="handle">The process handle obtained by OpenRemoteProcess.</param>
/// <param name="address">The address to read from.</param>
/// <param name="buffer">The buffer to read into.</param>
/// <param name="offset">The offset into the buffer.</param>
/// <param name="size">The number of bytes to read.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv ReadRemoteMemory(RC_Pointer id, RC_Pointer address, RC_Pointer buffer, int offset, int size)
{
	// Read the memory of the remote process into the buffer.	
	if (!CheckKernelStatus(id))
		return false;
	//std::cout << "[+] Reading ########################################" << std::endl;
	//std::cout << "[+] directoryTableBase" << directoryTableBase << std::endl;
	//std::cout << "[+] pKProcess" << pKProcess << std::endl;
	//std::cout << "[+] pBaseAddress" << pBaseAddress << std::endl;
	//std::cout << "[+] address" << address << std::endl;
	//std::cout << "[+] size" << size << std::endl;
	
	buffer = reinterpret_cast<RC_Pointer>(reinterpret_cast<uintptr_t>(buffer) + offset);
	//std::cout << "[+] buffer" << buffer << std::endl;
	//std::cout << "[+] DriverReader::DTBTargetProcess " << DriverReader::DTBTargetProcess << std::endl;

	SIZE_T numberOfBytesRead;
	if (DriverReader::ReadVirtualMemory(DriverReader::DTBTargetProcess, reinterpret_cast<uintptr_t>(address), buffer, size, &numberOfBytesRead))
	{
		return true;
	}
	std::cout << "[-] RVM failed" << std::endl;

	return false;
}

/// <summary>Writes memory to the remote process.</summary>
/// <param name="process">The process handle obtained by OpenRemoteProcess.</param>
/// <param name="address">The address to write to.</param>
/// <param name="buffer">The buffer to write.</param>
/// <param name="offset">The offset into the buffer.</param>
/// <param name="size">The number of bytes to write.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv WriteRemoteMemory(RC_Pointer id, RC_Pointer address, RC_Pointer buffer, int offset, int size)
{
	if (!CheckKernelStatus(id))
		return false;

	// Write the buffer into the memory of the remote process.
    buffer = reinterpret_cast<RC_Pointer>(reinterpret_cast<uintptr_t>(buffer) + offset);

	SIZE_T numberOfBytesWritten;
	if (DriverReader::WriteVirtualMemory(DriverReader::DTBTargetProcess, reinterpret_cast<uintptr_t>(address), buffer, size, &numberOfBytesWritten))
	{
		return true;
	}

	return false;
}

/// <summary>Control the remote process (Pause, Resume, Terminate).</summary>
/// <param name="handle">The process handle obtained by OpenRemoteProcess.</param>
/// <param name="action">The action to perform.</param>
extern "C" void RC_CallConv ControlRemoteProcess(RC_Pointer handle, ControlRemoteProcessAction action)
{
	// Perform the desired action on the remote process.
}

/// <summary>Attach a debugger to the process.</summary>
/// <param name="id">The identifier of the process returned by EnumerateProcesses.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv AttachDebuggerToProcess(RC_Pointer id)
{
	// Attach a debugger to the remote process.

	return false;
}

/// <summary>Detach a debugger from the remote process.</summary>
/// <param name="id">The identifier of the process returned by EnumerateProcesses.</param>
extern "C" void RC_CallConv DetachDebuggerFromProcess(RC_Pointer id)
{
	// Detach the debugger.
}

/// <summary>Wait for a debug event within the given timeout.</summary>
/// <param name="evt">[out] The occured debug event.</param>
/// <param name="timeoutInMilliseconds">The timeout in milliseconds.</param>
/// <returns>True if an event occured within the given timeout, false if not.</returns>
extern "C" bool RC_CallConv AwaitDebugEvent(DebugEvent* evt, int timeoutInMilliseconds)
{
	// Wait for a debug event.

	return false;
}

/// <summary>Handles the debug event described by evt.</summary>
/// <param name="evt">[in] The (modified) event returned by AwaitDebugEvent.</param>
extern "C" void RC_CallConv HandleDebugEvent(DebugEvent* evt)
{
	// Handle the debug event.
}

/// <summary>Sets a hardware breakpoint.</summary>
/// <param name="processId">The identifier of the process returned by EnumerateProcesses.</param>
/// <param name="address">The address of the breakpoint.</param>
/// <param name="reg">The register to use.</param>
/// <param name="type">The type of the breakpoint.</param>
/// <param name="size">The size of the breakpoint.</param>
/// <param name="set">True to set the breakpoint, false to remove it.</param>
/// <returns>True if it succeeds, false if it fails.</returns>
extern "C" bool RC_CallConv SetHardwareBreakpoint(RC_Pointer id, RC_Pointer address, HardwareBreakpointRegister reg, HardwareBreakpointTrigger type, HardwareBreakpointSize size, bool set)
{
	// Set a hardware breakpoint with the given parameters.

	return false;
}
