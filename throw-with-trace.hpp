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

#pragma once

#include <boost/stacktrace.hpp>
#include <boost/exception/all.hpp>


typedef boost::error_info<struct tag_stacktrace, boost::stacktrace::stacktrace> traced;

template <class E>
[[noreturn]] void throw_with_trace(const E& e)
{
	throw boost::enable_error_info(e) << traced(boost::stacktrace::stacktrace());
}
