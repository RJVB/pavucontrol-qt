/***
  This file is part of pavucontrol.

  Copyright 2006-2008 Lennart Poettering
  Copyright 2009 Colin Guthrie

  pavucontrol is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  pavucontrol is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with pavucontrol. If not, see <https://www.gnu.org/licenses/>.
***/

#ifndef pavucontrol_h
#define pavucontrol_h

#include <signal.h>
#include <string.h>
#include <glib.h>

#include <pulse/pulseaudio.h>

#include <QApplication>
#include <QDebug>

#ifdef NEEDS_PCVAPP_FUNCTIONS
// only include this header and its companion in compilation units
// where their functionality is invoked (i.e. PVCApplication::invokeMethod).
#include "needs_invoke_method_functor.h"
#else
class QThread;
#endif

#ifndef NEEDS_INVOKE_METHOD_FUNCTOR
class QSemaphore;
#endif

/* Can be removed when PulseAudio 0.9.23 or newer is required */
#ifndef PA_VOLUME_UI_MAX
# define PA_VOLUME_UI_MAX (pa_sw_volume_from_dB(+11.0))
#endif

#define HAVE_SOURCE_OUTPUT_VOLUMES PA_CHECK_VERSION(0,99,0)
#define HAVE_EXT_DEVICE_RESTORE_API PA_CHECK_VERSION(0,99,0)

enum SinkInputType {
    SINK_INPUT_ALL,
    SINK_INPUT_CLIENT,
    SINK_INPUT_VIRTUAL
};

enum SinkType {
    SINK_ALL,
    SINK_HARDWARE,
    SINK_VIRTUAL,
};

enum SourceOutputType {
    SOURCE_OUTPUT_ALL,
    SOURCE_OUTPUT_CLIENT,
    SOURCE_OUTPUT_VIRTUAL
};

enum SourceType {
    SOURCE_ALL,
    SOURCE_NO_MONITOR,
    SOURCE_HARDWARE,
    SOURCE_VIRTUAL,
    SOURCE_MONITOR,
};

pa_context* get_context(void);
void show_error(const char *txt);
void show_translated_error(const char *txt);

// Use blocking signal/slot connections to make them syncronous
#define PVCConnection   Qt::BlockingQueuedConnection
#define pvcApp          PVCApplication::instance()

class MainWindow;

class PVCApplication : public QApplication
{
    Q_OBJECT
public:

    PVCApplication(int &argc, char **argv);

    void setMainWindow(MainWindow *window)
    {
        w = window;
    }

    MainWindow *mainWindow()
    {
        return w;
    }

    static PVCApplication *instance()
    {
        return self;
    }

    static bool isQuitting()
    {
        return quitting;
    }

#ifdef NEEDS_PCVAPP_FUNCTIONS
    #include "invoke_method_functor.h"
#endif

#ifdef DEBUG
    QThread *currentThread() const;
#endif

    void setPAMainLoop(struct pa_threaded_mainloop *loop)
    {
        pa_mainloop = loop;
    }
    struct pa_threaded_mainloop *paMainLoop() const
    {
        return pa_mainloop;
    }

public slots:

    // pure GUI functions:

    void show_error(const char *txt)
    {
        ::show_error(txt);
    }
    void show_translated_error(const char *txt)
    {
        ::show_translated_error(txt);
    }

    void dec_outstanding();
    void createEventRoleWidget();
    void setConnectionState(gboolean state);
    void removeAllWidgets();
    void updateDeviceVisibility();
    void reset();
    void reconnect();
    void willQuit();

    // implementations of libpulse callback functions:

    void card_cb(const pa_card_info *i, int eol);
    void sink_cb(pa_context *c, const pa_sink_info *i, int eol);
    void source_cb(const pa_source_info *i, int eol);
    void sink_input_cb(const pa_sink_input_info *i, int eol);
    void source_output_cb(const pa_source_output_info *i, int eol);
    void client_cb(const pa_client_info *i, int eol);
    void server_info_cb(const pa_server_info *i);
    void ext_stream_restore_read_cb(
            const void *info,
            int eol);
//     void ext_stream_restore_subscribe_cb(pa_context *c);
    // the next slot only does something when HAVE_EXT_DEVICE_RESTORE_API
    void ext_device_restore_read_cb(
            const void *info,
            int eol);
//     void ext_device_restore_subscribe_cb(pa_context *c, pa_device_type_t type, uint32_t idx);
    // end HAVE_EXT_DEVICE_RESTORE_API

    void ext_device_manager_read_cb(int eol);
//     void ext_device_manager_subscribe_cb(pa_context *c);
    void removeSink(uint32_t index);
    void removeSource(uint32_t index);
    void removeSinkInput(uint32_t index);
    void removeSourceOutput(uint32_t index);
    void removeClient(uint32_t index);
    void removeCard(uint32_t index);
public:
    // keep our own cached thread pointer to save on some function calls
    const QThread *mainThread = nullptr;
private:
    bool hasGlib;

    MainWindow *w;
    static PVCApplication *self;
    static std::atomic<bool> quitting;

    // only set when using a pa_threaded_mainloop, i.e. when built with USE_THREADED_PALOOP
    static struct pa_threaded_mainloop *pa_mainloop;
};

#ifdef NEEDS_PCVAPP_FUNCTIONS

#ifdef NEEDS_INVOKE_METHOD_FUNCTOR
#define INVOKE_METHOD(app,fnc) app->invokeMethod(fnc, true)
#define INVOKE_METHOD_ASYNC(app,fnc) app->invokeMethod(fnc, false)
#else
#define INVOKE_METHOD(app,fnc) QMetaObject::invokeMethod(app, fnc, PVCConnection)
#define INVOKE_METHOD_ASYNC(app,fnc) QMetaObject::invokeMethod(app, fnc, Qt::QueuedConnection)
#endif

#ifdef USE_THREADED_PALOOP
#define PVCAPP_FUNCTION(ptr,fnc) { \
    PVCApplication *app = static_cast<PVCApplication*>(ptr); \
    INVOKE_METHOD(app, [=]() { app-> fnc ; }); \
}
#define PVCAPP_FUNCTION_CHECK(ptr,fnc) { \
    PVCApplication *app = ptr ? static_cast<PVCApplication*>(ptr) : pvcApp; \
    if (QThread::currentThread() != app->mainThread) { \
        INVOKE_METHOD(app, [=]() { app-> fnc ; }); \
    } else { \
        app-> fnc ; \
    } \
}
#else
// A PVCAPP_FUNCTION implementation that can take a PVCApplication* or a MainWindow* or a nullptr:
/*
#define PVCAPP_FUNCTION(ptr,fnc) { \
    PVCApplication *app = dynamic_cast<PVCApplication*>(static_cast<QObject*>(ptr)); \
    if (app && QThread::currentThread() != app->mainThread) { \
        qWarning() << Q_FUNC_INFO << "thread" << QThread::currentThread() << "!=" << app->mainThread << "; invokeMethod" << # fnc; \
        INVOKE_METHOD(app, [=]() { app-> fnc ; }); \
    } else { \
        pvcApp-> fnc ; \
    } \
}
 */
// The simple implementation that ignores the userdata ptr:
#define PVCAPP_FUNCTION(ptr,fnc) { \
    Q_UNUSED(ptr); \
    pvcApp-> fnc ; \
}
#define PVCAPP_FUNCTION_CHECK(ptr,fnc) PVCAPP_FUNCTION(ptr,fnc)
#endif // USE_THREADED_PALOOP
#endif // NEEDS_PCVAPP_FUNCTIONS


#endif
