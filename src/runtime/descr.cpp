// Copyright (c) 2014-2016 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "capi/typeobject.h"
#include "codegen/compvars.h"
#include "runtime/objmodel.h"
#include "runtime/rewrite_args.h"
#include "runtime/types.h"

namespace pyston {

static void propertyDocCopy(BoxedProperty* prop, Box* fget) {
    assert(prop);
    assert(fget);
    Box* get_doc;

    static BoxedString* doc_str = getStaticString("__doc__");
    try {
        get_doc = getattrInternal<ExceptionStyle::CXX>(fget, doc_str);
    } catch (ExcInfo e) {
        if (!e.matches(Exception)) {
            throw e;
        }
        e.clear();
        get_doc = NULL;
    }

    if (get_doc) {
        if (prop->cls == property_cls) {
            Py_XDECREF(prop->prop_doc);
            prop->prop_doc = get_doc;
        } else {
            /* If this is a property subclass, put __doc__
            in dict of the subclass instance instead,
            otherwise it gets shadowed by __doc__ in the
            class's dict. */
            setattr(prop, doc_str, get_doc);
        }
        prop->getter_doc = true;
    }
}

static Box* propertyInit(Box* _self, Box* fget, Box* fset, Box** args) {
    RELEASE_ASSERT(isSubclass(_self->cls, property_cls), "");
    Box* fdel = args[0];
    Box* doc = args[1];

    BoxedProperty* self = static_cast<BoxedProperty*>(_self);
    Box* prev_get = self->prop_get;
    Box* prev_set = self->prop_set;
    Box* prev_del = self->prop_del;
    Box* prev_doc = self->prop_doc;
    self->prop_get = fget == None ? NULL : incref(fget);
    self->prop_set = fset == None ? NULL : incref(fset);
    self->prop_del = fdel == None ? NULL : incref(fdel);
    self->prop_doc = xincref(doc);
    self->getter_doc = false;
    Py_XDECREF(prev_get);
    Py_XDECREF(prev_set);
    Py_XDECREF(prev_del);
    Py_XDECREF(prev_doc);

    /* if no docstring given and the getter has one, use that one */
    if ((doc == NULL || doc == None) && fget != NULL) {
        propertyDocCopy(self, fget);
    }

    return incref(None);
}

static Box* propertyGet(Box* self, Box* obj, Box* type) {
    RELEASE_ASSERT(isSubclass(self->cls, property_cls), "");

    BoxedProperty* prop = static_cast<BoxedProperty*>(self);
    if (obj == NULL || obj == None) {
        return incref(self);
    }

    if (prop->prop_get == NULL) {
        raiseExcHelper(AttributeError, "unreadable attribute");
    }

    return runtimeCall(prop->prop_get, ArgPassSpec(1), obj, NULL, NULL, NULL, NULL);
}

static Box* propertySet(Box* self, Box* obj, Box* val) {
    RELEASE_ASSERT(isSubclass(self->cls, property_cls), "");

    BoxedProperty* prop = static_cast<BoxedProperty*>(self);
    Box* func;
    if (val == NULL) {
        func = prop->prop_del;
    } else {
        func = prop->prop_set;
    }

    if (func == NULL) {
        raiseExcHelper(AttributeError, val == NULL ? "can't delete attribute" : "can't set attribute");
    }

    if (val == NULL) {
        autoDecref(runtimeCall(func, ArgPassSpec(1), obj, NULL, NULL, NULL, NULL));
    } else {
        autoDecref(runtimeCall(func, ArgPassSpec(2), obj, val, NULL, NULL, NULL));
    }
    return incref(None);
}

static Box* propertyDel(Box* self, Box* obj) {
    return propertySet(self, obj, NULL);
}

static Box* property_copy(BoxedProperty* old, Box* get, Box* set, Box* del) {
    RELEASE_ASSERT(isSubclass(old->cls, property_cls), "");

    if (!get || get == None)
        get = old->prop_get;
    if (!set || set == None)
        set = old->prop_set;
    if (!del || del == None)
        del = old->prop_del;

    // Optimization for the case when the old propery is not subclassed
    if (old->cls == property_cls) {
        BoxedProperty* prop = new BoxedProperty(get, set, del, old->prop_doc);

        prop->getter_doc = false;
        if ((old->getter_doc && get != None) || !old->prop_doc)
            propertyDocCopy(prop, get);

        return prop;
    } else {
        if (!get)
            get = None;
        if (!set)
            set = None;
        if (!del)
            del = None;
        Box* doc;
        if ((old->getter_doc && get != None) || !old->prop_doc)
            doc = None;
        else
            doc = old->prop_doc;

        return runtimeCall(old->cls, ArgPassSpec(4), get, set, del, &doc, NULL);
    }
}

static Box* propertyGetter(Box* self, Box* obj) {
    RELEASE_ASSERT(isSubclass(self->cls, property_cls), "");
    BoxedProperty* prop = static_cast<BoxedProperty*>(self);
    return property_copy(prop, obj, NULL, NULL);
}

static Box* propertySetter(Box* self, Box* obj) {
    RELEASE_ASSERT(isSubclass(self->cls, property_cls), "");
    BoxedProperty* prop = static_cast<BoxedProperty*>(self);
    return property_copy(prop, NULL, obj, NULL);
}

static Box* propertyDeleter(Box* self, Box* obj) {
    RELEASE_ASSERT(isSubclass(self->cls, property_cls), "");
    BoxedProperty* prop = static_cast<BoxedProperty*>(self);
    return property_copy(prop, NULL, NULL, obj);
}

static Box* staticmethodInit(Box* _self, Box* f) {
    RELEASE_ASSERT(isSubclass(_self->cls, staticmethod_cls), "");
    BoxedStaticmethod* self = static_cast<BoxedStaticmethod*>(_self);
    Py_CLEAR(self->sm_callable);
    self->sm_callable = incref(f);

    return incref(None);
}

static Box* staticmethodGet(Box* self, Box* obj, Box* type) {
    RELEASE_ASSERT(isSubclass(self->cls, staticmethod_cls), "");

    BoxedStaticmethod* sm = static_cast<BoxedStaticmethod*>(self);

    if (sm->sm_callable == NULL) {
        raiseExcHelper(RuntimeError, "uninitialized staticmethod object");
    }

    return incref(sm->sm_callable);
}

extern "C" PyObject* PyClassMethod_New(PyObject* callable) noexcept {
    return new BoxedClassmethod(callable);
}

static Box* classmethodInit(Box* _self, Box* f) {
    RELEASE_ASSERT(isSubclass(_self->cls, classmethod_cls), "");
    BoxedClassmethod* self = static_cast<BoxedClassmethod*>(_self);
    Box* prev = self->cm_callable;
    self->cm_callable = incref(f);
    Py_XDECREF(prev);

    return incref(None);
}

static Box* classmethodGet(Box* self, Box* obj, Box* type) {
    RELEASE_ASSERT(isSubclass(self->cls, classmethod_cls), "");

    BoxedClassmethod* cm = static_cast<BoxedClassmethod*>(self);

    if (cm->cm_callable == NULL) {
        raiseExcHelper(RuntimeError, "uninitialized classmethod object");
    }

    if (type == NULL) {
        type = obj->cls;
    }

    return new BoxedInstanceMethod(type, cm->cm_callable, type);
}

// TODO this should be auto-generated as a slot wrapper:
Box* BoxedMethodDescriptor::__call__(BoxedMethodDescriptor* self, Box* obj, BoxedTuple* varargs, Box** _args) {
    BoxedDict* kwargs = static_cast<BoxedDict*>(_args[0]);
    return BoxedMethodDescriptor::tppCall<CXX>(self, NULL, ArgPassSpec(1, 0, true, true), obj, varargs, kwargs, NULL,
                                               NULL);
}

template <ExceptionStyle S>
Box* BoxedMethodDescriptor::tppCall(Box* _self, CallRewriteArgs* rewrite_args, ArgPassSpec argspec, Box* arg1,
                                    Box* arg2, Box* arg3, Box** args,
                                    const std::vector<BoxedString*>* keyword_names) noexcept(S == CAPI) {
    if (S == CAPI) {
        try {
            return tppCall<CXX>(_self, NULL, argspec, arg1, arg2, arg3, args, keyword_names);
        } catch (ExcInfo e) {
            setCAPIException(e);
            return NULL;
        }
    }

    STAT_TIMER(t0, "us_timer_boxedmethoddescriptor__call__", 10);

    assert(_self->cls == method_cls);
    BoxedMethodDescriptor* self = static_cast<BoxedMethodDescriptor*>(_self);

    int ml_flags = self->method->ml_flags;
    int call_flags = ml_flags & ~(METH_CLASS | METH_COEXIST | METH_STATIC);

    if (rewrite_args && !rewrite_args->func_guarded) {
        rewrite_args->obj->addAttrGuard(offsetof(BoxedMethodDescriptor, method), (intptr_t)self->method);
    }

    ParamReceiveSpec paramspec(0, 0, false, false);
    Box** defaults = NULL;
    if (call_flags == METH_NOARGS) {
        paramspec = ParamReceiveSpec(1, 0, false, false);
    } else if (call_flags == METH_VARARGS) {
        paramspec = ParamReceiveSpec(1, 0, true, false);
    } else if (call_flags == (METH_VARARGS | METH_KEYWORDS)) {
        paramspec = ParamReceiveSpec(1, 0, true, true);
    } else if (call_flags == METH_O) {
        paramspec = ParamReceiveSpec(2, 0, false, false);
    } else if ((call_flags & ~(METH_O3 | METH_D3)) == 0) {
        int num_args = 0;
        if (call_flags & METH_O)
            num_args++;
        if (call_flags & METH_O2)
            num_args += 2;

        int num_defaults = 0;
        if (call_flags & METH_D1)
            num_defaults++;
        if (call_flags & METH_D2)
            num_defaults += 2;

        paramspec = ParamReceiveSpec(1 + num_args, num_defaults, false, false);
        if (num_defaults) {
            static Box* _defaults[] = { NULL, NULL, NULL };
            assert(num_defaults <= 3);
            defaults = _defaults;
        }
    } else {
        RELEASE_ASSERT(0, "0x%x", call_flags);
    }

    bool arg1_class_guarded = false;
    if (rewrite_args && argspec.num_args >= 1) {
        // Try to do the guard before rearrangeArguments if possible:
        rewrite_args->arg1->addAttrGuard(offsetof(Box, cls), (intptr_t)arg1->cls);
        arg1_class_guarded = true;
    }

    auto continuation = [=](CallRewriteArgs* rewrite_args, Box* arg1, Box* arg2, Box* arg3, Box** args) {
        if (ml_flags & METH_CLASS) {
            rewrite_args = NULL;
            if (!PyType_Check(arg1))
                raiseExcHelper(TypeError, "descriptor '%s' requires a type but received a '%s'", self->method->ml_name,
                               getFullTypeName(arg1).c_str());
        } else {
            if (!isSubclass(arg1->cls, self->type))
                raiseExcHelper(TypeError, "descriptor '%s' requires a '%s' arg1 but received a '%s'",
                               self->method->ml_name, getFullNameOfClass(self->type).c_str(),
                               getFullTypeName(arg1).c_str());
        }

        if (rewrite_args && !arg1_class_guarded) {
            rewrite_args->arg1->addAttrGuard(offsetof(Box, cls), (intptr_t)arg1->cls);
        }

        Box* rtn;
        if (call_flags == METH_NOARGS) {
            {
                UNAVOIDABLE_STAT_TIMER(t0, "us_timer_in_builtins");
                rtn = (Box*)self->method->ml_meth(arg1, NULL);
            }
            if (rewrite_args)
                rewrite_args->out_rtn
                    = rewrite_args->rewriter->call(true, (void*)self->method->ml_meth, rewrite_args->arg1,
                                                   rewrite_args->rewriter->loadConst(0, Location::forArg(1)))
                          ->setType(RefType::OWNED);
        } else if (call_flags == METH_VARARGS) {
            {
                UNAVOIDABLE_STAT_TIMER(t0, "us_timer_in_builtins");
                rtn = (Box*)self->method->ml_meth(arg1, arg2);
            }
            if (rewrite_args)
                rewrite_args->out_rtn
                    = rewrite_args->rewriter->call(true, (void*)self->method->ml_meth, rewrite_args->arg1,
                                                   rewrite_args->arg2)->setType(RefType::OWNED);
        } else if (call_flags == (METH_VARARGS | METH_KEYWORDS)) {
            {
                UNAVOIDABLE_STAT_TIMER(t0, "us_timer_in_builtins");
                rtn = (Box*)((PyCFunctionWithKeywords)self->method->ml_meth)(arg1, arg2, arg3);
            }
            if (rewrite_args)
                rewrite_args->out_rtn
                    = rewrite_args->rewriter->call(true, (void*)self->method->ml_meth, rewrite_args->arg1,
                                                   rewrite_args->arg2, rewrite_args->arg3)->setType(RefType::OWNED);
        } else if (call_flags == METH_O) {
            {
                UNAVOIDABLE_STAT_TIMER(t0, "us_timer_in_builtins");
                rtn = (Box*)self->method->ml_meth(arg1, arg2);
            }
            if (rewrite_args)
                rewrite_args->out_rtn
                    = rewrite_args->rewriter->call(true, (void*)self->method->ml_meth, rewrite_args->arg1,
                                                   rewrite_args->arg2)->setType(RefType::OWNED);
        } else if ((call_flags & ~(METH_O3 | METH_D3)) == 0) {
            {
                UNAVOIDABLE_STAT_TIMER(t0, "us_timer_in_builtins");
                rtn = ((Box * (*)(Box*, Box*, Box*, Box**))self->method->ml_meth)(arg1, arg2, arg3, args);
            }
            if (rewrite_args) {
                if (paramspec.totalReceived() == 2)
                    rewrite_args->out_rtn
                        = rewrite_args->rewriter->call(true, (void*)self->method->ml_meth, rewrite_args->arg1,
                                                       rewrite_args->arg2)->setType(RefType::OWNED);
                else if (paramspec.totalReceived() == 3)
                    rewrite_args->out_rtn
                        = rewrite_args->rewriter->call(true, (void*)self->method->ml_meth, rewrite_args->arg1,
                                                       rewrite_args->arg2, rewrite_args->arg3)->setType(RefType::OWNED);
                else if (paramspec.totalReceived() > 3)
                    rewrite_args->out_rtn
                        = rewrite_args->rewriter->call(true, (void*)self->method->ml_meth, rewrite_args->arg1,
                                                       rewrite_args->arg2, rewrite_args->arg3,
                                                       rewrite_args->args)->setType(RefType::OWNED);
                else
                    abort();
            }
        } else {
            RELEASE_ASSERT(0, "0x%x", call_flags);
        }

        if (!rtn)
            throwCAPIException();

        if (rewrite_args) {
            rewrite_args->rewriter->checkAndThrowCAPIException(rewrite_args->out_rtn);
            rewrite_args->out_success = true;
        }

        return rtn;
    };

    return rearrangeArgumentsAndCall(paramspec, NULL, self->method->ml_name, defaults, rewrite_args, argspec, arg1,
                                     arg2, arg3, args, keyword_names, continuation);
}

static Box* methodGetName(Box* b, void*) {
    assert(b->cls == method_cls);
    const char* s = static_cast<BoxedMethodDescriptor*>(b)->method->ml_name;
    if (s)
        return boxString(s);
    return incref(None);
}
static Box* methodGetDoc(Box* b, void*) {
    assert(b->cls == method_cls);
    const char* s = static_cast<BoxedMethodDescriptor*>(b)->method->ml_doc;
    if (s)
        return boxString(s);
    return incref(None);
}

static Box* methodRepr(Box* _o) {
    assert(_o->cls == method_cls);
    BoxedMethodDescriptor* md = static_cast<BoxedMethodDescriptor*>(_o);
    const char* name = md->method->ml_name;
    if (!name)
        name = "?";
    return PyString_FromFormat("<method '%s' of '%s' objects>", name, getNameOfClass(md->type));
}

Box* BoxedMethodDescriptor::descr_get(BoxedMethodDescriptor* self, Box* inst, Box* owner) noexcept {
    RELEASE_ASSERT(self->cls == method_cls, "");

    // CPython handles this differently: they create the equivalent of different BoxedMethodDescriptor
    // objects but with different class objects, which define different __get__ and __call__ methods.
    if (self->method->ml_flags & METH_CLASS)
        return boxInstanceMethod(owner, self, self->type);

    if (self->method->ml_flags & METH_STATIC)
        Py_FatalError("unimplemented");

    if (inst == NULL)
        return incref(self);
    else
        return boxInstanceMethod(inst, self, self->type);
}

void BoxedMethodDescriptor::dealloc(Box* _self) noexcept {
    BoxedMethodDescriptor* self = static_cast<BoxedMethodDescriptor*>(_self);

    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->type);
    self->cls->tp_free(self);
}

