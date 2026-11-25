////////////////////////////////////////////////////////////////////////////////////////
//
// Nestopia - NES/Famicom emulator written in C++
//
// Copyright (C) 2003-2008 Martin Freij
//
// This file is part of Nestopia.
//
// Nestopia is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Nestopia is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Nestopia; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
////////////////////////////////////////////////////////////////////////////////////////

#ifndef NST_API_DEBUGGER_H
#define NST_API_DEBUGGER_H

#include <map>
#include <memory>
#include <string>
#include "../NstCpu.hpp"
#include "NstApi.hpp"
#include "NstApiDebuggerCondition.hpp"

namespace Nes
{
	namespace Api
	{
		/**
		* CPU debugger interface.
		*
		* Installing a callback switches the CPU to a separate run loop that checks
		* breakpoints and step requests before every instruction. Without a
		* callback the normal loops run and the debugger costs nothing.
		*
		* The callback runs on the emulation thread before the instruction at
		* Event::address and fills in a Request saying how to continue. It may
		* block to expose a stopped machine to another thread. All debugger
		* methods must be serialized with emulator execution. Do not reset,
		* unload, load a state or save a state while the callback is blocked;
		* the current frame's execution stack is still active.
		*
		* None of the inspection methods change the emulated machine. Memory
		* reads bypass I/O registers and mapper read handlers, and the PPU is not
		* caught up to the CPU to report its state.
		*/
		class Debugger : public Base
		{
		public:

			template<typename T>
			Debugger(T& instance)
			: Base(instance), callback(nullptr), userData(nullptr) {}

			~Debugger();

			enum Reason
			{
				/** An execute breakpoint was reached. */
				EXEC_BREAKPOINT,
				/** A step or run-to request completed. */
				STEP_COMPLETE,
				/** RequestBreak() took effect. */
				BREAK_REQUEST
			};

			struct Event
			{
				uint address;
				Reason reason;
				/** Master clocks, see GetCycles(). */
				qaword cycles;
			};

			struct Request
			{
				enum Mode
				{
					/** Run until a breakpoint or RequestBreak(). */
					RUN,
					/** Stop before the next instruction. */
					STEP_INTO,
					/** Like STEP_INTO but runs a called subroutine, a BRK handler or an interrupt handler to completion. */
					STEP_OVER,
					/** Run until an RTS or RTI returns from the current stack level. */
					STEP_OUT,
					/** Run until the instruction at Request::address. */
					RUN_TO_ADDRESS,
					/** Run for at least Request::count CPU cycles. */
					RUN_CYCLES,
					/** Run to the first instruction on the next scanline. */
					RUN_TO_SCANLINE,
					/** Run to the first instruction of the next emulated frame. */
					RUN_TO_FRAME
				};

				Mode mode;
				uint address;
				qaword count;
			};

			typedef void (NST_CALLBACK *Callback)(UserData,const Event&,Request&);

			struct Registers
			{
				uint pc;
				uint a;
				uint x;
				uint y;
				uint sp;
				uint p;
			};

			struct PpuState
			{
				/** -1 for the pre-render line. */
				int scanline;
				uint dot;
				uint linesPerFrame;
				/** $2000 */
				uint ctrl;
				/** $2001 */
				uint mask;
				/** $2002 flag bits as of the last PPU synchronization. */
				uint status;
				/** $2003 */
				uint oamAddress;
				/** Internal v, t, x and w registers. */
				uint vramAddress;
				uint tempAddress;
				uint fineX;
				uint writeToggle;
			};

			/** Enables the debugger run loop, or disables it when the callback is null. */
			void SetCallback(Callback,UserData=nullptr);
			/**
			* Sets or removes a CPU address execute breakpoint. With a condition, see
			* DebuggerCondition, the breakpoint only halts when the expression is non-zero
			* before the instruction executes. Null or blank text means unconditional.
			* RESULT_ERR_INVALID_PARAM is returned for a bad address or condition and the
			* previous breakpoint at the address is kept. Disabling removes the condition.
			*/
			Result SetExecBreakpoint(uint,bool=true,const char* condition=nullptr) throw();
			/** Checks the syntax of a condition and describes the first error. Blank text is valid. */
			static Result ValidateCondition(const char*,std::string* error=nullptr);
			/** Removes all execute breakpoints. */
			void ClearExecBreakpoints();
			/** Stops before the next instruction executed while a callback is installed. */
			void RequestBreak();

			/** Returns the address of the next CPU instruction. */
			uint GetProgramCounter() const;
			void GetRegisters(Registers&) const;
			/** Only safe while the callback is blocked or between frames. */
			void SetRegisters(const Registers&);

			/** CPU bus byte without side effects, or -1 for I/O and unmapped addresses. */
			int Peek(uint) const;
			/** Offset into PRG ROM of a CPU address as currently banked, or -1. */
			long GetPrgRomOffset(uint) const;
			void GetPpuState(PpuState&) const;

			/** Monotonic master clock count. */
			qaword GetCycles() const;
			/** Monotonic CPU cycle count. */
			qaword GetCpuCycles() const;
			/** Instructions executed in the debugger run loop. */
			qaword GetInstructionCount() const;
			void ResetInstructionCount();

		private:

			static void NST_REGCALL Dispatch(void*,const Core::Cpu::DebugEvent&,Core::Cpu::DebugRequest&);
			static bool NST_REGCALL EvaluateCondition(void*,uint);
			static int PeekThunk(void*,unsigned);

			Callback callback;
			UserData userData;
			std::map<uint,std::shared_ptr<DebuggerCondition> > conditions;
		};
	}
}

#endif
