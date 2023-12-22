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

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/log/support/date_time.hpp>

#include "log.hpp"
#include "throw-with-trace.hpp"


namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;


BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", general_log::SeverityLevel);


namespace general_log
{
	const std::vector<std::string> strings =
	{
		"dbg",
		"inf",
		"war",
		"err",
		"fat",
	};


	SeverityLevel severity_level(size_t level)
	{
		if (level >= strings.size())
		{
			std::string allowed;
			for (auto const &s : strings)
				allowed += s + " ";
			throw_with_trace(std::runtime_error("The allowed log levels are: " + allowed));
		}
		return static_cast<SeverityLevel>(level);
	}


	SeverityLevel severity_level(const std::string &level)
	{
		size_t i;
		for(i = 0; i < strings.size(); i++)
		{
			if (level == strings[i])
				break;
		}
		return severity_level(i);
	}


	void init(const std::string &logfile, SeverityLevel clog_min, SeverityLevel flog_min)
	{
		auto format = expr::stream
				<< "[" << expr::format_date_time< boost::posix_time::ptime >("TimeStamp", "%Y-%m-%d %H:%M:%S.%f")
				<< " " << severity << "] "
				<< expr::smessage;

		auto fsink = logging::add_file_log
		(
			keywords::file_name = logfile,
			keywords::format = format
		);

		auto csink = logging::add_console_log
		(
			std::clog,
			keywords::format = format
		);

		fsink->locked_backend()->auto_flush(true);
		fsink->set_filter
		(
			severity >= flog_min
		);

		csink->locked_backend()->auto_flush(true);
		csink->set_filter
		(
			severity >= clog_min
		);

		logging::add_common_attributes();
	}


	std::ostream& operator<< (std::ostream& strm, SeverityLevel level)
	{
		auto l = static_cast< std::size_t >(level);
		if (l < strings.size())
			strm << strings[l];
		else
			strm << l;

		return strm;
	}

	std::istream& operator>> (std::istream& in, SeverityLevel& unit)
	{
		std::string token;
		in >> token;
		unit = severity_level(token);
		return in;
	}
}
