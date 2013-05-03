/*
* Copyright 2012 Valentin Milea
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

#include "../Config.h"
#include <memory>

namespace ut {

//
// helps ignore callbacks that arrive too late
//

class CallbackGuard
{
public:
    class Token
    {
    public:
        Token(Token&& other)
            : mIsBlocked(std::move(other.mIsBlocked)) { }

        Token& operator=(Token&& other)
        {
            mIsBlocked = std::move(other.mIsBlocked);
            return *this;
        }

        bool isBlocked() const
        {
            return *mIsBlocked;
        }

    private:
        explicit Token(std::shared_ptr<bool> isBlocked)
            : mIsBlocked(isBlocked) { }

        std::shared_ptr<bool> mIsBlocked;

        friend class CallbackGuard;
    };

    CallbackGuard()
        : mIsBlocked(std::make_shared<bool>(false)) { }

    ~CallbackGuard()
    {
        block();
    }

    Token getToken() const
    {
        return Token(mIsBlocked);
    }

    void block()
    {
        *mIsBlocked = true;
    }

private:
    std::shared_ptr<bool> mIsBlocked;
};

}
