/*
 * ChatGPT Plugin for libpurple/Pidgin
 * Copyright (c) 2024 Eion Robb
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "libchatgpt.h"
#include "cmds.h"
#include "glib-object.h"
#include "glib.h"
#include "purplecompat.h"
#include <http.h>
#include "markdown.h"

/******************************************************************************/
/* JSON functions */
/******************************************************************************/


gchar *
json_object_to_string(const JsonObject *jsonobj, gsize *length)
{
	JsonGenerator *generator;
	JsonNode *root;
	gchar *string;
	
	root = json_node_new(JSON_NODE_OBJECT);
	json_node_set_object(root, (JsonObject *)jsonobj);
	
	generator = json_generator_new();
	json_generator_set_root(generator, root);
	
	string = json_generator_to_data(generator, length);
	
	g_object_unref(generator);
	json_node_free(root);
	
	return string;
}

JsonNode *
json_decode(const gchar *data, gssize len)
{
	JsonParser *parser = json_parser_new();
	JsonNode *root = NULL;
	
	if (!data || !json_parser_load_from_data(parser, data, len, NULL))
	{
		purple_debug_error("chatgpt", "Error parsing JSON: %s\n", data ? data : "(null)");
	} else {
		root = json_parser_get_root(parser);
		if (root != NULL) {
			root = json_node_copy(root);
		}
	}
	g_object_unref(parser);
	
	return root;
}

JsonObject *
json_string_to_object(const gchar *data, gssize len)
{
	JsonNode *root = json_decode(data, len);
	JsonObject *ret;
	
	g_return_val_if_fail(root, NULL);
	
	if (!JSON_NODE_HOLDS_OBJECT(root)) {
		json_node_free(root);
		return NULL;
	}
	
	ret = json_node_dup_object(root);

	json_node_free(root);

	return ret;
}

/******************************************************************************/

static void
chatgpt_http_request_cb(PurpleHttpConnection *http_conn, PurpleHttpResponse *response, gpointer user_data)
{
	ChatGptApiConnection *conn = user_data;
	const gchar *data;
	gsize len;
	JsonObject *obj;
	
	data = purple_http_response_get_data(response, &len);
	obj = json_string_to_object(data, len);

	if (data == NULL || len == 0) {
		if (conn->error_callback != NULL) {
			conn->error_callback(conn->cga, data, len, conn->user_data);
		} else {
			purple_debug_error("chatgpt", "Error parsing response: %s\n", data);
		}
	} else {
		if (conn->callback != NULL) {
			conn->callback(conn->cga, obj, conn->user_data);
		}
	}

	json_object_unref(obj);
	// purple_http_connection_set_remove(conn->cga->conns, conn->http_conn);
	g_free(conn);
}

static ChatGptApiConnection *
chatgpt_http_request(ChatGptAccount *cga, const gchar *path, const JsonObject *obj, ChatGptCallbackFunc callback, gpointer user_data)
{
	PurpleConnection *pc = cga->pc;
	PurpleAccount *account = cga->account;
	ChatGptApiConnection *conn;
	PurpleHttpRequest *request;
	gchar *url;
	const gchar *openai_token = purple_account_get_string(account, "openai_token", NULL);

	url = g_strdup_printf("https://%s%s", CHATGPT_API_HOST, path);
	
	request = purple_http_request_new(url);
	purple_http_request_set_keepalive_pool(request, cga->keepalive_pool);
	if (obj != NULL) {
		gsize len;
		gchar *body = json_object_to_string(obj, &len);

		purple_http_request_set_method(request, "POST");
		purple_http_request_set_contents(request, body, len);
		purple_http_request_header_set(request, "Content-Type", "application/json");
	}
	purple_http_request_set_max_redirects(request, 0);
	purple_http_request_set_timeout(request, 120);
	purple_http_request_header_set(request, "OpenAI-Beta", "assistants=v2");
	purple_http_request_header_set_printf(request, "Authorization", "Bearer %s", openai_token);
	
	conn = g_new0(ChatGptApiConnection, 1);
	conn->cga = cga;
	conn->user_data = user_data;
	conn->callback = callback;
	
	conn->http_conn = purple_http_request(pc, request, chatgpt_http_request_cb, conn);
	if (conn->http_conn != NULL) {
		purple_http_connection_set_add(cga->conns, conn->http_conn);
	}
	purple_http_request_unref(request);

	g_free(url);
	return conn;
}


