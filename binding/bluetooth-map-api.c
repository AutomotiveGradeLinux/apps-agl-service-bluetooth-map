/*
 * Copyright (C) 2019 Konsulko Group
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
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

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <glib.h>
#include <gio/gio.h>
#include <json-c/json.h>

#define AFB_BINDING_VERSION 3
#include <afb/afb-binding.h>

#include "bluetooth-map-api.h"
#include "bluetooth-map-common.h"

/**
 * The global thread
 */
static GThread *global_thread;

static void signal_init_done(struct init_data *id, int rc)
{
	g_mutex_lock(&id->mutex);
	id->init_done = TRUE;
	id->rc = rc;
	g_cond_signal(&id->cond);
	g_mutex_unlock(&id->mutex);
}

struct map_state *map_get_userdata(afb_req_t request) {
	afb_api_t api = afb_req_get_api(request);
	return afb_api_get_userdata(api);
}

void call_work_lock(struct map_state *ns)
{
	g_mutex_lock(&ns->cw_mutex);
}

void call_work_unlock(struct map_state *ns)
{
	g_mutex_unlock(&ns->cw_mutex);
}

struct call_work *call_work_lookup_unlocked(
		struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method)
{
	struct call_work *cw;
	GSList *list;

	/* we can only allow a single pending call */
	for (list = ns->cw_pending; list; list = g_slist_next(list)) {
		cw = list->data;
		if (!g_strcmp0(access_type, cw->access_type) &&
		    !g_strcmp0(type_arg, cw->type_arg) &&
		    !g_strcmp0(method, cw->method))
			return cw;
	}
	return NULL;
}

struct call_work *call_work_lookup(
		struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method)
{
	struct call_work *cw;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_lookup_unlocked(ns, access_type, type_arg, method);
	g_mutex_unlock(&ns->cw_mutex);

	return cw;
}

int call_work_pending_id(
		struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method)
{
	struct call_work *cw;
	int id = -1;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_lookup_unlocked(ns, access_type, type_arg, method);
	if (cw)
		id = cw->id;
	g_mutex_unlock(&ns->cw_mutex);

	return id;
}

struct call_work *call_work_lookup_by_id_unlocked(
		struct map_state *ns, int id)
{
	struct call_work *cw;
	GSList *list;

	/* we can only allow a single pending call */
	for (list = ns->cw_pending; list; list = g_slist_next(list)) {
		cw = list->data;
		if (cw->id == id)
			return cw;
	}
	return NULL;
}

struct call_work *call_work_lookup_by_id(
		struct map_state *ns, int id)
{
	struct call_work *cw;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_lookup_by_id_unlocked(ns, id);
	g_mutex_unlock(&ns->cw_mutex);

	return cw;
}

struct call_work *call_work_create_unlocked(struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method, const char *bluez_method,
		GError **error)
{

	struct call_work *cw = NULL;

	cw = call_work_lookup_unlocked(ns, access_type, type_arg, method);
	if (cw) {
		g_set_error(error, NB_ERROR, NB_ERROR_CALL_IN_PROGRESS,
				"another call in progress (%s/%s/%s)",
				access_type, type_arg, method);
		return NULL;
	}

	/* no other pending; allocate */
	cw = g_malloc0(sizeof(*cw));
	cw->ns = ns;
	do {
		cw->id = ns->next_cw_id;
		if (++ns->next_cw_id < 0)
			ns->next_cw_id = 1;
	} while (call_work_lookup_by_id_unlocked(ns, cw->id));

	cw->access_type = g_strdup(access_type);
	cw->type_arg = g_strdup(type_arg);
	cw->method = g_strdup(method);
	cw->bluez_method = g_strdup(bluez_method);

	ns->cw_pending = g_slist_prepend(ns->cw_pending, cw);

	return cw;
}

struct call_work *call_work_create(struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method, const char *bluez_method,
		GError **error)
{

	struct call_work *cw;

	g_mutex_lock(&ns->cw_mutex);
	cw = call_work_create_unlocked(ns,
			access_type, type_arg, method, bluez_method,
			error);
	g_mutex_unlock(&ns->cw_mutex);

	return cw;
}

