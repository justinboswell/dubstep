# DuBStep

## A Library for creating hardware execution and data breakpoints at runtime on Win32/Win64

### Debug Registers
DR0, DR1, DR2, DR3 breakpoint address
DR7: flags:

Bits	Meaning
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
http://www.codeproject.com/Articles/28071/Toggle-hardware-data-read-execute-breakpoints-prog
