/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <quickjs.h>
#include <cassert>

namespace jsuno
{
class ValueRef
{
public:
    ValueRef(JSContext* ctx, JSValue val = JS_UNINITIALIZED)
        : ctx_(ctx)
        , val_(val)
    {
    }

    ValueRef(ValueRef&& ref)
        : ctx_(ref.ctx_)
        , val_(ref.val_)
    {
        ref.val_ = JS_UNINITIALIZED;
    }

    ~ValueRef() { JS_FreeValue(ctx_, val_); }

    ValueRef& operator=(ValueRef&& ref)
    {
        assert(ctx_ == ref.ctx_);
        JS_FreeValue(ctx_, val_);
        val_ = ref.val_;
        ref.val_ = JS_UNINITIALIZED;
        return *this;
    }

    ValueRef& operator=(JSValue val)
    {
        JS_FreeValue(ctx_, val_);
        val_ = val;
        return *this;
    }

    operator JSValueConst() const { return val_; }

    JSValue dup() const { return JS_DupValue(ctx_, val_); }

    JSValue release()
    {
        auto const val = val_;
        val_ = JS_UNINITIALIZED;
        return val;
    }

    JSValue* ptr() { return &val_; }

private:
    ValueRef(ValueRef const&) = delete;
    void operator=(ValueRef const&) = delete;

    JSContext* ctx_;
    JSValue val_;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
