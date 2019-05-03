/*
 * Copyright 2019 Konsulko Group
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 * Author: Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef BLUETOOTH_MAP_API_H
#define BLUETOOTH_MAP_API_H

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <glib.h>
#include <json-c/json.h>

#define BLUEZ_OBEX_SERVICE                 	"org.bluez.obex"
#define BLUEZ_OBEX_CLIENT_INTERFACE		BLUEZ_OBEX_SERVICE ".Client1"
#define BLUEZ_OBEX_SESSION_INTERFACE		BLUEZ_OBEX_SERVICE ".Session1"
#define BLUEZ_OBEX_TRANSFER_INTERFACE		BLUEZ_OBEX_SERVICE ".Transfer1"
#define BLUEZ_OBEX_MESSAGE_INTERFACE		BLUEZ_OBEX_SERVICE ".Message1"
#define BLUEZ_OBEX_MESSAGEACCESS_INTERFACE	BLUEZ_OBEX_SERVICE ".MessageAccess1"

#define BLUEZ_OBJECT_PATH			"/"
#define BLUEZ_OBEX_PATH				"/org/bluez/obex"

#define BLUEZ_ERRMSG(error) \
     (error ? error->message : "unspecified")

#define FREEDESKTOP_INTROSPECT			"org.freedesktop.DBus.Introspectable"
#define FREEDESKTOP_PROPERTIES			"org.freedesktop.DBus.Properties"
#define FREEDESKTOP_OBJECTMANAGER		"org.freedesktop.DBus.ObjectManager"

#define DBUS_REPLY_TIMEOUT			(120 * 1000)
#define DBUS_REPLY_TIMEOUT_SHORT		(10 * 1000)

#define	BLUEZ_AT_OBJECT				"object"
#define BLUEZ_AT_CLIENT				"client"
#define BLUEZ_AT_SESSION			"session"
#define BLUEZ_AT_TRANSFER			"transfer"
#define BLUEZ_AT_MESSAGE			"message"
#define BLUEZ_AT_MESSAGEACCESS			"message-access"

struct map_state;

struct map_state *map_get_userdata(afb_req_t request);

struct call_work *call_work_create_unlocked(struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method, const char *bluez_method,
		GError **error);

void call_work_destroy_unlocked(struct call_work *cw);

void call_work_lock(struct map_state *ns);

void call_work_unlock(struct map_state *ns);

struct call_work *call_work_lookup_unlocked(
		struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method);

const struct property_info *bluez_get_property_info(
		const char *access_type, GError **error);

gboolean bluez_property_dbus2json(const char *access_type,
		json_object *jprop, const gchar *key, GVariant *var,
		gboolean *is_config,
		GError **error);

GVariant *bluez_call(struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method, GVariant *params, GError **error);

json_object *bluez_get_properties(struct map_state *ns,
		const char *access_type, const char *path,
		GError **error);

json_object *bluez_get_property(struct map_state *ns,
		const char *access_type, const char *type_arg,
		gboolean is_json_name, const char *name, GError **error);

gboolean bluez_set_property(struct map_state *ns,
		const char *access_type, const char *type_arg,
		gboolean is_json_name, const char *name, json_object *jval,
		GError **error);

gboolean bluetooth_autoconnect(gpointer data);

/* convenience access methods */
static inline gboolean session_property_dbus2json(json_object *jprop,
		const gchar *key, GVariant *var, gboolean *is_config,
		GError **error)
{
	return bluez_property_dbus2json(BLUEZ_AT_SESSION,
			jprop, key, var, is_config, error);
}

static inline gboolean transfer_property_dbus2json(json_object *jprop,
		const gchar *key, GVariant *var, gboolean *is_config,
		GError **error)
{
	return bluez_property_dbus2json(BLUEZ_AT_TRANSFER,
			jprop, key, var, is_config, error);
}

static inline json_object *object_properties(struct map_state *ns,
		GError **error)
{
	return bluez_get_properties(ns,
			BLUEZ_AT_OBJECT, BLUEZ_OBJECT_PATH, error);
}

struct bluez_pending_work {
	struct map_state *ns;
	void *user_data;
	GCancellable *cancel;
	void (*callback)(void *user_data, GVariant *result, GError **error);
};

void bluez_cancel_call(struct map_state *ns,
		struct bluez_pending_work *cpw);

struct bluez_pending_work *
bluez_call_async(struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method, GVariant *params, GError **error,
		void (*callback)(void *user_data, GVariant *result, GError **error),
		void *user_data);

void bluez_decode_call_error(struct map_state *ns,
		const char *access_type, const char *type_arg,
		const char *method,
		GError **error);

#endif /* BLUETOOTH_MAP_API_H */
