#pragma once

#ifndef NEEDS_INVOKE_METHOD_FUNCTOR_H
#   error "needs_invoke_method_functor.h has not been included"
    // include the file, but only for the benefit of IDE parsers!
#   include "needs_invoke_method_functor.h"
#endif

// This file is meant to be included from inside a class definition;
// it provides the invokeMethod(<functor>) implementation. The method is
// always defined, but may invoke QMetaObject::invokeMethod() if Qt itself
// provides the required functionalities.

    template <typename F>
    void invokeMethod(F && fun)
    {
#ifdef DEBUG
        auto name = typeid(fun).name();
        const auto demangled = abi::__cxa_demangle(name, nullptr, nullptr, nullptr);
        if (demangled) {
            name = demangled;
        }
        qDebug() << "\n" << Q_FUNC_INFO;
#endif
        if (QThread::currentThread() == mainThread) {
            // we could warn about a deadlock like QMetaObject::invokeMethod() does (and hang),
            // or we could save on doing the check and possibly hang when used wrongly.
            // Or we can notify and execute the requested function.
#ifdef DEBUG
            qWarning() << name << "being called on the main thread";
#else
            qWarning() << Q_FUNC_INFO << "called on the main thread";
#endif
            fun();
        } else {
#ifdef NEEDS_INVOKE_METHOD_FUNCTOR
            QSemaphore sem;
#   ifdef DEBUG
            qDebug() << "invoking" << name << "on the main thread";
#   endif
            postEvent(this, new FunctorEvent<F>(std::forward<F>(fun), &sem));
            sem.acquire();
#   ifdef DEBUG
            qDebug() << name << "done";
#   endif
#else // NEEDS_INVOKE_METHOD_FUNCTOR
#   ifdef DEBUG
            qDebug() << "invoking" << name << "on the main thread";
#   endif
            QMetaObject::invokeMethod(this, fun, Qt::BlockingQueuedConnection);
#   ifdef DEBUG
            qDebug() << name << "done";
#   endif
#endif // NEEDS_INVOKE_METHOD_FUNCTOR
        }
#ifdef DEBUG
        if (demangled) {
            free(demangled);
        }
#endif
    }
