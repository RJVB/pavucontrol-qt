/***
  This file is part of pavucontrol.

  Copyright 2006-2008 Lennart Poettering
  Copyright 2008 Sjoerd Simons <sjoerd@luon.net>

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define NEEDS_PCVAPP_FUNCTIONS

#define PACKAGE_VERSION "0.1"

#include <pulse/pulseaudio.h>
#ifdef USE_THREADED_GLLOOP
#include <pulse/thread-mainloop.h>
#else
#include <pulse/glib-mainloop.h>
#endif
#include <pulse/ext-stream-restore.h>
#include <pulse/ext-device-manager.h>

// #include <canberra-gtk.h>

#include "pavucontrol.h"
#include "minimalstreamwidget.h"
#include "channel.h"
#include "streamwidget.h"
#include "cardwidget.h"
#include "sinkwidget.h"
#include "sourcewidget.h"
#include "sinkinputwidget.h"
#include "sourceoutputwidget.h"
#include "rolewidget.h"
#include "mainwindow.h"
#include <QMessageBox>
#include <QApplication>
#include <QLocale>
#include <QLibraryInfo>
#include <QTranslator>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QString>
#include <QAbstractEventDispatcher>
#ifndef NEEDS_INVOKE_METHOD_FUNCTOR
#include <QThread>
#endif
#include <QDebug>

#include <atomic>

static pa_context* context = nullptr;
#ifdef USE_THREADED_GLLOOP
static pa_threaded_mainloop *mainloop = nullptr;
#endif
static pa_mainloop_api* api = nullptr;
static std::atomic<int> n_outstanding;
static int default_tab = 0;
static bool retry = false;
static std::atomic<int> reconnect_timeout;

PVCApplication* PVCApplication::self = nullptr;
std::atomic<bool> PVCApplication::quitting;

void show_translated_error(const char *txt)
{
#ifdef USE_THREADED_GLLOOP
    if (QThread::currentThread() != pvcApp->mainThread) {
        // we need to execute on the main thread
        INVOKE_METHOD(pvcApp, [=]() { pvcApp->show_translated_error(txt); });
        return;
    }
#endif
    const auto pa_str = QString::fromUtf8(pa_strerror(pa_context_errno(context)));
    const auto message = QString(QStringLiteral("%1: %2"))
        .arg(QObject::tr(txt))
        .arg(pa_str);

    QMessageBox::critical(nullptr, QObject::tr("Error"), message);
    qApp->quit();
}

void show_error(const char *txt) {
#ifdef USE_THREADED_GLLOOP
    if (QThread::currentThread() != pvcApp->mainThread) {
        // we need to execute on the main thread
        INVOKE_METHOD(pvcApp, [=]() { pvcApp->show_error(txt); });
        return;
    }
#endif
    const auto pa_str = QString::fromUtf8(pa_strerror(pa_context_errno(context)));
    const auto message = QString(QStringLiteral("%1: %2"))
        .arg(QString::fromUtf8(txt))
        .arg(pa_str);

    QMessageBox::critical(nullptr, QObject::tr("Error"), message);
    qApp->quit();
}

void ext_stream_restore_read_cb(
        pa_context *,
        const pa_ext_stream_restore_info *i,
        int eol,
        void *userdata);

#if HAVE_EXT_DEVICE_RESTORE_API
void ext_device_restore_read_cb(
        pa_context *,
        const pa_ext_device_restore_info *i,
        int eol,
        void *userdata);
#endif
void ext_device_manager_read_cb(
        pa_context *,
        const pa_ext_device_manager_info *,
        int eol,
        void *userdata);

    PVCApplication::PVCApplication(int &argc, char **argv)
        : QApplication(argc, argv)
        , mainThread(QThread::currentThread())
    {
	   // I'd prefer to initialise PVCApplication::quitting elsewhere
	   // but that would require C++17 apparently (instead of C++11).
	   quitting = false;
        gContext = g_main_context_default();
        const auto dispatcher = eventDispatcher();
        hasGlib = dispatcher && dispatcher->inherits("QEventDispatcherGlib");
        qDebug() << Q_FUNC_INFO << "eventDispatcher:" << dispatcher << "hasGlib:" << hasGlib;
        self = this;
        connect(this, &QCoreApplication::aboutToQuit, this, &PVCApplication::willQuit);
    }

#ifdef DEBUG
    QThread *PVCApplication::currentThread() const
    {
        return QThread::currentThread();
    }
#endif

    // pure GUI functions:

    void PVCApplication::dec_outstanding()
    {
        if (n_outstanding <= 0)
            return;

        if (--n_outstanding <= 0) {
            // w->get_window()->set_cursor();
            w->setConnectionState(true);
        }
    }

    void PVCApplication::createEventRoleWidget()
    {
        w->createEventRoleWidget();
    }

    void PVCApplication::setConnectionState(gboolean state)
    {
        w->setConnectionState(state);
    }

    void PVCApplication::removeAllWidgets()
    {
        w->removeAllWidgets();
    }

    void PVCApplication::updateDeviceVisibility()
    {
        w->updateDeviceVisibility();
    }

    void PVCApplication::reset()
    {
        w->setConnectionState(false);
        w->removeAllWidgets();
        w->updateDeviceVisibility();
    }

    void PVCApplication::willQuit()
    {
        qDebug() << Q_FUNC_INFO << "!";
        quitting = 1;
    }

    // implementations of libpulse callback functions:

    void PVCApplication::card_cb(const pa_card_info *i, int eol)
    {
        if (eol < 0) {
            if (pa_context_errno(context) == PA_ERR_NOENTITY)
                return;

            show_translated_error("Card callback failure");
            return;
        }

        if (eol > 0) {
            dec_outstanding();
            return;
        }

        w->updateCard(*i);
    }

    void PVCApplication::sink_cb(pa_context *c, const pa_sink_info *i, int eol)
    {
        if (eol < 0) {
            if (pa_context_errno(context) == PA_ERR_NOENTITY)
                return;

            show_translated_error("Sink callback failure");
            return;
        }

        if (eol > 0) {
            dec_outstanding();
            return;
        }
#if HAVE_EXT_DEVICE_RESTORE_API
        if (w->updateSink(*i))
            ext_device_restore_subscribe_cb(c, PA_DEVICE_TYPE_SINK, i->index);
#else
        w->updateSink(*i);
#endif
    }

    void PVCApplication::source_cb(const pa_source_info *i, int eol)
    {
        if (eol < 0) {
            if (pa_context_errno(context) == PA_ERR_NOENTITY)
                return;

            show_translated_error("Source callback failure");
            return;
        }

        if (eol > 0) {
            dec_outstanding();
            return;
        }

        w->updateSource(*i);
    }

    void PVCApplication::sink_input_cb(const pa_sink_input_info *i, int eol)
    {
        if (eol < 0) {
            if (pa_context_errno(context) == PA_ERR_NOENTITY)
                return;

            show_translated_error("Sink input callback failure");
            return;
        }

        if (eol > 0) {
            dec_outstanding();
            return;
        }

        w->updateSinkInput(*i);
    }

    void PVCApplication::source_output_cb(const pa_source_output_info *i, int eol)
    {
        if (eol < 0) {
            if (pa_context_errno(context) == PA_ERR_NOENTITY)
                return;

            show_translated_error("Source output callback failure");
            return;
        }

        if (eol > 0)  {

            if (n_outstanding > 0) {
                /* At this point all notebook pages have been populated, so
                 * let's open one that isn't empty */
                if (default_tab != -1) {
                    if (default_tab < 1 || default_tab > w->notebook->count()) {
                        if (!w->sinkInputWidgets.empty())
                            w->notebook->setCurrentIndex(0);
                        else if (!w->sourceOutputWidgets.empty())
                            w->notebook->setCurrentIndex(1);
                        else if (!w->sourceWidgets.empty() && w->sinkWidgets.empty())
                            w->notebook->setCurrentIndex(3);
                        else
                            w->notebook->setCurrentIndex(2);
                    } else {
                        w->notebook->setCurrentIndex(default_tab - 1);
                    }
                    default_tab = -1;
                }
            }

            dec_outstanding();
            return;
        }

        w->updateSourceOutput(*i);
    }

    void PVCApplication::client_cb(const pa_client_info *i, int eol)
    {
        if (eol < 0) {
            if (pa_context_errno(context) == PA_ERR_NOENTITY)
                return;

            show_translated_error("Client callback failure");
            return;
        }

        if (eol > 0) {
            dec_outstanding();
            return;
        }

        w->updateClient(*i);
    }

    void PVCApplication::server_info_cb(const pa_server_info *i)
    {
        if (!i) {
            show_translated_error("Server info callback failure");
            return;
        }

        w->updateServer(*i);
        dec_outstanding();
    }

    void PVCApplication::ext_stream_restore_read_cb(
            const void *info,
            int eol)
    {
        if (eol < 0) {
            dec_outstanding();
            qDebug(tr("Failed to initialize stream_restore extension: %s").toUtf8().constData(), pa_strerror(pa_context_errno(context)));
            w->deleteEventRoleWidget();
            return;
        }

        if (eol > 0) {
            dec_outstanding();
            return;
        }

        const pa_ext_stream_restore_info *i = static_cast<const pa_ext_stream_restore_info*>(info);
        w->updateRole(*i);
    }

    void PVCApplication::ext_stream_restore_subscribe_cb(pa_context *c)
    {
        pa_operation *o;

        // evoke the global ext_stream_restore_read_cb() handler with userdata=nullptr to signal
        // that we're running on the same thread;
        if (!(o = pa_ext_stream_restore_read(c, ::ext_stream_restore_read_cb, nullptr))) {
            show_translated_error("pa_ext_stream_restore_read() failed");
            return;
        }

        pa_operation_unref(o);
    }

    void PVCApplication::ext_device_restore_read_cb(
            const void *info,
            int eol)
    {
#if HAVE_EXT_DEVICE_RESTORE_API
        if (eol < 0) {
            dec_outstanding();
            qDebug(tr("Failed to initialize device restore extension: %s").toUtf8().constData(), pa_strerror(pa_context_errno(context)));
            return;
        }

        if (eol > 0) {
            dec_outstanding();
            return;
        }

        /* Do something with a widget when this part is written */
        const pa_ext_device_restore_info *i = static_cast<const pa_ext_device_restore_info*>(info);
        w->updateDeviceInfo(*i);
#endif
    }

    void PVCApplication::ext_device_restore_subscribe_cb(pa_context *c, pa_device_type_t type, uint32_t idx)
    {
#if HAVE_EXT_DEVICE_RESTORE_API
        pa_operation *o;

        if (type != PA_DEVICE_TYPE_SINK)
            return;

        // evoke the global ext_device_restore_read_cb() handler with userdata=nullptr to signal
        // that we're running on the same thread;
        if (!(o = pa_ext_device_restore_read_formats(c, type, idx, ::ext_device_restore_read_cb, nullptr))) {
            show_translated_error("pa_ext_device_restore_read_sink_formats() failed");
            return;
        }

        pa_operation_unref(o);
#endif
    }

    void PVCApplication::ext_device_manager_read_cb(int eol)
    {
        if (eol < 0) {
            dec_outstanding();
            qDebug(tr("Failed to initialize device manager extension: %s").toUtf8().constData(), pa_strerror(pa_context_errno(context)));
            return;
        }

        w->canRenameDevices = true;

        if (eol > 0) {
            dec_outstanding();
            return;
        }

        /* Do something with a widget when this part is written */
    }

    void PVCApplication::ext_device_manager_subscribe_cb(pa_context *c)
    {
        pa_operation *o;

        if (!(o = pa_ext_device_manager_read(c, ::ext_device_manager_read_cb, nullptr))) {
            show_translated_error("pa_ext_device_manager_read() failed");
            return;
        }

        pa_operation_unref(o);
    }

    void PVCApplication::removeSink(uint32_t index)
    {
        w->removeSink(index);
    }

    void PVCApplication::removeSource(uint32_t index)
    {
        w->removeSource(index);
    }

    void PVCApplication::removeSinkInput(uint32_t index)
    {
        w->removeSinkInput(index);
    }

    void PVCApplication::removeSourceOutput(uint32_t index)
    {
        w->removeSourceOutput(index);
    }

    void PVCApplication::removeClient(uint32_t index)
    {
        w->removeClient(index);
    }

    void PVCApplication::removeCard(uint32_t index)
    {
        w->removeCard(index);
    }