/******************************************************************************/
/* ChatGPT functions */
/******************************************************************************/

static void
chatgpt_create_icon_cb(ChatGptAccount *cga, JsonObject *obj, gpointer user_data)
{
	gchar *id = user_data;
	JsonArray *data_arr = json_object_get_array_member(obj, "data");
	JsonObject *data_obj = json_array_get_object_element(data_arr, 0);
	const gchar *b64 = json_object_get_string_member(data_obj, "b64_json");

	if (b64) {
		gsize len;
		guchar *data = g_base64_decode(b64, &len);
		purple_buddy_icons_set_for_user(cga->account, id, data, len, NULL);
	}

	g_free(id);
}

static void
chatgpt_create_icon(ChatGptAccount *cga, const gchar *id, const gchar *instructions)
{
	JsonObject *obj = json_object_new();

	gchar *prompt = g_strdup_printf("An avatar icon for a chatbot. The chatbot is instructed to %s", instructions);

	json_object_set_string_member(obj, "prompt", prompt);
	json_object_set_string_member(obj, "size", "256x256");
	json_object_set_string_member(obj, "model", "dall-e-2");
	json_object_set_string_member(obj, "response_format", "b64_json");
	
	chatgpt_http_request(cga, "/v1/images/generations", obj, chatgpt_create_icon_cb, g_strdup(id));
}

static void
chatgpt_create_thread_cb(ChatGptAccount *cga, JsonObject *obj, gpointer user_data)
{
	gchar *assistant_id = user_data;
	const gchar *thread_id = json_object_get_string_member(obj, "id");

	// save the thread id to the buddy
	PurpleBuddy *buddy = purple_find_buddy(cga->account, assistant_id);
	purple_blist_node_set_string(PURPLE_BLIST_NODE(buddy), "thread_id", thread_id);

	ChatGptBuddy *cbuddy = purple_buddy_get_protocol_data(buddy);
	cbuddy->thread_id = g_strdup(thread_id);

	// mark the buddy as online
	purple_prpl_got_user_status(cga->account, assistant_id, "available", NULL);

	g_free(assistant_id);
}

static void
chatgpt_create_assistant_cb(ChatGptAccount *cga, JsonObject *obj, gpointer user_data)
{
	const gchar *id = json_object_get_string_member(obj, "id");
	const gchar *name = json_object_get_string_member(obj, "name");

	// add to the buddy list
	if (!purple_find_buddy(cga->account, id)) {
		purple_blist_add_buddy(purple_buddy_new(cga->account, id, name), NULL, NULL, NULL);
	}

	// create an icon for the buddy using the image generation api
	if (TRUE) {
		const gchar *instructions = json_object_get_string_member(obj, "instructions");
		chatgpt_create_icon(cga, id, instructions);
	}

	PurpleBuddy *buddy = purple_find_buddy(cga->account, id);
	ChatGptBuddy *cbuddy = g_new0(ChatGptBuddy, 1);
	purple_buddy_set_protocol_data(buddy, cbuddy);
	
	cbuddy->buddy = buddy;
	cbuddy->instructions = g_strdup(json_object_get_string_member(obj, "instructions"));
	cbuddy->name = g_strdup(json_object_get_string_member(obj, "name"));
	cbuddy->description = g_strdup(json_object_get_string_member(obj, "description"));
	cbuddy->model = g_strdup(json_object_get_string_member(obj, "model"));

	JsonObject *thread_obj = json_object_new();
	// create a thread for the assistant
	chatgpt_http_request(cga, "/v1/threads", thread_obj, chatgpt_create_thread_cb, g_strdup(id));
	json_object_unref(thread_obj);
}

static void
chatgpt_create_assistant(ChatGptAccount *cga, const gchar *instructions)
{
	JsonObject *obj = json_object_new();

	//JsonObject *response_format = json_object_new();
	//json_object_set_string_member(response_format, "type", "json_object");

	// TODO add more model options
	json_object_set_string_member(obj, "model", "gpt-4o-mini");
	json_object_set_string_member(obj, "instructions", instructions);
	//json_object_set_object_member(obj, "response_format", response_format);
	
	chatgpt_http_request(cga, "/v1/assistants", obj, chatgpt_create_assistant_cb, cga);
	json_object_unref(obj);
}

