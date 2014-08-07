# DuBStep (Dynamic Breakpoint API)
##### A Library for creating hardware execution and data breakpoints at runtime on Win32/Win64

### Purpose

Sometimes you need to set breakpoints from code. For example, trapping a buffer overrun by watching the memory after the end of your buffer. Or setting an instruction breakpoint after conditions have been met at runtime. Dubstep lets you do this.

Once you set a breakpoint, they will trigger in Visual Studio or CDB. If you have data breakpoints set in Visual Studio, the debugger and your code will fight. It's generally best to keep breakpoints to a minimum in VS when trying to use Dubstep to hunt something down. Visual Studio 2012's debugger appears to check the debug registers before stomping on them, but previous versions will just happily overwrite.

Works on: Visual Studio 2008 and newer. Should work on earlier versions, but they haven't been tested.

### Usage
Everything is implemented in a single header, dubstep.h, for ease of integration.
Simply include [dubstep.h](https://github.com/justinboswell/dubstep/raw/master/inc/dubstep.h) into your project somewhere. Note that it will include `<windows.h>`.
See the test source for example usage.

### API

* `dubstep::BreakpointHandle dubstep::SetBreakpoint(dubstep::BreakpointType type, void* address, dubstep::BreakpointSize size)`
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
	
* `bool dubstep::ClearBreakpoint(dubstep::BreakpointHandle breakpoint)`
	* Cancels a breakpoint set by `SetBreakpoint`.

* `void dubstep::SetBreakpointHandler(BreakpointHandler handler)`
	* Handler signature: `void MyHandler(void* address)` 
	* Installs a callback that will notify you when a breakpoint is hit. Note that this will make use of `SetUnhandledExceptionFilter()` and will override your filter if you have one. Also, you may stomp the filter if you install it after your first breakpoint after installing the breakpoint handler. Vectored exception handlers do not work on x64, so those are not employed here.

### Debug Registers Reference
* DR0, DR1, DR2, DR3 breakpoint address
* DR6: Low 4 bits contain the 1-based index of the debug register that tripped
	* 0x1: DR0
	* 0x2: DR1
	* 0x4: DR2
	* 0x8: DR3
* DR7: flags:
	* Bits 0-7: Flags for each of the 4 debug registers (2 for each). 
		* (1 << (reg * 2)) = Local/Process, 
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
* http://en.wikipedia.org/wiki/X86_debug_register
* [ThreadContext breakpoints in Windows] (http://www.codeproject.com/Articles/28071/Toggle-hardware-data-read-execute-breakpoints-prog)
* [Mac OSX] (http://stackoverflow.com/questions/2604439/how-do-i-write-x86-debug-registers-from-user-space-on-osx)