void card_cb(pa_context *, const pa_card_info *i, int eol, void *userdata) {
    PVCAPP_FUNCTION(userdata, card_cb(i, eol));
// PVCAPP_FUNCTION expands to:
// #ifdef USE_THREADED_GLLOOP
//     PVCApplication *app = static_cast<PVCApplication*>(userdata);
//     QMetaObject::invokeMethod(app, [=]() { app->card_cb(i, eol); }, PVCConnection);
// #else
//     Q_UNUSED(userdata);
//     pvcApp->card_cb(i, eol);
// #endif
}

void sink_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    PVCAPP_FUNCTION(userdata, sink_cb(c, i, eol));
}

void source_cb(pa_context *, const pa_source_info *i, int eol, void *userdata) {
    PVCAPP_FUNCTION(userdata, source_cb(i, eol));
}

void sink_input_cb(pa_context *, const pa_sink_input_info *i, int eol, void *userdata) {
    PVCAPP_FUNCTION(userdata, sink_input_cb(i, eol));
}

void source_output_cb(pa_context *, const pa_source_output_info *i, int eol, void *userdata) {
    PVCAPP_FUNCTION(userdata, source_output_cb(i, eol));
}

void client_cb(pa_context *, const pa_client_info *i, int eol, void *userdata) {
    PVCAPP_FUNCTION(userdata, client_cb(i, eol));
}