void call_work_destroy_unlocked(struct call_work *cw)
{
	struct map_state *ns = cw->ns;
	struct call_work *cw2;

	/* verify that it's something we know about */
	cw2 = call_work_lookup_by_id_unlocked(ns, cw->id);
	if (cw2 != cw) {
		AFB_ERROR("Bad call work to destroy");
		return;
	}

	/* remove it */
	ns->cw_pending = g_slist_remove(ns->cw_pending, cw);

	g_free(cw->access_type);
	g_free(cw->type_arg);
	g_free(cw->method);
	g_free(cw->bluez_method);
}


void call_work_destroy(struct call_work *cw)
{
	struct map_state *ns = cw->ns;

	g_mutex_lock(&ns->cw_mutex);
	call_work_destroy_unlocked(cw);
	g_mutex_unlock(&ns->cw_mutex);
}

static afb_event_t get_event_from_value(struct map_state *ns,
			const char *value)
{
	if (!g_strcmp0(value, "notification"))
		return ns->notification_event;

	return NULL;
}

static void map_subscribe_unsubscribe(afb_req_t request,
		gboolean unsub)
{
	struct map_state *ns = map_get_userdata(request);
	json_object *jresp = json_object_new_object();
	const char *value;
	afb_event_t event;
	int rc;

	/* if value exists means to set offline mode */
	value = afb_req_value(request, "value");
	if (!value) {
		afb_req_fail_f(request, "failed", "Missing \"value\" event");
		return;
	}

	event = get_event_from_value(ns, value);
	if (!event) {
		afb_req_fail_f(request, "failed", "Bad \"value\" event \"%s\"",
				value);
		return;
	}

	if (!unsub) {
		rc = afb_req_subscribe(request, event);
	} else {
		rc = afb_req_unsubscribe(request, event);
	}
	if (rc != 0) {
		afb_req_fail_f(request, "failed",
					"%s error on \"value\" event \"%s\"",
					!unsub ? "subscribe" : "unsubscribe",
					value);
		return;
	}

	afb_req_success_f(request, jresp, "Bluetooth MAP %s to event \"%s\"",
			!unsub ? "subscribed" : "unsubscribed",
			value);
}

static void push_message_callback(void *user_data,
		GVariant *result, GError **error)
{
	struct call_work *cw = user_data;
	struct map_state *ns = cw->ns;
	struct xfer_tuple *data;
	gchar *path = NULL;

	bluez_decode_call_error(ns,
		cw->access_type, cw->type_arg, cw->method,
		error);

	if (error && *error) {
		afb_req_fail_f(cw->request, "failed", "OBEX error: %s",
				(*error)->message);
		goto out_free;
	} else if (!result) {
		goto out_free;
	}

	g_variant_get(result, "(&oa{sv})", &path, NULL);

	afb_req_addref(cw->request);

	data = g_try_malloc0(sizeof(*data));
	data->type = XFER_SENTMSG;
	data->value = cw->request;

	g_hash_table_insert(ns->xfer_queue, g_strdup(path), data);

	g_variant_unref(result);
out_free:
	afb_req_unref(cw->request);
	call_work_destroy(cw);
}