static void
chatgpt_send_message_cb(ChatGptAccount *cga, JsonObject *obj, gpointer user_data)
{
	gchar *assistant_id = user_data;
	JsonArray *data_arr = json_object_get_array_member(obj, "data");
	JsonObject *data_obj = json_array_get_object_element(data_arr, 0);
	JsonArray *content = json_object_get_array_member(data_obj, "content");
	JsonObject *content_obj = json_array_get_object_element(content, 0);
	JsonObject *text = json_object_get_object_member(content_obj, "text");
	const gchar *text_value = json_object_get_string_member(text, "value");

	if (text_value == NULL) {
		serv_got_typing_stopped(cga->pc, assistant_id);
		purple_debug_error("chatgpt", "No content found in message\n");
		g_free(assistant_id);
		return;
	}

	// convert markdown to html
	gchar *html = markdown_convert_markdown(text_value, TRUE, FALSE);

	// send the message to the user
	purple_serv_got_im(cga->pc, assistant_id, html, PURPLE_MESSAGE_RECV, time(NULL));

	g_free(assistant_id);
	g_free(html);
}

static void
chatgpt_send_run_cb(ChatGptAccount *cga, JsonObject *obj, gpointer user_data)
{
	gchar *assistant_id = user_data;
	const gchar *run_id = json_object_get_string_member(obj, "id");
	const gchar *thread_id = json_object_get_string_member(obj, "thread_id");
	const gchar *status = json_object_get_string_member(obj, "status");

	if (purple_strequal(status, "completed")) {
		// get the messages
		gchar *url = g_strdup_printf("/v1/threads/%s/messages?run_id=%s", thread_id, run_id);
		chatgpt_http_request(cga, url, NULL, chatgpt_send_message_cb, g_strdup(assistant_id));
		g_free(url);
	} else if (status != NULL) {
		// wait for the run to complete
		purple_debug_info("chatgpt", "Run not completed yet\n");
		gchar *url = g_strdup_printf("/v1/threads/%s/runs/%s", thread_id, run_id);
		chatgpt_http_request(cga, url, NULL, chatgpt_send_run_cb, g_strdup(assistant_id));
		g_free(url);
	}

	g_free(assistant_id);
}

static void
chatgpt_send_message(ChatGptAccount *cga, const gchar *id, const gchar *message)
{
	PurpleBuddy *buddy = purple_find_buddy(cga->account, id);
	if (buddy == NULL) {
		purple_debug_error("chatgpt", "Buddy not found: %s\n", id);
		return;
	}

	const gchar *thread_id = purple_blist_node_get_string(PURPLE_BLIST_NODE(buddy), "thread_id");
	if (thread_id == NULL || thread_id[0] == 0) {
		purple_debug_error("chatgpt", "Thread ID not found for buddy: %s\n", id);
		return;
	}

	gchar *url = g_strdup_printf("/v1/threads/%s/messages", thread_id);
	JsonObject *obj = json_object_new();
	
	json_object_set_string_member(obj, "role", "user");
	json_object_set_string_member(obj, "content", message);
	
	chatgpt_http_request(cga, url, obj, NULL, NULL);

	json_object_unref(obj);
	g_free(url);

	// pretend the bot is typing a response
	purple_serv_got_typing(cga->pc, id, 0, PURPLE_TYPING);

	// start the run
	url = g_strdup_printf("/v1/threads/%s/runs", thread_id);
	obj = json_object_new();

	//JsonObject *response_format = json_object_new();
	//json_object_set_string_member(response_format, "type", "json_object");

	json_object_set_string_member(obj, "assistant_id", id);
	//json_object_set_object_member(obj, "response_format", response_format);
	//json_object_set_string_member(obj, "additional_instructions", "Please respond in JSON format");
	//TODO
	//json_object_set_bool_member(obj, "stream", TRUE);

	chatgpt_http_request(cga, url, obj, chatgpt_send_run_cb, g_strdup(id));

	json_object_unref(obj);
	g_free(url);
}