int BoxedMethodDescriptor::traverse(Box* _self, visitproc visit, void* arg) noexcept {
    BoxedMethodDescriptor* self = static_cast<BoxedMethodDescriptor*>(_self);

    Py_VISIT(self->type);
    return 0;
}

void BoxedProperty::dealloc(Box* _self) noexcept {
    BoxedProperty* self = static_cast<BoxedProperty*>(_self);

    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->prop_get);
    Py_XDECREF(self->prop_set);
    Py_XDECREF(self->prop_del);
    Py_XDECREF(self->prop_doc);
    self->cls->tp_free(self);
}

int BoxedProperty::traverse(Box* _self, visitproc visit, void* arg) noexcept {
    BoxedProperty* self = static_cast<BoxedProperty*>(_self);

    Py_VISIT(self->prop_get);
    Py_VISIT(self->prop_set);
    Py_VISIT(self->prop_del);
    Py_VISIT(self->prop_doc);
    return 0;
}

void BoxedStaticmethod::dealloc(Box* _self) noexcept {
    BoxedStaticmethod* self = static_cast<BoxedStaticmethod*>(_self);

    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->sm_callable);
    self->cls->tp_free(self);
}

int BoxedStaticmethod::traverse(Box* _self, visitproc visit, void* arg) noexcept {
    BoxedStaticmethod* self = static_cast<BoxedStaticmethod*>(_self);

    Py_VISIT(self->sm_callable);
    return 0;
}