static void compose(afb_req_t request)
{
	struct map_state *ns = map_get_userdata(request);
	GError *error = NULL;
	GString *message = NULL;
	GVariant *params, *reply;
	gchar *name, *session;
	struct call_work *cw;
	int fd;

	call_work_lock(ns);
	if (!ns || !ns->session_path) {
		afb_req_fail_f(request, "failed", "no valid OBEX session");
		call_work_unlock(ns);
		return;
	}
	session = g_strdup(ns->session_path);
	call_work_unlock(ns);

	message = bmessage_encoder(request);
	if (!message) {
		afb_req_fail_f(request, "failed", "no valid message provided");
		goto err_msg_invalid;
	}

	fd = g_file_open_tmp("obex-clientXXXXXX", &name, NULL);
	close(fd);

	g_file_set_contents(name, message->str, -1, NULL);
	g_string_free(message, TRUE);

	params = g_variant_new("(&s)", "/telecom/msg/OUTBOX");
	reply = bluez_call(ns, BLUEZ_AT_MESSAGEACCESS, session, "SetFolder", params, NULL);
	if (reply) g_variant_unref(reply);

	cw = call_work_create(ns, BLUEZ_AT_MESSAGEACCESS, session,
			"queue_message", "PushMessage", &error);
	if (!cw) {
		afb_req_fail_f(request, "failed", "can't queue work %s",
				error->message);
		g_error_free(error);
		goto err_queue_free;
	}

	cw->request = request;
	afb_req_addref(request);

	params = g_variant_new("(&s&sa{sv})", name, "", NULL);
	cw->cpw = bluez_call_async(ns, BLUEZ_AT_MESSAGEACCESS, session,
				   "PushMessage", params, &error,
				    push_message_callback, cw);

	if (!cw->cpw) {
		afb_req_fail_f(request, "failed", "connection error %s",
				error->message);
		call_work_destroy(cw);
		g_error_free(error);
		/* fall-thru */
	}

err_queue_free:
	g_free(name);

err_msg_invalid:
	g_free(session);
}


static void get_message_callback(void *user_data,
		GVariant *result, GError **error)
{
	struct call_work *cw = user_data;
	struct map_state *ns = cw->ns;

	bluez_decode_call_error(ns,
		cw->access_type, cw->type_arg, cw->method,
		error);

	if (error && *error) {
		afb_req_fail_f(cw->request, "failed", "OBEX error: %s",
				(*error)->message);
		goto out_free;
	}

	if (!result) {
		goto out_free;
	}

	g_variant_unref(result);

	return;

out_free:
	afb_req_unref(cw->request);
	call_work_destroy(cw);
}

static void message(afb_req_t request)
{
	struct map_state *ns = map_get_userdata(request);
	GError *error = NULL;
	GVariant *params;
	const char *handle;
	gchar *path, *filename;
	struct call_work *cw;

	handle = afb_req_value(request, "handle");
	if (!handle) {
		afb_req_fail_f(request, "failed", "no handle value passed");
		return;
	}

	call_work_lock(ns);
	if (!ns || !ns->session_path) {
		afb_req_fail_f(request, "failed", "no valid OBEX session");
		call_work_unlock(ns);
		return;
	}
	path = g_strconcat(ns->session_path, "/", handle, NULL);
	call_work_unlock(ns);

	cw = call_work_create(ns, BLUEZ_AT_MESSAGE, NULL,
			"get_message", "Get", &error);
	if (!cw) {
		afb_req_fail_f(request, "failed", "can't queue work %s",
				error->message);
		g_error_free(error);
		goto err_queue_free;
	}

	cw->request = request;
	afb_req_addref(request);

	filename = g_strconcat("/tmp/obex-message-", handle, NULL);
	params = g_variant_new("(&sb)", filename, g_variant_new_boolean(FALSE));
	cw->cpw = bluez_call_async(ns, BLUEZ_AT_MESSAGE, path,
				   "Get", params, &error,
				    get_message_callback, cw);
	g_free(filename);

	if (!cw->cpw) {
		afb_req_fail_f(request, "failed", "connection error %s",
				error->message);
		call_work_destroy(cw);
		g_error_free(error);
		/* fall-thru */
	}

err_queue_free:
	g_free(path);
}