static void
chatgpt_fetch_assistants_cb(ChatGptAccount *cga, JsonObject *obj, gpointer user_data)
{
	JsonArray *data_arr = json_object_get_array_member(obj, "data");
	guint i, len = json_array_get_length(data_arr);
	
	for (i = 0; i < len; i++) {
		JsonObject *data_obj = json_array_get_object_element(data_arr, i);
		const gchar *id = json_object_get_string_member(data_obj, "id");
		const gchar *name = json_object_get_string_member(data_obj, "name");

		// add to the buddy list
		if (!purple_find_buddy(cga->account, id)) {
			purple_blist_add_buddy(purple_buddy_new(cga->account, id, name), NULL, NULL, NULL);
		}

		PurpleBuddy *buddy = purple_find_buddy(cga->account, id);
		ChatGptBuddy *cbuddy = g_new0(ChatGptBuddy, 1);
		purple_buddy_set_protocol_data(buddy, cbuddy);

		cbuddy->buddy = buddy;
		cbuddy->thread_id = g_strdup(json_object_get_string_member(data_obj, "thread_id"));
		cbuddy->instructions = g_strdup(json_object_get_string_member(data_obj, "instructions"));
		cbuddy->name = g_strdup(json_object_get_string_member(data_obj, "name"));
		cbuddy->description = g_strdup(json_object_get_string_member(data_obj, "description"));
		cbuddy->model = g_strdup(json_object_get_string_member(data_obj, "model"));

		const gchar *thread_id = purple_blist_node_get_string(PURPLE_BLIST_NODE(buddy), "thread_id");
		if (thread_id == NULL || thread_id[0] == 0) {
			JsonObject *thread_obj = json_object_new();
			// create a thread for the assistant
			chatgpt_http_request(cga, "/v1/threads", thread_obj, chatgpt_create_thread_cb, g_strdup(id));
			json_object_unref(thread_obj);
		}

		purple_prpl_got_user_status(cga->account, id, "available", NULL);
	}
}

static void
chatgpt_fetch_assistants(ChatGptAccount *cga)
{
	chatgpt_http_request(cga, "/v1/assistants", NULL, chatgpt_fetch_assistants_cb, NULL);
}


/******************************************************************************/
/* PRPL functions */
/******************************************************************************/

static const char *
chatgpt_list_icon(PurpleAccount *account, PurpleBuddy *buddy)
{
	return "chatgpt";
}

void
chatgpt_tooltip_text(PurpleBuddy *buddy, PurpleNotifyUserInfo *user_info, gboolean full)
{
	ChatGptBuddy *cbuddy = purple_buddy_get_protocol_data(buddy);

	if (cbuddy) {
		gchar *escaped;
#define RENDER_PAIR(key, value) \
		if (value && *value) { \
			escaped = g_markup_printf_escaped("%s", value); \
			purple_notify_user_info_add_pair_html(user_info, key, escaped); \
			g_free(escaped); \
		}

		RENDER_PAIR("Name", cbuddy->name);
		RENDER_PAIR("Model", cbuddy->model);
		RENDER_PAIR("Instructions", cbuddy->instructions);
		RENDER_PAIR("Description", cbuddy->description);

#undef RENDER_PAIR
	}
}

static void
chatgpt_buddy_free(PurpleBuddy *buddy)
{
	ChatGptBuddy *cbuddy = purple_buddy_get_protocol_data(buddy);
	if (cbuddy) {
		g_free(cbuddy->thread_id);
		g_free(cbuddy->instructions);
		g_free(cbuddy->name);
		g_free(cbuddy->description);
		g_free(cbuddy->model);
		g_free(cbuddy);
	}
}

GList *
chatgpt_status_types(PurpleAccount *account)
{
	GList *types = NULL;
	PurpleStatusType *status;
	
	status = purple_status_type_new_full(PURPLE_STATUS_OFFLINE, NULL, NULL, TRUE, TRUE, FALSE);
	types = g_list_append(types, status);
	
	status = purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, "available", NULL, TRUE, TRUE, FALSE);
	types = g_list_append(types, status);
	
	return types;
}

gint
chatgpt_send_im(PurpleConnection *gc, const char *who, const char *message, PurpleMessageFlags flags)
{
	ChatGptAccount *cga = purple_connection_get_protocol_data(gc);
	if (purple_strequal(who, CHATGPT_INSTRUCTOR_ID)) {
		// Treat this as creating a new assistant
		chatgpt_create_assistant(cga, message);	
	} else {
		chatgpt_send_message(cga, who, message);
	}
	
	return 1;
}


