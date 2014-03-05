
#include <cassert>
#include <iostream>

#include "dubstep.h"

void BreakpointHandler(void* address)
{
	std::cout << "Breakpoint hit @ 0x" << std::hex << reinterpret_cast<DWORD_PTR>(address) << std::endl;
}

int ExceptionHandler(EXCEPTION_POINTERS* ex)
{
	return -1;
}

int main(int argc, char* argv[])
{
	wchar_t buf[32];
	lstrcpy(buf, L"This is a TEST");

	dubstep::SetBreakpointHandler(BreakpointHandler);

	HANDLE bp = dubstep::SetBreakpoint(dubstep::TYPE_Access, buf, dubstep::SIZE_4);
	assert(bp != 0);

	EXCEPTION_POINTERS* ex = NULL;
	__try
	{
		buf[1] = L'f';
	}
	//__except (dubstep::Breakpoint::FilterException(GetExceptionInformation()))
	__except(ExceptionHandler(GetExceptionInformation()))
	{
		std::cout << "write trapped" << std::endl;
	}

	bool cleared = dubstep::ClearBreakpoint(bp);
	assert(cleared);

	return 0;
}
