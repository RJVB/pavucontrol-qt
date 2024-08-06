#ifndef NEEDS_INVOKE_METHOD_FUNCTOR_H

#include <qglobal.h>

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0) && !defined(EXTENDED_QT_5_9_8)
#define NEEDS_INVOKE_METHOD_FUNCTOR
#endif

#include <typeinfo>
#include <cxxabi.h>
#include <QThread>
#ifdef NEEDS_INVOKE_METHOD_FUNCTOR
#include <QSemaphore>
#else
class QSemaphore;
#endif
#ifdef DEBUG
#include <QDebug>
#endif


// The FunctorEvent definition is provided regardless of whether it is required
// so that `moc` can see it. Its functionality is intentionally crippled when
// it is not required.

// This taken from https://stackoverflow.com/a/21653558/1460868
// With this alternative to QMetaObject::invokeMethod(obj,Func,type)
// the application runs under Qt as old as 5.6.3 (and possibly older).
#ifdef QUSEREVENT_FUNCTOREVENT_NUM
#   define QFUNCTOR_EVENT QEvent::Type(QEvent::User+QUSEREVENT_FUNCTOREVENT_NUM)
#else
#   define QFUNCTOR_EVENT QEvent::User
#endif
template <typename F>
struct FunctorEvent : public QEvent
{
public:
    using Fun = typename std::decay<F>::type;
    Fun fun;
    FunctorEvent(Fun && fun, QSemaphore *sem = nullptr)
        : QEvent(QFUNCTOR_EVENT)
        , fun(std::move(fun))
        , semaphore(sem)
    {}
    FunctorEvent(const Fun & fun, QSemaphore *sem = nullptr)
        : QEvent(QFUNCTOR_EVENT)
        , fun(fun)
        , semaphore(sem)
    {}
    ~FunctorEvent()
    {
        fun();
        if (semaphore) {
#ifdef NEEDS_INVOKE_METHOD_FUNCTOR
            semaphore->release();
#else
            qWarning() << Q_FUNC_INFO << name() << "called but support for using the semaphore wasn't compiled in!";
#endif
        }
#ifdef DEBUG
        qDebug() << Q_FUNC_INFO << this << name() << "done on the main thread";
#endif
    }
    QString name() const
    {
        const auto mangled = typeid(fun).name();
        const auto demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, nullptr);
        if (demangled) {
            const QString ret = QString::fromUtf8(demangled);
            free(demangled);
            return ret;
        }
        return QString::fromUtf8(mangled);
    }
private:
    QSemaphore *semaphore;
};

#define NEEDS_INVOKE_METHOD_FUNCTOR_H
#endif //NEEDS_INVOKE_METHOD_FUNCTOR_H