void
chatgpt_fake_group_buddy(PurpleConnection *pc, const char *who, const char *old_group, const char *new_group)
{
	// Do nothing to stop the remove+add behaviour
}
void
chatgpt_fake_group_rename(PurpleConnection *pc, const char *old_name, PurpleGroup *group, GList *moved_buddies)
{
	// Do nothing to stop the remove+add behaviour
}

static void
chatgpt_alias_buddy(PurpleConnection *pc, const char *who, const char *alias)
{
	ChatGptAccount *cga = purple_connection_get_protocol_data(pc);
	gchar *url = g_strdup_printf("/v1/assistants/%s", who);
	JsonObject *obj = json_object_new();

	json_object_set_string_member(obj, "name", alias);

	chatgpt_http_request(cga, url, obj, NULL, NULL);

	json_object_unref(obj);
	g_free(url);
}

static void
chatgpt_login(PurpleAccount *account)
{
	PurpleConnection *pc = purple_account_get_connection(account);
	ChatGptAccount *cga = g_new0(ChatGptAccount, 1);
	PurpleConnectionFlags flags;
	
	purple_connection_set_protocol_data(pc, cga);

	flags = purple_connection_get_flags(pc);
	flags |= PURPLE_CONNECTION_FLAG_HTML | PURPLE_CONNECTION_FLAG_NO_BGCOLOR | PURPLE_CONNECTION_FLAG_NO_FONTSIZE;
	purple_connection_set_flags(pc, flags);
	
	cga->account = account;
	cga->pc = pc;
	cga->keepalive_pool = purple_http_keepalive_pool_new();
	cga->conns = purple_http_connection_set_new();
	
	purple_connection_update_progress(pc, "", 1, 1);
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	purple_connection_set_state(pc, PURPLE_CONNECTION_CONNECTED);
#endif

	// add the default buddy to the buddy list if it's not already there
	if (!purple_find_buddy(account, CHATGPT_INSTRUCTOR_ID)) {
		purple_blist_add_buddy(purple_buddy_new(account, CHATGPT_INSTRUCTOR_ID, NULL), NULL, NULL, NULL);
	}

	const gchar *openai_token = purple_account_get_string(account, "openai_token", NULL);
	if (openai_token == NULL || openai_token[0] == 0) {
		purple_notify_message(pc, PURPLE_NOTIFY_MSG_ERROR, "ChatGPT", "You need to set your OpenAI API key in the account settings. Get it from " CHATGPT_API_KEY_URL, NULL, NULL, NULL);
		purple_notify_uri(pc, CHATGPT_API_KEY_URL);
	} else {
		purple_prpl_got_user_status(account, CHATGPT_INSTRUCTOR_ID, "available", NULL);
		purple_serv_got_im(pc, CHATGPT_INSTRUCTOR_ID, "Hello! I'm the ChatGPT plugin for Pidgin. To create a new assistant, send a message to me with the instructions for the assistant you want to create.", PURPLE_MESSAGE_SYSTEM | PURPLE_MESSAGE_RECV, time(NULL));

		chatgpt_fetch_assistants(cga);
	}
}

static void
chatgpt_close(PurpleConnection *pc)
{
	ChatGptAccount *sa;
	GSList *buddies;
	
	g_return_if_fail(pc != NULL);
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	purple_connection_set_state(pc, PURPLE_CONNECTION_DISCONNECTING);
#endif
	
	sa = purple_connection_get_protocol_data(pc);
	g_return_if_fail(sa != NULL);
	
	buddies = purple_blist_find_buddies(sa->account, NULL);
	while (buddies != NULL) {
		PurpleBuddy *buddy = buddies->data;
		chatgpt_buddy_free(buddy);
		purple_buddy_set_protocol_data(buddy, NULL);
		buddies = g_slist_delete_link(buddies, buddies);
	}
	
	purple_debug_info("teams", "destroying incomplete connections\n");

	purple_http_connection_set_destroy(sa->conns);
	sa->conns = NULL;
	purple_http_conn_cancel_all(pc);
	purple_http_keepalive_pool_unref(sa->keepalive_pool);
	
	g_free(sa);
}

gboolean
chatgpt_offline_message(const PurpleBuddy *buddy)
{
	return TRUE;
}

/******************************************************************************/
/* Plugin functions */
/******************************************************************************/

