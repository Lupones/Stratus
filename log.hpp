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

#include <iostream>
#include <sstream>

#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/global_logger_storage.hpp>


// These macros use the global variable lg
#define LOGDEB(msg) BOOST_LOG_SEV(general_log::logger::get(), general_log::SeverityLevel::debug)   << msg
#define LOGINF(msg) BOOST_LOG_SEV(general_log::logger::get(), general_log::SeverityLevel::info)    << msg
#define LOGWAR(msg) BOOST_LOG_SEV(general_log::logger::get(), general_log::SeverityLevel::warning) << msg
#define LOGERR(msg) BOOST_LOG_SEV(general_log::logger::get(), general_log::SeverityLevel::error)   << msg

// Note that LOGFAT inmediately exits
#define LOGFAT(msg) do {BOOST_LOG_SEV(general_log::logger::get(), general_log::SeverityLevel::fatal) << msg; exit(EXIT_FAILURE);} while(0)


namespace general_log
{
	enum class SeverityLevel
	{
		debug = 0,
		info,
		warning,
		error,
		fatal,
	};

	// Declare a global logger
	BOOST_LOG_INLINE_GLOBAL_LOGGER_DEFAULT(logger, boost::log::sources::severity_logger_mt<general_log::SeverityLevel>);

	// Initialize the general application events log
	void init(const std::string &logfile, SeverityLevel clog_min, SeverityLevel flog_min);

	// SeverityLevel from integer
	SeverityLevel severity_level(size_t level);

	// SeverityLevel from string
	SeverityLevel severity_level(const std::string &level);

	// Pipe a SeverityLevel into an ostream
	std::ostream& operator<< (std::ostream& strm, SeverityLevel level);

	// SeverityLevel from string coming from istream
	std::istream& operator>> (std::istream& in, SeverityLevel& unit);
}


template <typename It, typename F>
std::string iterable_to_string(It first, It last, F func, std::string sep)
{
	size_t n = std::distance(first, last);
	if (n <= 0) return "";

	std::stringstream ss;
	It it = first;
	for (size_t i = 0; i < n - 1; i++)
	{
		ss << func(*it) << sep;
		it++;
	}
	ss << func(*it);

	return ss.str();
}