static void list_msgs(afb_req_t request)
{
	struct map_state *ns = map_get_userdata(request);
	GVariant *params, *reply;
	GVariantIter *iter = NULL, *iter2 = NULL;
	const char *folder;
	const gchar *path = NULL;
	json_object *jresp;
	gchar *session;

	folder = afb_req_value(request, "folder");
	if (!folder) {
		afb_req_fail_f(request, "failed", "no folder value passed");
		return;
	}

	call_work_lock(ns);
	session = g_strdup(ns->session_path);

	params = g_variant_new("(&s)", "/telecom/msg");
	reply = bluez_call(ns, BLUEZ_AT_MESSAGEACCESS, session, "SetFolder", params, NULL);
	if (!reply) {
		afb_req_fail_f(request, "failed", "cannot switch to telecom/msg");
		goto out_free;
	}
	g_variant_unref(reply);

	params = g_variant_new("(&sa{sv})", folder, NULL);
	reply = bluez_call(ns, BLUEZ_AT_MESSAGEACCESS, session, "ListMessages", params, NULL);
	if (!reply) {
		afb_req_fail_f(request, "failed",
			"Cannot list messages in telecom/msg/%s", folder);
		goto out_free;
	}

	jresp = json_object_new_object();

	g_variant_get(reply, "(a{oa{sv}})", &iter);
	while (g_variant_iter_loop(iter, "{oa{sv}}", &path, &iter2)) {
		const char *key = NULL;
		GVariant *val = NULL;
		json_object *msg = json_object_new_object();

		while (g_variant_iter_loop(iter2, "{sv}", &key, &val)) {
			GError *error = NULL;
			gboolean is_config, ret;

			ret = message_property_dbus2json(msg, key, val,
							 &is_config, &error);
			g_variant_unref(val);
			if (!ret) {
				AFB_DEBUG("%s property %s - %s",
						path,
						key, error->message);
				g_clear_error(&error);
			}
		}
		json_object_object_add(jresp, path + strlen(session) + 1, msg);
	}

	afb_req_success_f(request, jresp, "Bluetooth MAP folder listing");

out_free:
	call_work_unlock(ns);
	g_free(session);
}

static void subscribe(afb_req_t request)
{
	map_subscribe_unsubscribe(request, FALSE);
}

static void unsubscribe(afb_req_t request)
{
	map_subscribe_unsubscribe(request, TRUE);
}

static void map_request_message(struct map_state *ns, const gchar *path)
{
	GVariant *reply, *params =
		g_variant_new("(&sb)", "", g_variant_new_boolean(FALSE));
	reply = bluez_call(ns, BLUEZ_AT_MESSAGE, path, "Get", params, NULL);
	if (reply)
		g_variant_unref(reply);
}

static void map_notification_event(struct map_state *ns, gchar *filename)
{
	json_object *jresp;
	gchar *buf;

	if (!g_file_get_contents(filename, &buf, NULL, NULL))
		return;

	jresp = bmessage_parse(buf);
	if (jresp)
		afb_event_push(ns->notification_event, jresp);
	g_free(buf);
}

static gboolean map_notification_check(GVariant *var)
{
	GVariantIter *array = g_variant_iter_new(var);
	const char *name = NULL;
	GVariant *val = NULL;

	while (g_variant_iter_next(array, "{&sv}", &name, &val)) {
		if (g_strcmp0("Status", name))
			continue;
		return !g_strcmp0("notification", g_variant_get_string(val, NULL));
	}

	return FALSE;
}

static void map_message_response(struct map_state *ns, gchar *filename)
{
	struct call_work *cw;
	json_object *jresp;
	gchar *buf;

	if (!g_file_get_contents(filename, &buf, NULL, NULL))
		return;

	jresp = bmessage_parse(buf);

	call_work_lock(ns);

	cw = call_work_lookup_unlocked(ns, BLUEZ_AT_MESSAGE, NULL, "get_message");
	if (!cw) {
		if (jresp)
			json_object_put(jresp);
		return;
	}


	afb_req_success_f(cw->request, jresp, "Bluetooth MAP message result");

	call_work_destroy_unlocked(cw);

	call_work_unlock(ns);
	g_free(buf);
}

