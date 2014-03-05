# DuBStep 
##### A Library for creating hardware execution and data breakpoints at runtime on Win32/Win64

### Usage
Everything is implemented in a single header, dubstep.h, for ease of integration.
Simply include dubstep.h into your project somewhere. Note that it will include `<windows.h>`.
See the test source for example usage.

### API

* `HANDLE dubstep::SetBreakpoint(dubstep::BreakpointType type, void* address, dubstep::BreakpointSize size)`
	* Types:
		* dubstep::TYPE_Exec:   trap when the PC hits this address
		* dubstep::TYPE_Access: trap data reads and writes
		* dubstep::TYPE_Write:  trap data writes
	* Sizes:
		* dubstep::SIZE_1
		* dubstep::SIZE_2
		* dubstep::SIZE_4
		* dubstep::SIZE_8
	* Must be called from the thread you wish to monitor. You can have up to 4 active breakpoints per thread.
	* Returns a HANDLE to the breakpoint which can be passed to `ClearBreakpoint` to cancel it. A return value of 0 indicates that the breakpoint could not be created. Causes:
		* You do not have permission to `OpenThread` with `THREAD_ALL_ACCESS`
		* There are no available breakpoint registers
	
* `bool dubstep::ClearBreakpoint(HANDLE breakpoint)`
	* Cancels a breakpoint set by `SetBreakpoint`.

* `void dubstep::SetBreakpointHandler(BreakpointHandler handler)`
	* Installs a callback that will notify you when a breakpoint is hit.
	* Handler signature: `void MyHandler(void* address)`

### TODO
* Get this to work more cleanly while the debugger is attached

### Debug Registers Reference
* DR0, DR1, DR2, DR3 breakpoint address
* DR7: flags:
	* Bits 0-7: Flags for each of the 4 debug registers (2 for each). 
		* (1 << (reg * 2)) = Process, 
		* (1 << (reg * 2 + 1)) = Global. Global requires kernel privileges. If you are reading this, you don't have them.
	* Bits 16-23 :  2 bits for each register, breakpoint type:
		* 0x0: Code
		* 0x1: Write
		* 0x2: Reserved
		* 0x3: Triggers when data is read or written
	* Bits 24-31: 2 bits for each register, data size in bytes:
		* 0x0: 1
		* 0x1: 2
		* 0x2: 8
		* 0x3: 4

### References
http://en.wikipedia.org/wiki/X86_debug_register
http://www.codeproject.com/Articles/28071/Toggle-hardware-data-read-execute-breakpoints-prog
