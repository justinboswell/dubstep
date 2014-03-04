
# A Library for creating hardware execution and data breakpoints dynamically on Win32/Win64


### Debug Registers
DR0, DR1, DR2, DR3 breakpoint address
DR7: flags:

Bits	Meaning
* Bits 0-7	Flags for each of the 4 debug registers (2 for each). The first flag is set to specify a local breakpoint (so the CPU resets the flag when switching tasks), and the second flag is set to specify a global breakpoint. In Windows, obviously, you can only use the first flag (although I haven't tried the second).
* Bits 16-23 :  2 bits for each register, defining when the breakpoint will be triggered:
** 00b - Triggers when code is executed
** 01b - Triggers when data is written
** 10b - Reserved
** 11b - Triggers when data is read or written
* Bits 24-31: 2 bits for each register, defining the size of the breakpoint:
** 00b - 1 byte
** 01b - 2 bytes
** 10b - 8 bytes
** 11b - 4 bytes

### References
http://www.codeproject.com/Articles/28071/Toggle-hardware-data-read-execute-breakpoints-prog
