/*
 * Copyright (C) 2019 Konsulko Group
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

#ifndef BLUETOOTH_MAP_COMMON_H
#define BLUETOOTH_MAP_COMMON_H

#include <stddef.h>

#define _GNU_SOURCE
#include <glib.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-object.h>

#include <json-c/json.h>

#define AFB_BINDING_VERSION 3
#include <afb/afb-binding.h>

#define MAP_UUID		"00001133-0000-1000-8000-00805f9b34fb"

struct call_work;

struct map_state {
	GMainLoop *loop;
	GDBusConnection *conn;
	guint message_sub;

	afb_event_t notification_event;

	/* NOTE: single connection allowed for now */
	/* NOTE: needs locking and a list */
	GMutex cw_mutex;
	int next_cw_id;
	GSList *cw_pending;
	struct call_work *cw;

	/* OBEX session path */
	gchar *session_path;

	/* xfer queue table */
	GHashTable *xfer_queue;
};

struct init_data {
	GCond cond;
	GMutex mutex;
	gboolean init_done;
	afb_api_t api;
	struct map_state *ns; /* before setting afb_api_set_userdata() */
	int rc;
};

struct call_work {
	struct bluetooth_state *ns;
	int id;
	gchar *access_type;
	gchar *type_arg;
	gchar *method;
	struct bluez_pending_work *cpw;
	afb_req_t request;
	GDBusMethodInvocation *invocation;
};

/**
 * Structure for converting from dbus properties to json
 * and vice-versa.
 * Note this is _not_ a generic dbus json bridge since
 * some constructs are not easily mapped.
 */
struct property_info {
	const char *name;	/* the connman property name */
	const char *json_name;	/* the json name (if NULL autoconvert) */
	const char *fmt;
	unsigned int flags;
	const struct property_info *sub;
};

#define PI_CONFIG	(1U << 0)

const struct property_info *property_by_dbus_name(
		const struct property_info *pi,
		const gchar *dbus_name,
		gboolean *is_config);
const struct property_info *property_by_json_name(
		const struct property_info *pi,
		const gchar *json_name,
		gboolean *is_config);
const struct property_info *property_by_name(
		const struct property_info *pi,
		gboolean is_json_name, const gchar *name,
		gboolean *is_config);

gchar *property_get_json_name(const struct property_info *pi,
		const gchar *name);
gchar *property_get_name(const struct property_info *pi,
		const gchar *json_name);

gchar *configuration_dbus_name(const gchar *dbus_name);
gchar *configuration_json_name(const gchar *json_name);

gchar *property_name_dbus2json(const struct property_info *pi,
		gboolean is_config);

json_object *property_dbus2json(
		const struct property_info **pip,
		const gchar *key, GVariant *var,
		gboolean *is_config);

gboolean root_property_dbus2json(
		json_object *jparent,
		const struct property_info *pi,
		const gchar *key, GVariant *var,
		gboolean *is_config);

GVariant *property_json_to_gvariant(
		const struct property_info *pi,
		const char *fmt,	/* if NULL use pi->fmt */
		const struct property_info *pi_parent,
		json_object *jval,
		GError **error);

typedef enum {
	NB_ERROR_BAD_TECHNOLOGY,
	NB_ERROR_BAD_SERVICE,
	NB_ERROR_OUT_OF_MEMORY,
	NB_ERROR_NO_SERVICES,
	NB_ERROR_BAD_PROPERTY,
	NB_ERROR_UNIMPLEMENTED,
	NB_ERROR_UNKNOWN_PROPERTY,
	NB_ERROR_UNKNOWN_SERVICE,
	NB_ERROR_UNKNOWN_TRANSFER,
	NB_ERROR_MISSING_ARGUMENT,
	NB_ERROR_ILLEGAL_ARGUMENT,
	NB_ERROR_CALL_IN_PROGRESS,
} NBError;

#define NB_ERROR (nb_error_quark())

extern GQuark nb_error_quark(void);

json_object *get_property_collect(json_object *jreqprop, json_object *jprop,
		GError **error);
json_object *get_named_property(const struct property_info *pi,
		gboolean is_json_name, const char *name, json_object *jprop);


#endif /* BLUETOOTH_MAP_COMMON_H */