int BoxedStaticmethod::clear(Box* _self) noexcept {
    BoxedStaticmethod* self = static_cast<BoxedStaticmethod*>(_self);

    Py_CLEAR(self->sm_callable);
    return 0;
}

void BoxedClassmethod::dealloc(Box* _self) noexcept {
    BoxedClassmethod* self = static_cast<BoxedClassmethod*>(_self);

    PyObject_GC_UnTrack(self);
    Py_XDECREF(self->cm_callable);
    self->cls->tp_free(self);
}

int BoxedClassmethod::traverse(Box* _self, visitproc visit, void* arg) noexcept {
    BoxedClassmethod* self = static_cast<BoxedClassmethod*>(_self);

    Py_VISIT(self->cm_callable);
    return 0;
}

int BoxedClassmethod::clear(Box* _self) noexcept {
    BoxedClassmethod* self = static_cast<BoxedClassmethod*>(_self);

    Py_CLEAR(self->cm_callable);
    return 0;
}

template <ExceptionStyle S>
Box* wrapperDescrTppCall(Box* _self, CallRewriteArgs* rewrite_args, ArgPassSpec argspec, Box* arg1, Box* arg2,
                         Box* arg3, Box** args, const std::vector<BoxedString*>* keyword_names) noexcept(S == CAPI) {
    if (S == CAPI) {
        try {
            return wrapperDescrTppCall<CXX>(_self, NULL, argspec, arg1, arg2, arg3, args, keyword_names);
        } catch (ExcInfo e) {
            setCAPIException(e);
            return NULL;
        }
    }

    STAT_TIMER(t0, "us_timer_boxedwrapperdecsriptor_call", (_self->cls->is_user_defined ? 10 : 20));

    assert(_self->cls == &PyWrapperDescr_Type);
    PyWrapperDescrObject* self = reinterpret_cast<PyWrapperDescrObject*>(_self);

    int flags = self->d_base->flags;
    wrapperfunc wrapper = self->d_base->wrapper;

    ParamReceiveSpec paramspec(1, 0, true, false);
    if (flags == PyWrapperFlag_KEYWORDS) {
        paramspec = ParamReceiveSpec(1, 0, true, true);
    } else if (flags == PyWrapperFlag_PYSTON || flags == 0) {
        paramspec = ParamReceiveSpec(1, 0, true, false);
    } else if (flags == PyWrapperFlag_1ARG) {
        paramspec = ParamReceiveSpec(1, 0, false, false);
    } else if (flags == PyWrapperFlag_2ARG) {
        paramspec = ParamReceiveSpec(2, 0, false, false);
    } else {
        RELEASE_ASSERT(0, "%d", flags);
    }

    auto continuation = [=](CallRewriteArgs* rewrite_args, Box* arg1, Box* arg2, Box* arg3, Box** args) {
#ifndef NDEBUG
        if (paramspec.takes_varargs)
            assert(arg2 && arg2->cls == tuple_cls);
#endif

        Box* rtn;
        if (flags == PyWrapperFlag_KEYWORDS) {
            wrapperfunc_kwds wk = (wrapperfunc_kwds)wrapper;
            rtn = (*wk)(arg1, arg2, self->d_wrapped, arg3);

            if (rewrite_args) {
                auto rewriter = rewrite_args->rewriter;
                rewrite_args->out_rtn
                    = rewriter->call(true, (void*)wk, rewrite_args->arg1, rewrite_args->arg2,
                                     rewriter->loadConst((intptr_t)self->d_wrapped, Location::forArg(2)),
                                     rewrite_args->arg3)->setType(RefType::OWNED);
                rewrite_args->rewriter->checkAndThrowCAPIException(rewrite_args->out_rtn);
                rewrite_args->out_success = true;
            }
        } else if (flags == PyWrapperFlag_PYSTON || flags == 0) {
            rtn = (*wrapper)(arg1, arg2, self->d_wrapped);

            if (rewrite_args) {
                auto rewriter = rewrite_args->rewriter;
                rewrite_args->out_rtn
                    = rewriter->call(true, (void*)wrapper, rewrite_args->arg1, rewrite_args->arg2,
                                     rewriter->loadConst((intptr_t)self->d_wrapped, Location::forArg(2)))
                          ->setType(RefType::OWNED);
                rewrite_args->rewriter->checkAndThrowCAPIException(rewrite_args->out_rtn);
                rewrite_args->out_success = true;
            }
        } else if (flags == PyWrapperFlag_1ARG) {
            wrapperfunc_1arg wrapper_1arg = (wrapperfunc_1arg)wrapper;
            rtn = (*wrapper_1arg)(arg1, self->d_wrapped);

            if (rewrite_args) {
                auto rewriter = rewrite_args->rewriter;
                rewrite_args->out_rtn
                    = rewriter->call(true, (void*)wrapper, rewrite_args->arg1,
                                     rewriter->loadConst((intptr_t)self->d_wrapped, Location::forArg(1)))
                          ->setType(RefType::OWNED);
                rewrite_args->rewriter->checkAndThrowCAPIException(rewrite_args->out_rtn);
                rewrite_args->out_success = true;
            }
        } else if (flags == PyWrapperFlag_2ARG) {
            rtn = (*wrapper)(arg1, arg2, self->d_wrapped);

            if (rewrite_args) {
                auto rewriter = rewrite_args->rewriter;
                rewrite_args->out_rtn
                    = rewriter->call(true, (void*)wrapper, rewrite_args->arg1, rewrite_args->arg2,
                                     rewriter->loadConst((intptr_t)self->d_wrapped, Location::forArg(2)))
                          ->setType(RefType::OWNED);
                rewrite_args->rewriter->checkAndThrowCAPIException(rewrite_args->out_rtn);
                rewrite_args->out_success = true;
            }
        } else {
            RELEASE_ASSERT(0, "%d", flags);
        }

        if (S == CXX && !rtn)
            throwCAPIException();
        return rtn;
    };

    return callCXXFromStyle<S>([&]() {
        return rearrangeArgumentsAndCall(paramspec, NULL, self->d_base->name, NULL, rewrite_args, argspec, arg1, arg2,
                                         arg3, args, keyword_names, continuation);
    });
}

