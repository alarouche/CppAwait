/*
* Copyright 2012-2013 Valentin Milea
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once

#include <CppAwait/misc/Functional.h>

namespace loo {

//
// AbstractScheduler
//

class AbstractScheduler
{
protected:
    AbstractScheduler() { }
public:
    virtual ~AbstractScheduler() { }

    virtual void schedule(const ut::Action& action) = 0;

    virtual void schedule(ut::Action&& action)
    {
        schedule(action);
    }

private:
    AbstractScheduler(const AbstractScheduler&);
    AbstractScheduler& operator=(const AbstractScheduler&);
};

//
// ImmediateScheduler
//

class ImmediateScheduler : public AbstractScheduler
{
public:
    ImmediateScheduler() { }

    void schedule(const ut::Action& action)
    {
        action();
    }
};

//
// helpers
//

AbstractScheduler& mainScheduler();

template <typename Callable>
void schedule(Callable&& action, AbstractScheduler& scheduler = mainScheduler())
{
    scheduler.schedule(std::forward<Callable>(action));
}

}