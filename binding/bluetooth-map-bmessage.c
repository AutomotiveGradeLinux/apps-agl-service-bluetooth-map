/*
 * Copyright (C) 2019 Konsulko Group
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
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

static void populate_json_entry(gchar *msg, json_object *jresp)
{
	gchar **tmp = g_strsplit(msg, ":", 2);
	gchar *key = g_ascii_strdown(tmp[0], -1), *ptr;

	// TODO: process CHARSET in fields
	ptr = strstr(key, ";");
	if (ptr)
		*ptr = '\0';

	if (strlen(tmp[1]))
		json_object_object_add(jresp, key, json_object_new_string(tmp[1]));

	g_free(key);
	g_strfreev(tmp);
}

static gboolean __expect(gchar ***msg, gchar *str, json_object *jresp, gboolean autoincrement)
{
	gboolean ret = FALSE;
	gchar *tmp;

	if (!**msg)
		return FALSE;

	tmp = **msg;

	if (jresp) {
		ret = g_str_has_prefix(tmp, str);
		populate_json_entry(tmp, jresp);
	} else if (g_str_has_suffix(str, ":")) {
		ret = g_str_has_prefix(tmp, str);
	} else {
		if (!g_strcmp0(tmp, str))
			ret = TRUE;
	}

	if (autoincrement)
		(*msg)++;

	return ret;
}

#define expect(msg, str, jresp) { if (!__expect(msg, str, jresp, TRUE)) { return FALSE; }; }

static gboolean parse_sender_vcard(gchar ***msg, json_object *jresp)
{
	json_object *jobj;

	expect(msg, "VERSION:", NULL);

	jobj = json_object_new_object();
	json_object_object_add(jresp, "sender", jobj);

	expect(msg, "FN", jobj);
	expect(msg, "N", jobj);
	expect(msg, "TEL:", jobj);

	return TRUE;
}

static gboolean __bmessage_parse(gchar **msg, json_object *jresp)
{
	gboolean benv = FALSE, bmsg = FALSE, vcard = FALSE;
	GString *message = g_string_new(NULL);

	// BMSG: Begin of valid message
	expect(&msg, "BEGIN:BMSG", NULL);

	// BMSG: Both Android and iOS
	expect(&msg, "VERSION:", NULL);
	expect(&msg, "STATUS:", jresp);
	expect(&msg, "TYPE:", jresp);
	expect(&msg, "FOLDER:", jresp);

	// BMSG: iOS puts some non-standard stuff here

	while (*msg) {
		if (!vcard && __expect(&msg, "BEGIN:VCARD", NULL, TRUE)) {
			vcard = parse_sender_vcard(&msg, jresp);
			continue;
		}

		// VCARD: any BENV is invalid if comes before the recipient data
		if (!vcard)
			continue;

		if (!benv && __expect(&msg, "BEGIN:BENV", NULL, TRUE)) {
			benv = TRUE;
			continue;
		}

		// BENV: can't be in the message body yet
		if (!benv)
			continue;

		if (!bmsg && __expect(&msg, "BEGIN:MSG", NULL, TRUE)) {
			bmsg = TRUE;
			continue;
		}

		// MSG: not in message yet
		if (!bmsg)
			continue;

		// MSG: Keep looking for END:MSG
		if (!__expect(&msg, "END:MSG", NULL, FALSE)) {
			g_string_append(message, *msg);
			g_string_append(message, "\n");
		}

		// MSG: Keep looking for END:MSG (increment this time)
		if (__expect(&msg, "END:MSG", NULL, TRUE))
			break;
	}

	// BMSG: End of valid message
	expect(&msg, "END:BBODY", NULL);
	expect(&msg, "END:BENV", NULL);
	expect(&msg, "END:BMSG", NULL);

	// remove trailing \n
	message->str[message->len - 1] = '\0';

	json_object_object_add(jresp, "message", json_object_new_string(message->str));
	g_string_free(message, TRUE);

	// If not BMSG then isn't a valid message
	return bmsg;
}

// NOTE: replaces \r with \0 to make it easier to parse
static void sanitize_msg(gchar **msg)
{
	while (*msg) {
		gchar *tmp = *msg++;
		if (g_str_has_suffix(tmp, "\r"))
			*strstr(tmp, "\r") = '\0';
	}
}

json_object *bmessage_parse(const gchar *bmessage)
{
	gchar **msg;
	json_object *jresp = json_object_new_object();
	gboolean ret;

	if (!bmessage || !strlen(bmessage))
		return NULL;

	AFB_INFO("Parsing incoming bMessage of length %lu", strlen(bmessage));

	msg = g_strsplit(bmessage, "\n", -1);
	sanitize_msg(msg);

	ret = __bmessage_parse(msg, jresp);
	if (!ret) {
		json_object_put(jresp);
		jresp = NULL;
	}

	g_strfreev(msg);

	return jresp;
}

#define CRLF		"\r\n"
#define BEGINMSG	"BEGIN:MSG" CRLF
#define ENDMSG		"END:MSG" CRLF

#define body_append(str, value)		g_string_append_printf(str, "%s%s", value, CRLF)

GString *msg2dos(const gchar *data)
{
	gchar **msg, **tmp;
	GString *value;

	if (!data)
		return NULL;

	msg = g_strsplit(data, "\n", -1);
	value = g_string_new(NULL);

	for (tmp = msg; *tmp; tmp++) {
		g_string_append(value, *tmp);

		if (!g_str_has_suffix(*tmp, "\r"))
			g_string_append(value, CRLF);
		else
			g_string_append(value, "\n");
	}

	g_strfreev(msg);

	return value;
}

GString *bmessage_encoder(afb_req_t request)
{
	const gchar *recipient;
	GString *msg, *body = g_string_new(NULL);
	gchar *tmp;

	recipient = afb_req_value(request, "recipient");
	if (!recipient) {
		g_string_free(body, TRUE);
		return NULL;
	}

	msg = msg2dos(afb_req_value(request, "message"));
	if (!msg) {
		g_string_free(body, TRUE);
		return NULL;
	}

	body_append(body, "BEGIN:BMSG");
	body_append(body, "VERSION:1.0");

	// TODO: maybe add support for MMS
	body_append(body, "TYPE:SMS_GSM");

	body_append(body, "FOLDER:telecom/msg/outbox");
	body_append(body, "BEGIN:BENV");

	body_append(body, "BEGIN:VCARD");
	body_append(body, "VERSION:2.1");

	// TODO: get contact information from agl-service-bluetooth-pbap
	tmp = g_strdup_printf("TEL:%s", recipient);
	body_append(body, tmp);
	g_free(tmp);

	body_append(body, "END:VCARD");

	body_append(body, "BEGIN:BBODY");
	body_append(body, "CHARSET:UTF-8");

	tmp = g_strdup_printf("LENGTH:%lu",
		strlen(BEGINMSG) + msg->len + strlen(ENDMSG));

	body_append(body, tmp);
	g_free(tmp);

	g_string_append(body, BEGINMSG);
	g_string_append(body, msg->str);
	g_string_free(msg, TRUE);
	g_string_append(body, ENDMSG);

	body_append(body, "END:BBODY");
	body_append(body, "END:BENV");
	body_append(body, "END:BMSG");

	return body;
}