#if PURPLE_VERSION_CHECK(3, 0, 0)
	typedef struct _ChatGptProtocol
	{
		PurpleProtocol parent;
	} ChatGptProtocol;

	typedef struct _ChatGptProtocolClass
	{
		PurpleProtocolClass parent_class;
	} ChatGptProtocolClass;

	G_MODULE_EXPORT GType chatgpt_protocol_get_type(void);
	#define chatgpt_TYPE_PROTOCOL             (chatgpt_protocol_get_type())
	#define chatgpt_PROTOCOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), chatgpt_TYPE_PROTOCOL, ChatGptProtocol))
	#define chatgpt_PROTOCOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), chatgpt_TYPE_PROTOCOL, ChatGptProtocolClass))
	#define chatgpt_IS_PROTOCOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), chatgpt_TYPE_PROTOCOL))
	#define chatgpt_IS_PROTOCOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), chatgpt_TYPE_PROTOCOL))
	#define chatgpt_PROTOCOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), chatgpt_TYPE_PROTOCOL, ChatGptProtocolClass))

	static PurpleProtocol *chatgpt_protocol;
#else
	
// Normally set in core.c in purple3
void _purple_socket_init(void);
void _purple_socket_uninit(void);

#endif


static gboolean
plugin_load(PurplePlugin *plugin
#if PURPLE_VERSION_CHECK(3, 0, 0)
, GError **error
#endif
)
{
	
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	_purple_socket_init();
	purple_http_init();
#endif
	
	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin
#if PURPLE_VERSION_CHECK(3, 0, 0)
, GError **error
#endif
)
{
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	_purple_socket_uninit();
	purple_http_uninit();
#endif
	purple_signals_disconnect_by_handle(plugin);
	
	return TRUE;
}

static GList *
chatgpt_actions(
#if !PURPLE_VERSION_CHECK(3, 0, 0)
PurplePlugin *plugin, gpointer context
#else
PurpleConnection *pc
#endif
)
{
	GList *m = NULL;
	// PurpleProtocolAction *act;

	// act = purple_protocol_action_new(_("Search for Teams Contacts"), chatgpt_search_users);
	// m = g_list_append(m, act);

	return m;
}

#if !PURPLE_VERSION_CHECK(2, 8, 0)
#	define OPT_PROTO_INVITE_MESSAGE 0x00000800
#endif

// Add forwards-compatibility for newer libpurple's when compiling on older ones
typedef struct 
{
	PurplePluginProtocolInfo parent;

	#if !PURPLE_VERSION_CHECK(2, 14, 0)
		char *(*get_cb_alias)(PurpleConnection *gc, int id, const char *who);
		gboolean (*chat_can_receive_file)(PurpleConnection *, int id);
		void (*chat_send_file)(PurpleConnection *, int id, const char *filename);
	#endif
} PurplePluginProtocolInfoExt;