static void bluez_map_signal_callback(
	GDBusConnection *connection,
	const gchar *sender_name,
	const gchar *object_path,
	const gchar *interface_name,
	const gchar *signal_name,
	GVariant *parameters,
	gpointer user_data)
{
	struct map_state *ns = user_data;
	GVariant *var = NULL;
	const gchar *path = NULL;
	const gchar *key = NULL;
	GVariantIter *array = NULL, *array1 = NULL;

	/* AFB_INFO("sender=%s", sender_name);
	AFB_INFO("object_path=%s", object_path);
	AFB_INFO("interface=%s", interface_name);
	AFB_INFO("signal=%s", signal_name); */

	if (!g_strcmp0(signal_name, "InterfacesAdded")) {

		g_variant_get(parameters, "(&oa{sa{sv}})", &path, &array);

		while (g_variant_iter_next(array, "{&s@a{sv}}", &key, &var)) {
			const char *name = NULL;
			GVariant *val = NULL;

			if (!g_strcmp0(key, BLUEZ_OBEX_MESSAGE_INTERFACE)) {

				/* ListMessages will send all messages as events
				 * if notification status isn't checked
				 */
				if (!map_notification_check(var))
					return;

				map_request_message(ns, path);
				continue;
			}

			if (g_strcmp0(key, BLUEZ_OBEX_TRANSFER_INTERFACE))
				continue;

			call_work_lock(ns);
			if (!ns->session_path || strncmp(ns->session_path, path, strlen(ns->session_path))) {
				call_work_unlock(ns);
				continue;
			}
			call_work_unlock(ns);

			array1 = g_variant_iter_new(var);

			while (g_variant_iter_next(array1, "{&sv}", &name, &val)) {
				struct xfer_tuple *data;
				gchar *filename;

				if (g_strcmp0(name, "Filename"))
					continue;

				call_work_lock(ns);
				data = g_try_malloc0(sizeof(*data));
				filename = g_strdup(g_variant_get_string(val, NULL));

				// TODO: figure a better way that isn't based on name
				if (strstr(filename, "obex-message-"))
					data->type = XFER_MESSAGE;
				else
					data->type = XFER_NOTIFICATION;
				data->value = filename;
				g_hash_table_insert(ns->xfer_queue, g_strdup(path), data);
				call_work_unlock(ns);
				break;
			}
		}

	} else if (!g_strcmp0(signal_name, "PropertiesChanged")) {

		g_variant_get(parameters, "(&sa{sv}as)", &path, &array, &array1);

		if (g_strcmp0(path, BLUEZ_OBEX_TRANSFER_INTERFACE))
			return;

		while (g_variant_iter_next(array, "{&sv}", &key, &var)) {
			struct xfer_tuple *val;
			const gchar *status;

			// only check Status field
			if (g_strcmp0(key, "Status"))
				continue;

			// only need the "complete" or "error" Status
			status = g_variant_get_string(var, NULL);
			if (g_strcmp0(status, "complete") && g_strcmp0(status, "error"))
				break;

			call_work_lock(ns);
			val = g_hash_table_lookup(ns->xfer_queue, object_path);
			if (val)
				g_hash_table_remove(ns->xfer_queue, object_path);
			call_work_unlock(ns);

			if (!val)
				break;

			if (val->type == XFER_NOTIFICATION) {
				if (!g_strcmp0(status, "complete"))
					map_notification_event(ns, (gchar *) val->value);

				g_free(val->value);
				g_free(val);
				break;
			} else if (val->type == XFER_SENTMSG) {
				afb_req_t request = (afb_req_t) val->value;

				if (!g_strcmp0(status, "complete"))
					afb_req_success_f(request, NULL, "OBEX - transfer %s completed", object_path);
				else
					afb_req_fail_f(request, "failed", "OBEX - transfer %s failed", object_path);

				afb_req_unref(request);
				g_free(val);
				break;
			} else if (val->type == XFER_MESSAGE) {
				if (!g_strcmp0(status, "complete"))
					map_message_response(ns, (gchar *) val->value);

				g_free(val->value);
				g_free(val);
				break;
			}
		}
	}
}

static struct map_state *map_init(GMainLoop *loop)
{
	struct map_state *ns;
	GError *error = NULL;

	ns = g_try_malloc0(sizeof(*ns));
	if (!ns) {
		AFB_ERROR("out of memory allocating map state");
		goto err_no_ns;
	}

	AFB_INFO("connecting to dbus");

	ns->loop = loop;
	ns->conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
	if (!ns->conn) {
		if (error)
			g_dbus_error_strip_remote_error(error);
		AFB_ERROR("Cannot connect to D-Bus, %s",
				error ? error->message : "unspecified");
		g_error_free(error);
		goto err_no_conn;

	}