void server_info_cb(pa_context *, const pa_server_info *i, void *userdata) {
    PVCAPP_FUNCTION(userdata, server_info_cb(i));
}

void ext_stream_restore_read_cb(
        pa_context *,
        const pa_ext_stream_restore_info *i,
        int eol,
        void *userdata) {
    // userdata can be NULL so check for that
    PVCAPP_FUNCTION_CHECK(userdata, ext_stream_restore_read_cb(i, eol));
}

static void ext_stream_restore_subscribe_cb(pa_context *c, void *userdata) {
    PVCAPP_FUNCTION(userdata, ext_stream_restore_subscribe_cb(c));
}

#if HAVE_EXT_DEVICE_RESTORE_API
void ext_device_restore_read_cb(
        pa_context *,
        const pa_ext_device_restore_info *i,
        int eol,
        void *userdata) {
    // userdata can be NULL so check for that
    PVCAPP_FUNCTION_CHECK(userdata, ext_device_restore_read_cb(i,eol));
}

static void ext_device_restore_subscribe_cb(pa_context *c, pa_device_type_t type, uint32_t idx, void *userdata) {
    PVCAPP_FUNCTION(userdata, ext_device_restore_subscribe_cb(c, type, idx));
}
#endif

void ext_device_manager_read_cb(
        pa_context *,
        const pa_ext_device_manager_info *,
        int eol,
        void *userdata) {
    // userdata can be NULL so check for that
    PVCAPP_FUNCTION_CHECK(userdata, ext_device_manager_read_cb(eol));
}

