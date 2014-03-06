
#include <cassert>
#include <iostream>

#include "dubstep.h"

void BreakpointHandler(void* address)
{
	std::cout << "Breakpoint hit @ 0x" << std::hex << reinterpret_cast<DWORD_PTR>(address) << std::endl;
}

int main(int argc, char* argv[])
{
	wchar_t buf[32];
	lstrcpy(buf, L"This is a TEST");

	dubstep::SetBreakpointHandler(BreakpointHandler);

	HANDLE bp = dubstep::SetBreakpoint(dubstep::TYPE_Access, buf, dubstep::SIZE_4);
	assert(bp != 0);

#ifdef DEBUG_EXCEPTION_FILTER
	__try
	{
		buf[1] = L'f';
	}
	__except (dubstep::Breakpoint::FilterException(GetExceptionInformation()))
	{
		std::cout << "write trapped" << std::endl;
	}
#else
	buf[1] = L'f';
#endif

	bool cleared = dubstep::ClearBreakpoint(bp);
	assert(cleared);

	return 0;
}