#if !PURPLE_VERSION_CHECK(3, 0, 0)
static void
plugin_init(PurplePlugin *plugin)
{
	PurplePluginInfo *info = g_new0(PurplePluginInfo, 1);
	PurplePluginProtocolInfoExt *prpl_info_ext = g_new0(PurplePluginProtocolInfoExt, 1);
	PurplePluginProtocolInfo *prpl_info = (PurplePluginProtocolInfo *) prpl_info_ext;
#endif

#if PURPLE_VERSION_CHECK(3, 0, 0)
static void 
chatgpt_protocol_init(PurpleProtocol *prpl_info) 
{
	PurpleProtocol *info = prpl_info;
#endif
	PurpleAccountOption *opt;
	PurpleBuddyIconSpec icon_spec = {"jpeg", 0, 0, 96, 96, 0, PURPLE_ICON_SCALE_DISPLAY};

	//PurpleProtocol
	info->id = CHATGPT_PLUGIN_ID;
	info->name = "ChatGPT";
	prpl_info->options = OPT_PROTO_NO_PASSWORD | OPT_PROTO_IM_IMAGE;

#if !PURPLE_VERSION_CHECK(3, 0, 0)
#	define chatgpt_PRPL_APPEND_ACCOUNT_OPTION(opt) prpl_info->protocol_options = g_list_append(prpl_info->protocol_options, (opt));
	prpl_info->icon_spec = icon_spec;
#else
#	define chatgpt_PRPL_APPEND_ACCOUNT_OPTION(opt) prpl_info->account_options = g_list_append(prpl_info->account_options, (opt));
	prpl_info->icon_spec = &icon_spec;
#endif
	
	opt = purple_account_option_string_new(_("OpenAI API Token"), "openai_token", NULL);
	chatgpt_PRPL_APPEND_ACCOUNT_OPTION(opt);

#undef chatgpt_PRPL_APPEND_ACCOUNT_OPTION
	
#if PURPLE_VERSION_CHECK(3, 0, 0)
}

static void 
chatgpt_protocol_class_init(PurpleProtocolClass *prpl_info) 
{
#endif
	//PurpleProtocolClass
	prpl_info->login = chatgpt_login;
	prpl_info->close = chatgpt_close;
	prpl_info->status_types = chatgpt_status_types;
	prpl_info->list_icon = chatgpt_list_icon;
#if PURPLE_VERSION_CHECK(3, 0, 0)
}

static void 
chatgpt_protocol_client_iface_init(PurpleProtocolClientIface *prpl_info) 
{
	PurpleProtocolClientIface *info = prpl_info;
#endif
	
	//PurpleProtocolClientIface
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	info->actions = chatgpt_actions;
#else
	info->get_actions = chatgpt_actions;
#endif
	prpl_info->tooltip_text = chatgpt_tooltip_text;
	prpl_info->normalize = purple_normalize_nocase;
	prpl_info->offline_message = chatgpt_offline_message;
	prpl_info->get_account_text_table = NULL; // chatgpt_get_account_text_table;
#if PURPLE_VERSION_CHECK(3, 0, 0)
}

static void 
chatgpt_protocol_server_iface_init(PurpleProtocolServerIface *prpl_info) 
{
#endif
	
	//PurpleProtocolServerIface
	// prpl_info->get_info = chatgpt_get_info;
	// prpl_info->set_status = chatgpt_set_status;
	prpl_info->group_buddy = chatgpt_fake_group_buddy;
	prpl_info->rename_group = chatgpt_fake_group_rename;
	prpl_info->alias_buddy = chatgpt_alias_buddy;
#if PURPLE_VERSION_CHECK(3, 0, 0)
}

static void 
chatgpt_protocol_im_iface_init(PurpleProtocolIMIface *prpl_info) 
{
#endif
	
	//PurpleProtocolIMIface
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	prpl_info->send_im = chatgpt_send_im;
#else
	prpl_info->send = chatgpt_send_im;
#endif
	// prpl_info->send_typing = chatgpt_send_typing;
#if PURPLE_VERSION_CHECK(3, 0, 0)
}

static void 
chatgpt_protocol_xfer_iface_init(PurpleProtocolXferInterface *prpl_info) 
{
#endif
	
	//PurpleProtocolXferInterface
	// prpl_info->new_xfer = chatgpt_new_xfer;
	// prpl_info->send_file = chatgpt_send_file;
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	// prpl_info->can_receive_file = chatgpt_can_receive_file;
#else
	// prpl_info->can_receive = chatgpt_can_receive_file;
#endif
	#if PURPLE_VERSION_CHECK(2, 14, 0)
		// prpl_info->chat_send_file = chatgpt_chat_send_file;
		// prpl_info->chat_can_receive_file = chatgpt_chat_can_receive_file;
	#else
		// prpl_info_ext->chat_send_file = chatgpt_chat_send_file;
		// prpl_info_ext->chat_can_receive_file = chatgpt_chat_can_receive_file;
	#endif
	
#if PURPLE_VERSION_CHECK(3, 0, 0)
}

static void 
chatgpt_protocol_roomlist_iface_init(PurpleProtocolRoomlistIface *prpl_info) 
{
#endif
	
	//PurpleProtocolRoomlistIface
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	// prpl_info->roomlist_get_list = chatgpt_roomlist_get_list;
#else
	// prpl_info->get_list = chatgpt_roomlist_get_list;
#endif
	
#if !PURPLE_VERSION_CHECK(3, 0, 0)
	// Plugin info
	info->magic = PURPLE_PLUGIN_MAGIC;
	info->major_version = 2;
	info->minor_version = MIN(PURPLE_MINOR_VERSION, 8);
	info->type = PURPLE_PLUGIN_PROTOCOL;
	info->priority = PURPLE_PRIORITY_DEFAULT;
	info->version = CHATGPT_PLUGIN_VERSION;
	info->summary = N_("ChatGPT Protocol Plugin");
	info->description = N_("ChatGPT Protocol Plugin");
	info->author = "Eion Robb <eionrobb@gmail.com>";
	info->homepage = "http://github.com/EionRobb/pidgin-chatgpt";
	info->load = plugin_load;
	info->unload = plugin_unload;
	info->extra_info = prpl_info;
	
	// Protocol info
	#if PURPLE_MINOR_VERSION >= 5
		prpl_info->struct_size = sizeof(PurplePluginProtocolInfoExt);
	#endif
	#if PURPLE_MINOR_VERSION >= 8
		// prpl_info->add_buddy_with_invite = chatgpt_add_buddy_with_invite;
	#endif
	
	plugin->info = info;

#endif
	
}

#if PURPLE_VERSION_CHECK(3, 0, 0)


PURPLE_DEFINE_TYPE_EXTENDED(
	ChatGptProtocol, chatgpt_protocol, PURPLE_TYPE_PROTOCOL, 0,

	PURPLE_IMPLEMENT_INTERFACE_STATIC(PURPLE_TYPE_PROTOCOL_CLIENT_IFACE,
	                                  chatgpt_protocol_client_iface_init)

	PURPLE_IMPLEMENT_INTERFACE_STATIC(PURPLE_TYPE_PROTOCOL_SERVER_IFACE,
	                                  chatgpt_protocol_server_iface_init)

	PURPLE_IMPLEMENT_INTERFACE_STATIC(PURPLE_TYPE_PROTOCOL_IM_IFACE,
	                                  chatgpt_protocol_im_iface_init)

	PURPLE_IMPLEMENT_INTERFACE_STATIC(PURPLE_TYPE_PROTOCOL_CHAT_IFACE,
	                                  chatgpt_protocol_chat_iface_init)

	PURPLE_IMPLEMENT_INTERFACE_STATIC(PURPLE_TYPE_PROTOCOL_PRIVACY_IFACE,
	                                  chatgpt_protocol_privacy_iface_init)

	PURPLE_IMPLEMENT_INTERFACE_STATIC(PURPLE_TYPE_PROTOCOL_ROOMLIST_IFACE,
	                                  chatgpt_protocol_roomlist_iface_init)

	PURPLE_IMPLEMENT_INTERFACE_STATIC(PURPLE_TYPE_PROTOCOL_XFER,
	                                  chatgpt_protocol_xfer_iface_init)
);

static gboolean
libpurple3_plugin_load(PurplePlugin *plugin, GError **error)
{
	chatgpt_protocol_register_type(plugin);
	chatgpt_protocol = purple_protocols_add(PURPLE_TYPE_PROTOCOL, error);
	if (!chatgpt_protocol)
		return FALSE;
	
	return plugin_load(plugin, error);
}

static gboolean
libpurple3_plugin_unload(PurplePlugin *plugin, GError **error)
{
	if (!plugin_unload(plugin, error))
		return FALSE;

	if (!purple_protocols_remove(chatgpt_protocol, error))
		return FALSE;

	return TRUE;
}

static PurplePluginInfo *
plugin_query(GError **error)
{
	return purple_plugin_info_new(
		"id",           chatgpt_PLUGIN_ID,
		"name",         "ChatGPT Protocol",
		"version",      chatgpt_PLUGIN_VERSION,
		"category",     N_("Protocol"),
		"summary",      N_("ChatGPT Protocol Plugin"),
		"description",  N_("ChatGPT Protocol Plugin"),
		"website",      "http://github.com/EionRobb/pidgin-chatgpt",
		"abi-version",  PURPLE_ABI_VERSION,
		"flags",        PURPLE_PLUGIN_INFO_FLAGS_INTERNAL |
		                PURPLE_PLUGIN_INFO_FLAGS_AUTO_LOAD,
		NULL
	);
}


PURPLE_PLUGIN_INIT(chatgpt, plugin_query, libpurple3_plugin_load, libpurple3_plugin_unload);
#else
	
static PurplePluginInfo aLovelyBunchOfCoconuts;
PURPLE_INIT_PLUGIN(chatgpt, plugin_init, aLovelyBunchOfCoconuts);
#endif

