
// The MIT License (MIT)
//
// Copyright (c) 2014 Justin Boswell
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
// THE SOFTWARE.

#ifndef DUBSTEP_H_INC
#define DUBSTEP_H_INC

#include <windows.h>

namespace dubstep {

enum BreakpointType
{
	TYPE_Exec   = 0,
	TYPE_Access = 3,
	TYPE_Write  = 1
};

enum BreakpointSize
{
	SIZE_1 = 0,
	SIZE_2 = 1,
	SIZE_4 = 3,
	SIZE_8 = 2
};

typedef void (*BreakpointHandler)(void*);

namespace internal
{
	enum Scope
	{
		SCOPE_Local,
		SCOPE_Global, // Need kernel privileges, so unimplemented for now
	};

	// enum RegisterFlags
	// {
	// 	REGISTER_0 = 0x0001,
	// 	REGISTER_1 = 0x0004,
	// 	REGISTER_2 = 0x0010,
	// 	REGISTER_3 = 0x0040,
	// };

	template<Scope S>
	class Breakpoint
	{
	public:
		BreakpointType Type;
		BreakpointSize Size;
		void* Address;
		HANDLE Thread;
		HANDLE Complete;
		volatile bool Enabled;
		int Register;

		Breakpoint(BreakpointType type, void* address, BreakpointSize size)
		: Type(type)
		, Size(size)
		, Address(address)
		, Thread(INVALID_HANDLE_VALUE)
		, Complete(INVALID_HANDLE_VALUE)
		, Register(-1)
		, Enabled(false)
		{
		}

		~Breakpoint()
		{	
		}

		// Get access to the thread's context with full privileges
		bool OpenThread()
		{
			Thread = ::OpenThread(THREAD_ALL_ACCESS, FALSE, GetCurrentThreadId());
			return Thread != 0;
		}

		void CloseThread()
		{
			::CloseHandle(Thread);
		}

		bool Attach()
		{
			Enabled = true;

			return WriteThreadContext(&AddToThreadContext);
		}

		bool Detach()
		{
			Enabled = false;

			return WriteThreadContext(&RemoveFromThreadContext);
		}
		
		// Thread context cannot be written for a thread that is currently running,
		// therefore spawn a new thread whose only job is to suspend the current
		// thread, update its context, and resume it.
		bool WriteThreadContext(LPTHREAD_START_ROUTINE threadProc)
		{
			if (OpenThread())
			{
				// Create event to signal that context write is complete, create a thread and then wait until
				// it adds the breakpoint to this thread's context
				Complete = ::CreateEvent(NULL, FALSE, FALSE, NULL);
				HANDLE contextWriter = ::CreateThread(NULL, 0, threadProc, static_cast<LPVOID>(this), 0, NULL);
				::WaitForSingleObject(Complete, INFINITE);
				::CloseHandle(Complete);
				Complete = INVALID_HANDLE_VALUE;

				CloseThread();

				return Register != -1;
			}
			
			return false;
		}

