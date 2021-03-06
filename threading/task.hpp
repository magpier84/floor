/*
 *  Flo's Open libRary (floor)
 *  Copyright (C) 2004 - 2021 Florian Ziesche
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FLOOR_TASK_HPP__
#define __FLOOR_TASK_HPP__

#include <floor/core/essentials.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <functional>
#include <floor/threading/thread_safety.hpp>
using namespace std;

class task {
public:
	//! task constructor, don't use this directly (use task::spawn instead)
	task(std::function<void()> op, const string task_name);
	
	//! creates ("spawns") a new task that asynchronously executes the supplied function in a separate thread
	//! NOTE: memory management of the task object is not necessary as it will automatically self-destruct
	//! after completing the task op or after encountering an unhandled exception.
	//! NOTE: example usage: task::spawn([]() { cout << "do something in here" << endl; });
	static void spawn(std::function<void()> op, const string task_name = "task") {
#if !defined(__clang_analyzer__) // kill off memory leak warnings, it's supposed to work like this
		new task(op, task_name);
#endif
	}
	
protected:
	const std::function<void()> op;
	const string task_name;
	atomic<bool> initialized { false };
	thread thread_obj;
	
	//! the tasks threads run method (only used internally)
	static void run(task* this_task, std::function<void()> task_op);
	
	//! destruction from the outside is not allowed, since the task will automatically self-destruct
	~task() = default;
	// prohibit copying
	task(const task& tsk) = delete;
	task& operator=(const task& tsk) = delete;
	
};

#endif