	AFB_INFO("connected to dbus");

	ns->notification_event =
		afb_daemon_make_event("notification");

	if (!afb_event_is_valid(ns->notification_event)) {
		AFB_ERROR("Cannot create events");
		goto err_no_events;
	}

	ns->xfer_queue = g_hash_table_new(g_str_hash, g_str_equal);
	ns->message_sub = g_dbus_connection_signal_subscribe(
			ns->conn,
			BLUEZ_OBEX_SERVICE,
			NULL,   /* interface */
			NULL,	/* member */
			NULL,	/* object path */
			NULL,	/* arg0 */
			G_DBUS_SIGNAL_FLAGS_NONE,
			bluez_map_signal_callback,
			ns,
			NULL);
	if (!ns->message_sub) {
		AFB_ERROR("Unable to subscribe to interface signals");
		goto err_no_message_sub;
	}

	g_mutex_init(&ns->cw_mutex);
	ns->next_cw_id = 1;

	return ns;

err_no_message_sub:
	/* no way to clear the events */
err_no_events:
	g_dbus_connection_close(ns->conn, NULL, NULL, NULL);
err_no_conn:
	g_free(ns);
err_no_ns:
	return NULL;
}

static void map_cleanup(struct map_state *ns)
{
	g_dbus_connection_signal_unsubscribe(ns->conn, ns->message_sub);
	g_dbus_connection_close(ns->conn, NULL, NULL, NULL);
	g_free(ns);
}

static gpointer map_func(gpointer ptr)
{
	struct init_data *id = ptr;
	struct map_state *ns;
	GMainLoop *loop;

	loop = g_main_loop_new(NULL, FALSE);
	if (!loop) {
		AFB_ERROR("Unable to create main loop");
		goto err_no_loop;
	}

	/* real map init */
	ns = map_init(loop);
	if (!ns) {
		AFB_ERROR("map_init() failed");
		goto err_no_ns;
	}

	id->ns = ns;
	ns->loop = loop;

	afb_api_set_userdata(id->api, ns);
	signal_init_done(id, 0);

	g_main_loop_run(loop);
	g_main_loop_unref(ns->loop);

	map_cleanup(ns);
	afb_api_set_userdata(id->api, NULL);

	return NULL;

err_no_ns:
	g_main_loop_unref(loop);

err_no_loop:
	return NULL;
}

static gboolean map_create_session(struct map_state *ns, const char *address)
{
	GVariant *reply, *params, *dict;
	GVariantBuilder builder;
	gchar *val;

	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&builder, "{sv}", "Target", g_variant_new_string("map"));
	dict = g_variant_builder_end(&builder);
	params = g_variant_new("(&s@a{sv})", address, dict);

	reply = bluez_call(ns, BLUEZ_AT_CLIENT, NULL, "CreateSession", params, NULL);
	if (!reply) {
		AFB_ERROR("Cannot open MAP OBEX session");
		return FALSE;
	}

	call_work_lock(ns);

	g_variant_get(reply, "(&o)", &val);
	ns->session_path = g_strdup(val);

	call_work_unlock(ns);

	g_variant_unref(reply);

	return TRUE;
}

static gboolean is_map_dev_and_init(struct map_state *ns, struct json_object *dev)
{
	struct json_object *props = NULL, *val = NULL;
	int i;

	json_object_object_get_ex(dev, "properties", &props);
	if (!props)
		return FALSE;

	json_object_object_get_ex(props, "connected", &val);
	if (!val || !json_object_get_boolean(val))
		return FALSE;

	json_object_object_get_ex(props, "uuids", &val);
	for (i = 0; i < json_object_array_length(val); i++) {
		const char *uuid = json_object_get_string(json_object_array_get_idx(val, i));
		const char *address = NULL;
		struct json_object *val1 = NULL;

		if (g_strcmp0(MAS_UUID, uuid))
			continue;

		json_object_object_get_ex(props, "address", &val1);
		address = json_object_get_string(val1);

		if (!address)
			return FALSE;

		if (map_create_session(ns, address)) {
			json_object_object_get_ex(dev, "device", &val1);
			AFB_NOTICE("MAP device connected: %s", json_object_get_string(val1));

			return TRUE;
		}
		break;
	}

	return FALSE;
}

