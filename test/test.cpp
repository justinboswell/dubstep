
#include <cassert>
#include <iostream>

#include "dubstep.h"

int main(int argc, char* argv[])
{
	wchar_t buf[32];
	lstrcpy(buf, L"This is a TEST");

	HANDLE bp = dubstep::SetBreakpoint(dubstep::TYPE_Access, buf, dubstep::SIZE_4);
	assert(bp != 0);

	__try
	{
		buf[1] = L'f';
	}
	__except (::GetExceptionCode() == EXCEPTION_SINGLE_STEP)
	{
		std::cout << "write trapped" << std::endl;
	}

	return 0;
}