template <ExceptionStyle S>
Box* wrapperObjectTppCall(Box* _self, CallRewriteArgs* rewrite_args, ArgPassSpec argspec, Box* arg1, Box* arg2,
                          Box* arg3, Box** args, const std::vector<BoxedString*>* keyword_names) noexcept(S == CAPI) {
    STAT_TIMER(t0, "us_timer_boxedwrapperobject_call", (_self->cls->is_user_defined ? 10 : 20));

    assert(_self->cls == &wrappertype);
    wrapperobject* self = reinterpret_cast<wrapperobject*>(_self);

    RewriterVar* r_obj = NULL;
    Box** new_args = NULL;
    if (argspec.totalPassed() >= 3)
        new_args = (Box**)alloca(sizeof(Box*) * (argspec.totalPassed() + 1 - 3));

    if (rewrite_args) {
        r_obj = rewrite_args->obj->getAttr(offsetof(wrapperobject, self), Location::forArg(0));
    }
    ArgPassSpec new_argspec
        = bindObjIntoArgs(self->self, r_obj, rewrite_args, argspec, arg1, arg2, arg3, args, new_args);
    return wrapperDescrTppCall<S>((Box*)self->descr, rewrite_args, new_argspec, arg1, arg2, arg3, new_args,
                                  keyword_names);
}

