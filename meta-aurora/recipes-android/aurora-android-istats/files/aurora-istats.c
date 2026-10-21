/*
 * aurora-istats: stand-in for Android's IStats service in the container.
 *
 * mcu_mgmtd waits for android.frameworks.stats.IStats/default, which the
 * container has no framework to provide, so it asks the service manager for
 * it every second forever and keeps the CPU awake. Register a service that
 * accepts and drops what is reported (reportVendorAtom is oneway).
 */
#include <gbinder.h>
#include <glib.h>
#include <glib-unix.h>

#include <signal.h>
#include <stdio.h>

#define ISTATS_IFACE "android.frameworks.stats.IStats"
#define ISTATS_NAME ISTATS_IFACE "/default"

/* AIDL meta-transactions */
#define AIDL_GET_INTERFACE_HASH    0x00fffffd
#define AIDL_GET_INTERFACE_VERSION 0x00fffffe

typedef struct {
    GBinderServiceManager *sm;
    GBinderLocalObject *obj;
    gulong presence_id;
    gulong add_id;
} App;

static GBinderLocalReply *handle(GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
                                 guint flags, int *status, void *user_data)
{
    GBinderLocalReply *reply;

    (void)req;
    (void)user_data;
    *status = GBINDER_STATUS_OK;
    if (flags & GBINDER_TX_FLAG_ONEWAY)
        return NULL;

    /* no exception, then the result if there is one */
    reply = gbinder_local_object_new_reply(obj);
    gbinder_local_reply_append_int32(reply, 0);
    if (code == AIDL_GET_INTERFACE_VERSION)
        gbinder_local_reply_append_int32(reply, 1);
    else if (code == AIDL_GET_INTERFACE_HASH)
        gbinder_local_reply_append_string16(reply, "notfrozen");
    return reply;
}

static void added(GBinderServiceManager *sm, int status, void *user_data)
{
    App *app = user_data;

    (void)sm;
    app->add_id = 0;
    if (status == GBINDER_STATUS_OK)
        fprintf(stderr, "registered %s\n", ISTATS_NAME);
    else
        fprintf(stderr, "registering %s failed: %d\n", ISTATS_NAME, status);
}

/* (Re)register whenever the service manager is (back) up */
static void presence_changed(GBinderServiceManager *sm, void *user_data)
{
    App *app = user_data;

    if (!gbinder_servicemanager_is_present(sm))
        return;
    gbinder_servicemanager_cancel(sm, app->add_id);
    app->add_id = gbinder_servicemanager_add_service(sm, ISTATS_NAME, app->obj, added, app);
}

static gboolean quit(gpointer loop)
{
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void)
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    App app = { 0 };

    setvbuf(stderr, NULL, _IOLBF, 0);
    app.sm = gbinder_servicemanager_new("/dev/binder");
    if (!app.sm) {
        fprintf(stderr, "no service manager on /dev/binder\n");
        return 1;
    }
    app.obj = gbinder_servicemanager_new_local_object(app.sm, ISTATS_IFACE, handle, &app);
    gbinder_local_object_set_stability(app.obj, GBINDER_STABILITY_VINTF);

    app.presence_id = gbinder_servicemanager_add_presence_handler(app.sm, presence_changed, &app);
    presence_changed(app.sm, &app);

    g_unix_signal_add(SIGTERM, quit, loop);
    g_unix_signal_add(SIGINT, quit, loop);
    g_main_loop_run(loop);

    gbinder_servicemanager_remove_handler(app.sm, app.presence_id);
    gbinder_servicemanager_cancel(app.sm, app.add_id);
    gbinder_local_object_unref(app.obj);
    gbinder_servicemanager_unref(app.sm);
    g_main_loop_unref(loop);
    return 0;
}