static void discovery_result_cb(void *closure, struct json_object *result,
				const char *error, const char *info,
				afb_api_t api)
{
	struct map_state *ns = afb_api_get_userdata(api);
	enum json_type type;
	struct json_object *dev, *tmp;
	int i;

	if (!json_object_object_get_ex(result, "devices", &tmp))
		return;
	type = json_object_get_type(tmp);

	if (type != json_type_array)
		return;

	for (i = 0; i < json_object_array_length(tmp); i++) {
		dev = json_object_array_get_idx(tmp, i);
		if (is_map_dev_and_init(ns, dev))
			return;
	}
}

static void process_connection_event(afb_api_t api, struct json_object *object)
{
	struct json_object *val = NULL, *props = NULL;
	const char *action;

	json_object_object_get_ex(object, "action", &val);
	if (!val)
		return;
	action = json_object_get_string(val);
	if (g_strcmp0("changed", action))
		return;

	json_object_object_get_ex(object, "properties", &props);
	if (!props)
		return;

	json_object_object_get_ex(props, "connected", &val);
	if (!val)
		return;

	if (json_object_get_boolean(val)) {
		struct json_object *args = json_object_new_object();
		afb_api_call(api, "Bluetooth-Manager", "managed_objects",
			     args, discovery_result_cb, NULL);
		return;
	}
}

static int init(afb_api_t api)
{
	struct init_data init_data, *id = &init_data;
	json_object *args;
	gint64 end_time;
	int ret;

	memset(id, 0, sizeof(*id));
	id->init_done = FALSE;
	id->rc = 0;
	id->api = api;
	g_cond_init(&id->cond);
	g_mutex_init(&id->mutex);

	ret = afb_daemon_require_api("Bluetooth-Manager", 1);
	if (ret) {
		AFB_ERROR("unable to initialize bluetooth binding");
		return -EINVAL;
	}

	global_thread = g_thread_new("agl-service-bluetooth-map", map_func, id);

	AFB_INFO("bluetooth-map binding waiting for init done");

	/* wait maximum 3 seconds for init done */
	end_time = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
	g_mutex_lock(&id->mutex);
	while (!id->init_done) {
		if (!g_cond_wait_until(&id->cond, &id->mutex, end_time))
			break;
	}
	g_mutex_unlock(&id->mutex);

	/* subscribe to Bluetooth-Manager events */
	args = json_object_new_object();
	json_object_object_add(args , "value", json_object_new_string("device_changes"));
	afb_api_call_sync(api, "Bluetooth-Manager", "subscribe", args, NULL, NULL, NULL);

	args = json_object_new_object();
	afb_api_call(api, "Bluetooth-Manager", "managed_objects", args, discovery_result_cb, NULL);

	return id->rc;
}

static void onevent(afb_api_t api, const char *event, struct json_object *object)
{
	if (!g_ascii_strcasecmp(event, "Bluetooth-Manager/device_changes"))
		process_connection_event(api, object);
	else
		AFB_ERROR("Unsupported event: %s\n", event);
}

static const afb_verb_t binding_verbs[] = {
	{ .verb = "compose",       .callback = compose,     .info = "Compose message" },
	{ .verb = "message",       .callback = message,     .info = "Retrieve message" },
	{ .verb = "list_messages", .callback = list_msgs,   .info = "List messages" },
	{ .verb = "subscribe",     .callback = subscribe,   .info = "Subscribe to events" },
	{ .verb = "unsubscribe",   .callback = unsubscribe, .info = "Unsubscribe to events" },
	{}
};

/*
 * description of the binding for afb-daemon
 */
const afb_binding_t afbBindingV3 = {
	.api = "bluetooth-map",
	.verbs = binding_verbs,
	.init = init,
	.onevent = onevent,
};