extern "C" PyObject* PyStaticMethod_New(PyObject* callable) noexcept {
    return new BoxedStaticmethod(callable);
}

extern "C" PyObject* PyDescr_NewClassMethod(PyTypeObject* type, PyMethodDef* method) noexcept {
    // Pyston change: we don't have a separate capi classmethod descriptor type, we just use the normal
    // one but with the METH_CLASS flag set.
    if (!(method->ml_flags & METH_CLASS)) {
        method = new PyMethodDef(*method);
        method->ml_flags |= METH_CLASS;
    }
    return new pyston::BoxedMethodDescriptor(method, type);
}

extern "C" PyObject* PyDescr_NewMethod(PyTypeObject* type, PyMethodDef* method) noexcept {
    return new BoxedMethodDescriptor(method, type);
}

void setupDescr() {
    property_cls->instances_are_nonzero = true;

    property_cls->giveAttr("__init__", new BoxedFunction(FunctionMetadata::create(
                                                             (void*)propertyInit, UNKNOWN, 5, false, false,
                                                             ParamNames({ "", "fget", "fset", "fdel", "doc" }, "", "")),
                                                         { None, None, None, NULL }));
    property_cls->giveAttr("__get__", new BoxedFunction(FunctionMetadata::create((void*)propertyGet, UNKNOWN, 3)));
    property_cls->giveAttr("__set__", new BoxedFunction(FunctionMetadata::create((void*)propertySet, UNKNOWN, 3)));
    property_cls->giveAttr("__delete__", new BoxedFunction(FunctionMetadata::create((void*)propertyDel, UNKNOWN, 2)));
    property_cls->giveAttr("getter", new BoxedFunction(FunctionMetadata::create((void*)propertyGetter, UNKNOWN, 2)));
    property_cls->giveAttr("setter", new BoxedFunction(FunctionMetadata::create((void*)propertySetter, UNKNOWN, 2)));
    property_cls->giveAttr("deleter", new BoxedFunction(FunctionMetadata::create((void*)propertyDeleter, UNKNOWN, 2)));
    property_cls->giveAttrMember("fget", T_OBJECT, offsetof(BoxedProperty, prop_get));
    property_cls->giveAttrMember("fset", T_OBJECT, offsetof(BoxedProperty, prop_set));
    property_cls->giveAttrMember("fdel", T_OBJECT, offsetof(BoxedProperty, prop_del));
    property_cls->giveAttrMember("__doc__", T_OBJECT, offsetof(BoxedProperty, prop_doc));
    property_cls->freeze();

    staticmethod_cls->giveAttr(
        "__init__", new BoxedFunction(FunctionMetadata::create((void*)staticmethodInit, UNKNOWN, 5, false, false),
                                      { None, None, None, None }));
    staticmethod_cls->giveAttr(
        "__get__",
        new BoxedFunction(FunctionMetadata::create((void*)staticmethodGet, UNKNOWN, 3, false, false), { None }));
    staticmethod_cls->freeze();


    classmethod_cls->giveAttr(
        "__init__", new BoxedFunction(FunctionMetadata::create((void*)classmethodInit, UNKNOWN, 5, false, false),
                                      { None, None, None, None }));
    classmethod_cls->giveAttr(
        "__get__",
        new BoxedFunction(FunctionMetadata::create((void*)classmethodGet, UNKNOWN, 3, false, false), { None }));
    classmethod_cls->freeze();

    method_cls->giveAttr("__get__", new BoxedFunction(FunctionMetadata::create((void*)BoxedMethodDescriptor::descr_get,
                                                                               UNKNOWN, 3, ParamNames::empty(), CAPI)));
    FunctionMetadata* method_call_cl
        = FunctionMetadata::create((void*)BoxedMethodDescriptor::__call__, UNKNOWN, 2, true, true);
    method_cls->giveAttr("__call__", new BoxedFunction(method_call_cl));
    method_cls->tpp_call.capi_val = BoxedMethodDescriptor::tppCall<CAPI>;
    method_cls->tpp_call.cxx_val = BoxedMethodDescriptor::tppCall<CXX>;
    method_cls->giveAttrDescriptor("__doc__", methodGetDoc, NULL);
    method_cls->giveAttrDescriptor("__name__", methodGetName, NULL);
    method_cls->giveAttr("__repr__", new BoxedFunction(FunctionMetadata::create((void*)methodRepr, UNKNOWN, 1)));
    method_cls->freeze();

    PyType_Ready(&PyGetSetDescr_Type);
    PyType_Ready(&PyMemberDescr_Type);

    PyType_Ready(&wrappertype);
    PyType_Ready(&PyWrapperDescr_Type);

#if 0
    wrappertype.tpp_call.capi_val = wrapperObjectTppCall<CAPI>;
    wrappertype.tpp_call.cxx_val = wrapperObjectTppCall<CXX>;
    wrappertype.tp_call = proxyToTppCall;
    PyType_Ready(&wrappertype);

    PyWrapperDescr_Type.tpp_call.capi_val = wrapperDescrTppCall<CAPI>;
    PyWrapperDescr_Type.tpp_call.cxx_val = wrapperDescrTppCall<CXX>;
    PyWrapperDescr_Type.tp_call = proxyToTppCall;
    PyType_Ready(&PyWrapperDescr_Type);
#endif
}
}