		static DWORD WINAPI AddToThreadContext(LPVOID param)
		{
			Breakpoint* breakpoint = static_cast<Breakpoint*>(param);

			// Stop the thread
			::SuspendThread(breakpoint->Thread);

			// read the current context so we can find an open register
			CONTEXT ctx = {0};
			ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
			::GetThreadContext(breakpoint->Thread, &ctx);

			// find the first available register
			int regIdx = -1;
			for (int idx = 1; idx < 4; ++idx)
			{
				const unsigned regFlag = (1 << (idx * 2));
				if ((ctx.Dr7 & regFlag) == 0)
				{
					regIdx = idx;
					break;
				}
			}

			if (regIdx != -1)
			{
				breakpoint->Register = regIdx;
				switch (regIdx)
				{
					case 0: ctx.Dr0 = reinterpret_cast<DWORD_PTR>(breakpoint->Address); break;
					case 1: ctx.Dr1 = reinterpret_cast<DWORD_PTR>(breakpoint->Address); break;
					case 2: ctx.Dr2 = reinterpret_cast<DWORD_PTR>(breakpoint->Address); break;
					case 3: ctx.Dr3 = reinterpret_cast<DWORD_PTR>(breakpoint->Address); break;
				}

				// Compute register flags
				const unsigned typeShift = 16 + (regIdx * 4);
				const unsigned sizeShift = 18 + (regIdx * 4);
				const unsigned typeFlags = (breakpoint->Type << typeShift);
				const unsigned sizeFlags = (breakpoint->Size << sizeShift);
				const unsigned regFlags = (1 << (regIdx*2));
				const unsigned mask = (0x3 << typeShift) | (0x3 << sizeShift);
				
				// Update debug register flags
				ctx.Dr7 = (ctx.Dr7 & ~mask) | (regFlags | typeFlags | sizeFlags);

				// write the context back to the thread
				ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
				::SetThreadContext(breakpoint->Thread, &ctx);
			}
			else
			{
				breakpoint->Enabled = false;
			}

			// resume the thread and notify the caller that work is complete
			::ResumeThread(breakpoint->Thread);
			::SetEvent(breakpoint->Complete);

			return 0;
		}

		static DWORD WINAPI RemoveFromThreadContext(LPVOID param)
		{
			Breakpoint* breakpoint = static_cast<Breakpoint*>(param);

			// Stop the thread
			::SuspendThread(breakpoint->Thread);

			CONTEXT ctx = {0};
			ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

			// Disable the breakpoint
			unsigned regFlag = (1 << (breakpoint->Register * 2));
			ctx.Dr7 &= ~regFlag;

			// write the debug registers back to the thread context
			::SetThreadContext(breakpoint->Thread, &ctx);

			// restart the thread and signal that work is complete
			::ResumeThread(breakpoint->Thread);
			::SetEvent(breakpoint->Complete);

			return 0;
		}

		static LONG WINAPI FilterException(LPEXCEPTION_POINTERS ex)
		{
			if (ex->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP)
			{
				if (Handler)
				{
					// Dr6's low bits (0-3) will contain the debug register that tripped
					unsigned regBit = ex->ContextRecord->Dr6 & 0x0f;
					void* address = NULL;
					switch (regBit)
					{
						case 1: address = reinterpret_cast<void*>(ex->ContextRecord->Dr0); break;
						case 2: address = reinterpret_cast<void*>(ex->ContextRecord->Dr1); break;
						case 4: address = reinterpret_cast<void*>(ex->ContextRecord->Dr2); break;
						case 8: address = reinterpret_cast<void*>(ex->ContextRecord->Dr3); break;
					}
					(*Handler)(address);
				}
				return EXCEPTION_CONTINUE_EXECUTION;
			}

			return EXCEPTION_CONTINUE_SEARCH;
		}

		static BreakpointHandler Handler;
	};
} // namespace dubstep::internal

typedef internal::Breakpoint<internal::SCOPE_Local> Breakpoint;

template <internal::Scope S>
BreakpointHandler internal::Breakpoint<S>::Handler = NULL;

void SetBreakpointHandler(BreakpointHandler handler)
{
	Breakpoint::Handler = handler;
}

HANDLE SetBreakpoint(BreakpointType type, void *address, BreakpointSize size)
{
    Breakpoint* breakpoint = new Breakpoint(type, address, size);
	if (!breakpoint->Attach())
	{
		delete breakpoint;
		return 0;
	}

	// install the exception filter if user has requested notification
	static volatile bool filterInstalled = false;
	if (Breakpoint::Handler && !filterInstalled)
	{
		::SetUnhandledExceptionFilter(Breakpoint::FilterException);
		filterInstalled = true;
	}

	return reinterpret_cast<HANDLE>(breakpoint);
}

bool ClearBreakpoint(HANDLE bph)
{
    Breakpoint* breakpoint = reinterpret_cast<Breakpoint*>(bph);

	bool detached = breakpoint->Detach();
	delete breakpoint;

	return detached;
}

} // namespace dubstep

#endif // DUBSTEP_H_INC
