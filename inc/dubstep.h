// The MIT License (MIT)
//
// Copyright (c) 2014 Justin Boswell <justin.boswell@gmail.com>
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

#if defined(_MSC_VER) || defined(WIN32)
#define DUBSTEP_PLATFORM_WINDOWS
#include <windows.h>
#elif defined(__linux__) || defined(LINUX)
#define DUBSTEP_PLATFORM_LINUX
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/wait.h>
#elif defined(__APPLE__)
#define DUBSTEP_PLATFORM_OSX
#endif

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

	struct BreakpointBase
	{
		BreakpointType Type;
		BreakpointSize Size;
		void* Address;
		volatile bool Enabled;
		int Register;
		
		BreakpointBase(BreakpointType type, void* address, BreakpointSize size)
			: Type(type)
			, Size(size)
			, Address(address)
			, Register(-1)
			, Enabled(false)
		{
		}

		virtual ~BreakpointBase() {}

		virtual void Attach() = 0;
		virtual void Detach() = 0;

		static BreakpointHandler Handler;
	};

#if defined(DUBSTEP_PLATFORM_WINDOWS)
	template<Scope S>
	struct Breakpoint : public BreakpointBase
	{
		HANDLE Thread;
		HANDLE Complete;


		Breakpoint(BreakpointType type, void* address, BreakpointSize size)
			: BreakpointBase(type, address, size)
			, Thread(INVALID_HANDLE_VALUE)
			, Complete(INVALID_HANDLE_VALUE)
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

		virtual bool Attach()
		{
			Enabled = true;

			return WriteThreadContext(&AddToThreadContext);
		}

		virtual bool Detach()
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
				::CreateThread(NULL, 0, threadProc, static_cast<LPVOID>(this), 0, NULL);
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
			for (int idx = 0; idx < 4; ++idx)
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
	};
#elif defined(DUBSTEP_PLATFORM_LINUX)
	template <Scope S>
	struct Breakpoint : public BreakpointBase
	{
		Breakpoint(BreakpointType type, void* address, BreakpointSize size)
			: BreakpointBase(type, address, size)
		{
		}

		virtual bool Attach()
		{
			Enabled = true;
			
			pid_t pid = getpid();
			pid_t tracer = 0;

			if ((tracer = fork()) == 0)
			{
				unsigned long dr7 = 0; // TODO
				struct user u = {0};
				if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0)
					exit(1);
				if (ptrace(PTRACE_POKEUSER, pid, offsetof(struct user, u_debugreg[0], address) != 0)
					exit(2);
				if (ptrace(PTRACE_POKEUSER, pid, offsetof(struct user, u_debugreg[7], dr7) != 0)
					exit(4);
				if (ptrace(PTRACE_DETACH, pid, NULL, NULL) != 0)
					exit(8);
				exit(0);
			}
			
			int tracerExit = 0;
			waitpid(tracer, &tracerExit, 0);
			if (WEXITSTATUS(tracerExit) != 0)
				return false;
				
			return true;
		}

		virtual bool Detach()
		{
			Enabled = false;

			pid_t pid = getpid();
			pid_t tracer = 0;
			if ((tracer = fork()) == 0)
			{
				unsigned long dr7 = 0; // TODO
				struct user u = {0};
				if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) != 0)
					exit(1);
				if (ptrace(PTRACE_POKEUSER, pid, offsetof(struct user, u_debugreg[0], Address) != 0)
					exit(2);
				if (ptrace(PTRACE_POKEUSER, pid, offsetof(struct user, u_debugreg[7], dr7) != 0)
					exit(4);
				if (ptrace(PTRACE_DETACH, pid, NULL, NULL) != 0)
					exit(8);
				exit(0);
			}
			
			int tracerExit = 0;
			waitpid(tracer, &tracerExit, 0);
			if (WEXITSTATUS(tracerExit) != 0)
				return false;
			return true;
		}
	};
#elif defined(DUBSTEP_PLATFORM_OSX)
	template <Scope S>
	struct Breakpoint: public BreakpointBase
	{
		Breakpoint(BreakpointType type, void* address, BreakpointSize size)
			: BreakpointBase(type, address, size)
		{
		}

		virtual bool Attach()
		{
			Enabled = true;

			return true;
		}

		virtual bool Detach()
		{
			Enabled = false;

			return true;
		}
	};
#endif
} // namespace dubstep::internal

typedef internal::Breakpoint<internal::SCOPE_Local> Breakpoint;
typedef void* BreakpointHandle;

template <internal::Scope S>
BreakpointHandler internal::Breakpoint<S>::Handler = NULL;

void SetBreakpointHandler(BreakpointHandler handler)
{
	Breakpoint::Handler = handler;

	// install the exception filter if user has requested notification,
	// otherwise clear it
	if (handler)
		::SetUnhandledExceptionFilter(Breakpoint::FilterException);
	else
		::SetUnhandledExceptionFilter(NULL);
}

BreakpointHandle SetBreakpoint(BreakpointType type, void *address, BreakpointSize size)
{
	Breakpoint* breakpoint = new Breakpoint(type, address, size);
	if (!breakpoint->Attach())
	{
		delete breakpoint;
		return 0;
	}

	return reinterpret_cast<BreakpointHandle>(breakpoint);
}

bool ClearBreakpoint(BreakpointHandle bph)
{
	Breakpoint* breakpoint = reinterpret_cast<Breakpoint*>(bph);

	bool detached = breakpoint->Detach();
	delete breakpoint;

	return detached;
}

} // namespace dubstep

#endif // DUBSTEP_H_INC
