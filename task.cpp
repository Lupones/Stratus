/*
 * Copyright 2023 Universitat Politècnica de València

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <vector>

#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <boost/filesystem.hpp>
#include <cxx-prettyprint/prettyprint.hpp>
#include <fmt/format.h>
#include <glib.h>

#include "log.hpp"
#include "task.hpp"
#include "throw-with-trace.hpp"

#include "pstream.h"
#include <bitset>

namespace acc = boost::accumulators;
namespace fs = boost::filesystem;

using std::cerr;
using std::endl;
using std::string;
using std::to_string;
using fmt::literals::operator""_format;


//Init static atribute
std::atomic<uint32_t> Task::ID(0);


Task::~Task()
{
}

const std::string Task::status_to_str(const Task::Status &s)
{
    switch (s) {
        case Status::runnable:
            return "runnable";
        case Status::limit_reached:
            return "limit_reached";
        case Status::exited:
            return "exited";
        case Status::done:
            return "done";
    }
    throw_with_trace(
        std::runtime_error("Unknown status, should not reach this"));
}

const std::string Task::status_to_str() const
{
    return Task::status_to_str(status);
}

const Task::Status &Task::get_status() const
{
    return status;
}

void Task::set_status(const Task::Status &new_status)
{
    LOGDEB("Task {}:{} changes its status from {} to {}"_format(
        id, name, Task::status_to_str(), Task::status_to_str(new_status)));
    status = new_status;
}