static void ext_device_manager_subscribe_cb(pa_context *c, void *userdata) {
    PVCAPP_FUNCTION(userdata, ext_device_manager_subscribe_cb(c));
}

// toplevel subscription/interface callback. It contains more calls into PA code that require
// callbacks into our own code than calls interacting with the GUI that need to be executed on
// the main thread. So we keep this function out of PVCApplication and only place those GUI
// calls via PVCAPP_FUNCTION().
void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata) {
    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
                PVCAPP_FUNCTION(userdata, removeSink(index));
            } else {
                pa_operation *o;
                if (!(o = pa_context_get_sink_info_by_index(c, index, sink_cb, userdata))) {
                    show_translated_error("pa_context_get_sink_info_by_index() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SOURCE:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
                PVCAPP_FUNCTION(userdata, removeSource(index));
            } else {
                pa_operation *o;
                if (!(o = pa_context_get_source_info_by_index(c, index, source_cb, userdata))) {
                    show_translated_error("pa_context_get_source_info_by_index() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
                PVCAPP_FUNCTION(userdata, removeSinkInput(index));
            } else {
                pa_operation *o;
                if (!(o = pa_context_get_sink_input_info(c, index, sink_input_cb, userdata))) {
                    show_translated_error("pa_context_get_sink_input_info() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
                PVCAPP_FUNCTION(userdata, removeSourceOutput(index));
            } else {
                pa_operation *o;
                if (!(o = pa_context_get_source_output_info(c, index, source_output_cb, userdata))) {
                    show_translated_error("pa_context_get_sink_input_info() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_CLIENT:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
                PVCAPP_FUNCTION(userdata, removeClient(index));
            } else {
                pa_operation *o;
                if (!(o = pa_context_get_client_info(c, index, client_cb, userdata))) {
                    show_translated_error("pa_context_get_client_info() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_SERVER: {
                pa_operation *o;
                if (!(o = pa_context_get_server_info(c, server_info_cb, userdata))) {
                    show_translated_error("pa_context_get_server_info() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;

        case PA_SUBSCRIPTION_EVENT_CARD:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
                PVCAPP_FUNCTION(userdata, removeCard(index));
            } else {
                pa_operation *o;
                if (!(o = pa_context_get_card_info_by_index(c, index, card_cb, userdata))) {
                    show_translated_error("pa_context_get_card_info_by_index() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;
        default:
            qWarning() << Q_FUNC_INFO << "Unhandled subscribed event type" << t
                << "(" << (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) << ")";
            break;
    }
}

/* Forward Declaration */
gboolean connect_to_pulse(gpointer userdata);

void context_state_callback(pa_context *c, void *userdata) {

    g_assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;

        case PA_CONTEXT_READY: {
            pa_operation *o;

            // all cross-thread calls are executed synchronously so a priori
            // we do not need to worry about increasing and decreasing global
            // variables. They're std::atomic to be sure.
            reconnect_timeout = 1;

            /* Create event widget immediately so it's first in the list */
            PVCAPP_FUNCTION(userdata, createEventRoleWidget());

            pa_context_set_subscribe_callback(c, subscribe_cb, userdata);

            if (!(o = pa_context_subscribe(c, (pa_subscription_mask_t)
                                           (PA_SUBSCRIPTION_MASK_SINK|
                                            PA_SUBSCRIPTION_MASK_SOURCE|
                                            PA_SUBSCRIPTION_MASK_SINK_INPUT|
                                            PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT|
                                            PA_SUBSCRIPTION_MASK_CLIENT|
                                            PA_SUBSCRIPTION_MASK_SERVER|
                                            PA_SUBSCRIPTION_MASK_CARD), nullptr, nullptr))) {
                show_translated_error("pa_context_subscribe() failed");
                return;
            }
            pa_operation_unref(o);

            /* Keep track of the outstanding callbacks for UI tweaks */
            n_outstanding = 0;

            if (!(o = pa_context_get_server_info(c, server_info_cb, userdata))) {
                show_translated_error("pa_context_get_server_info() failed");
                return;
            }
            pa_operation_unref(o);
            n_outstanding++;

            if (!(o = pa_context_get_client_info_list(c, client_cb, userdata))) {
                show_translated_error("pa_context_client_info_list() failed");
                return;
            }
            pa_operation_unref(o);
            n_outstanding++;

            if (!(o = pa_context_get_card_info_list(c, card_cb, userdata))) {
                show_translated_error("pa_context_get_card_info_list() failed");
                return;
            }
            pa_operation_unref(o);
            n_outstanding++;

            if (!(o = pa_context_get_sink_info_list(c, sink_cb, userdata))) {
                show_translated_error("pa_context_get_sink_info_list() failed");
                return;
            }
            pa_operation_unref(o);
            n_outstanding++;

            if (!(o = pa_context_get_source_info_list(c, source_cb, userdata))) {
                show_translated_error("pa_context_get_source_info_list() failed");
                return;
            }
            pa_operation_unref(o);
            n_outstanding++;

            if (!(o = pa_context_get_sink_input_info_list(c, sink_input_cb, userdata))) {
                show_translated_error("pa_context_get_sink_input_info_list() failed");
                return;
            }
            pa_operation_unref(o);
            n_outstanding++;

            if (!(o = pa_context_get_source_output_info_list(c, source_output_cb, userdata))) {
                show_translated_error("pa_context_get_source_output_info_list() failed");
                return;
            }
            pa_operation_unref(o);
            n_outstanding++;

            /* These calls are not always supported */
            if ((o = pa_ext_stream_restore_read(c, ext_stream_restore_read_cb, userdata))) {
                pa_operation_unref(o);
                n_outstanding++;

                pa_ext_stream_restore_set_subscribe_cb(c, ext_stream_restore_subscribe_cb, userdata);

                if ((o = pa_ext_stream_restore_subscribe(c, 1, nullptr, nullptr)))
                    pa_operation_unref(o);

            } else
                qDebug(QObject::tr("Failed to initialize stream_restore extension: %s").toUtf8().constData(), pa_strerror(pa_context_errno(context)));

#if HAVE_EXT_DEVICE_RESTORE_API
            /* TODO Change this to just the test function */
            if ((o = pa_ext_device_restore_read_formats_all(c, ext_device_restore_read_cb, userdata))) {
                pa_operation_unref(o);
                n_outstanding++;

                pa_ext_device_restore_set_subscribe_cb(c, ext_device_restore_subscribe_cb, userdata);

                if ((o = pa_ext_device_restore_subscribe(c, 1, nullptr, nullptr)))
                    pa_operation_unref(o);

            } else
                qDebug(QObject::tr("Failed to initialize device restore extension: %s").toUtf8().constData(), pa_strerror(pa_context_errno(context)));
#endif

            if ((o = pa_ext_device_manager_read(c, ext_device_manager_read_cb, userdata))) {
                pa_operation_unref(o);
                n_outstanding++;

                pa_ext_device_manager_set_subscribe_cb(c, ext_device_manager_subscribe_cb, userdata);

                if ((o = pa_ext_device_manager_subscribe(c, 1, nullptr, nullptr)))
                    pa_operation_unref(o);

            } else
                qDebug(QObject::tr("Failed to initialize device manager extension: %s").toUtf8().constData(), pa_strerror(pa_context_errno(context)));


            break;
        }

        case PA_CONTEXT_FAILED:
            PVCAPP_FUNCTION(userdata, reset());
            pa_context_unref(context);
            context = nullptr;

            if (reconnect_timeout > 0) {
                qWarning() << QObject::tr("Connection failed, attempting reconnect").toUtf8().constData();
                g_timeout_add_seconds(reconnect_timeout, connect_to_pulse, userdata);
            }
            return;

        case PA_CONTEXT_TERMINATED:
        default:
            if (!pvcApp->isQuitting()) {
                PVCAPP_FUNCTION_CHECK(userdata, quit());
            }
            return;
    }
}

pa_context* get_context() {
  return context;
}

gboolean connect_to_pulse(gpointer userdata) {

    if (context)
        return false;

    pa_proplist *proplist = pa_proplist_new();
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, QObject::tr("PulseAudio Volume Control").toUtf8().constData());
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "org.PulseAudio.pavucontrol");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "audio-card");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, PACKAGE_VERSION);

    context = pa_context_new_with_proplist(api, nullptr, proplist);
    g_assert(context);

    pa_proplist_free(proplist);

    pa_context_set_state_callback(context, context_state_callback, userdata);

    pvcApp->mainWindow()->setConnectingMessage();
#ifdef __APPLE__
    pa_context_flags_t connectFlags = PA_CONTEXT_NOAUTOSPAWN;
#else
    pa_context_flags_t connectFlags = pa_context_flags_t(PA_CONTEXT_NOFAIL|PA_CONTEXT_NOAUTOSPAWN);
#endif
    if (pa_context_connect(context, nullptr, connectFlags, nullptr) < 0) {
        if (pa_context_errno(context) == PA_ERR_INVALID) {
            pvcApp->mainWindow()->setConnectingMessage(QObject::tr("Connection to PulseAudio failed. Automatic retry in 5s.<br><br>"
                "In this case this is likely because PULSE_SERVER in the Environment/X11 Root Window Properties"
                "or default-server in client.conf is misconfigured.<br>"
                "This situation can also arrise when PulseAudio crashed and left stale details in the X11 Root Window.<br>"
                "If this is the case, then PulseAudio should autospawn again, or if this is not configured you should"
                "run start-pulseaudio-x11 manually.").toUtf8().constData());
            reconnect_timeout = 5;
        }
        else {
            if(!retry) {
                reconnect_timeout = -1;
                qApp->quit();
            } else {
                qDebug("%s", QObject::tr("Connection failed, attempting reconnect").toUtf8().constData());
                reconnect_timeout = 5;
                g_timeout_add_seconds(reconnect_timeout, connect_to_pulse, userdata);
            }
        }
    } else {
#ifndef USE_THREADED_GLLOOP
        // pump the GLib context until we've connected, before entering the Qt main loop.
        // Not necessary, but eliminates some GUI glitching on opening
        auto gContext = g_main_context_default();
        for (;;)
        {
            pa_context_state_t state = pa_context_get_state(context);
            if (state == PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(state))
                break;
            g_main_context_iteration(gContext, true);
        }
#endif
    }

    return false;
}

/**
 * @brief ...
 * 
 * @param argc p_argc:...
 * @param argv p_argv:...
 * @return int
 */
int main(int argc, char *argv[]) {

    signal(SIGPIPE, SIG_IGN);

    n_outstanding = 0;
    reconnect_timeout = 1;

    PVCApplication app(argc, argv);

    app.setOrganizationName(QStringLiteral("pavucontrol-qt"));
    app.setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    QString locale = QLocale::system().name();
    QTranslator qtTranslator;
    if(qtTranslator.load(QStringLiteral("qt_") + locale, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        qApp->installTranslator(&qtTranslator);

    QTranslator appTranslator;
    if(appTranslator.load(QStringLiteral("pavucontrol-qt_") + locale, QStringLiteral(PAVUCONTROL_QT_DATA_DIR) + QStringLiteral("/translations")))
        qApp->installTranslator(&appTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("PulseAudio Volume Control"));
    parser.addHelpOption();

    const QString VERINFO = QStringLiteral(PAVUCONTROLQT_VERSION
                                           "\nQt        " QT_VERSION_STR);
    app.setApplicationVersion(VERINFO);
    parser.addVersionOption();

    QCommandLineOption tabOption(QStringList() << QStringLiteral("tab") << QStringLiteral("t"), QObject::tr("Select a specific tab on load."), QStringLiteral("tab"));
    parser.addOption(tabOption);

    QCommandLineOption retryOption(QStringList() << QStringLiteral("retry") << QStringLiteral("r"), QObject::tr("Retry forever if pa quits (every 5 seconds)."));
    parser.addOption(retryOption);

    QCommandLineOption maximizeOption(QStringList() << QStringLiteral("maximize") << QStringLiteral("m"), QObject::tr("Maximize the window."));
    parser.addOption(maximizeOption);

    parser.process(app);
    default_tab = parser.value(tabOption).toInt();
    retry = parser.isSet(retryOption);

    // ca_context_set_driver(ca_gtk_context_get(), "pulse");

    MainWindow* mainWindow = new MainWindow();
    if(parser.isSet(maximizeOption))
        mainWindow->showMaximized();

    app.setMainWindow(mainWindow);

#ifdef USE_THREADED_GLLOOP
    mainloop = pa_threaded_mainloop_new();
    g_assert(mainloop);
    api = pa_threaded_mainloop_get_api(mainloop);
    g_assert(api);
#else
    pa_glib_mainloop *m = pa_glib_mainloop_new(g_main_context_default());
    g_assert(m);
    api = pa_glib_mainloop_get_api(m);
    g_assert(api);
#endif

    connect_to_pulse(&app);
    if (reconnect_timeout >= 0) {
#ifdef USE_THREADED_GLLOOP
        pa_threaded_mainloop_start(mainloop);
#endif
        mainWindow->show();
        app.exec();
    }

    if (reconnect_timeout < 0)
        show_translated_error("Fatal Error: Unable to connect to PulseAudio");

// #ifdef USE_THREADED_GLLOOP
//     pa_threaded_mainloop_stop(mainloop);
//     pa_threaded_mainloop_free(mainloop);
// #endif

    delete mainWindow;

    if (context) {
        pa_context_disconnect(context);
        pa_context_unref(context);
    }
// Be nice and free the pa_glib_mainloop used with Qt's GLib-based event dispatcher.
// Don't do the equivalent when using the pa_threaded_mainloop so it can do its own
// cleanup/housekeeping (and we avoid instabilities on exit).
#ifndef USE_THREADED_GLLOOP
    pa_glib_mainloop_free(m);
#endif

    return 0;
}
