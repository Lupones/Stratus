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

#include "app-task.hpp"
#include "intel-rdt.hpp"
#include "vm-task.hpp"

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/rolling_variance.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <deque>
#include <set>

typedef std::shared_ptr<Task> task_ptr_t;
typedef std::vector<task_ptr_t> tasklist_t;

namespace cat
{
const uint32_t min_num_ways = 2;
const uint32_t max_num_ways = 20;
const uint32_t complete_mask = ~(-1U << max_num_ways);

namespace policy
{
namespace acc = boost::accumulators;

// Base class that does nothing
class Base
{
  protected:
    std::shared_ptr<IntelRDT> cat;

  public:
    Base() = default;

    void set_cat(std::shared_ptr<IntelRDT> _cat)
    {
        cat = _cat;
    }
    std::shared_ptr<IntelRDT> get_cat()
    {
        return cat;
    }
    const std::shared_ptr<IntelRDT> get_cat() const
    {
        return cat;
    }

    void set_cbms(const cbms_t &cbms)
    {
        assert(cat->get_max_closids() >= cbms.size());
        for (size_t clos = 0; clos < cbms.size(); clos++) {
            get_cat()->set_cbm(clos, 0, cbms[clos], 0);
            //get_cat()->set_cbm(clos, cbms[clos], "data");
        }
    }

    virtual ~Base() = default;

    // Derived classes should perform their operations here.
    // The base class does nothing by default.
    virtual void apply(uint64_t, double, double, const tasklist_t &)
    {
    }
};

// Test partitioning policy
class Test : public Base
{
  protected:
    uint64_t every = -1;

  public:
    virtual ~Test() = default;
    Test(uint64_t _every) : every(_every)
    {
    }
    virtual void apply(uint64_t, double, double, const tasklist_t &) override;
};
typedef Test Tt;

} // namespace policy
} // namespace cat
