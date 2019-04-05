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
	GVariant *params =
		g_variant_new("(&sb)", "", g_variant_new_boolean(FALSE));
	bluez_call(ns, BLUEZ_AT_MESSAGE, path, "Get", params, NULL);
}

static void map_notification_event(struct map_state *ns, gchar *filename)
{
	json_object *jresp;
	gchar *buf;

	if (!g_file_get_contents(filename, &buf, NULL, NULL))
		return;

	jresp = json_object_new_object();
	json_object_object_add(jresp, "bmessage", json_object_new_string(buf));

	afb_event_push(ns->notification_event, jresp);
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
				map_request_message(ns, path);
				continue;
			}

			if (g_strcmp0(key, BLUEZ_OBEX_TRANSFER_INTERFACE))
				continue;

			array1 = g_variant_iter_new(var);

			while (g_variant_iter_next(array1, "{&sv}", &name, &val)) {
				if (g_strcmp0(name, "Filename"))
					continue;

				call_work_lock(ns);
				g_hash_table_insert(ns->xfer_queue, g_strdup(path),
					g_strdup(g_variant_get_string(val, NULL)));
				call_work_unlock(ns);
				break;
			}
		}

	} else if (!g_strcmp0(signal_name, "PropertiesChanged")) {

		g_variant_get(parameters, "(&sa{sv}as)", &path, &array, &array1);

		if (g_strcmp0(path, BLUEZ_OBEX_TRANSFER_INTERFACE))
			return;

		while (g_variant_iter_next(array, "{&sv}", &key, &var)) {
			gchar *filename;

			// only check Status field
			if (g_strcmp0(key, "Status"))
				continue;

			// only need the "complete" Status
			if (g_strcmp0(g_variant_get_string(var, NULL), "complete"))
				return;

			call_work_lock(ns);
			filename = (gchar *) g_hash_table_lookup(ns->xfer_queue, object_path);
			if (filename) {
				g_hash_table_remove(ns->xfer_queue, object_path);
				call_work_unlock(ns);

				map_notification_event(ns, filename);
				g_free(filename);
				break;
			}
			call_work_unlock(ns);
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

		if (g_strcmp0(MAP_UUID, uuid))
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
	const char *action, *device;

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

	json_object_object_get_ex(object, "device", &val);
	if (!val)
		return;

	device = json_object_get_string(val);

	AFB_NOTICE("MAP device disconnected: %s", device);
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
	{ .verb = "subscribe",	.callback = subscribe,	.info = "Subscribe to events" },
	{ .verb = "unsubscribe",.callback = unsubscribe,.info = "Unsubscribe to events" },
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
