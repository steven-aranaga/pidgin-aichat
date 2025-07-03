/*
 * pidgin-aichat
 *
 * Copyright (C) 2025
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include <glib.h>
#include <json-glib/json-glib.h>
#include "../providers.h"
#include "../provider_registry.h"
#include "../libaichat.h"

/* Cohere models */
static const char *cohere_models[] = {
    "command-r-plus",
    "command-r",
    "command",
    "command-nightly",
    "command-light",
    "command-light-nightly",
    NULL
};

/* Format a chat request for Cohere Chat API */
static JsonObject*
cohere_format_request(AiChatBuddy *buddy, const char *message)
{
    JsonObject *request;
    JsonArray *chat_history;
    JsonObject *chat_msg;
    GList *history;
    
    request = json_object_new();
    chat_history = json_array_new();
    
    /* Add conversation history in Cohere format */
    for (history = buddy->history; history != NULL; history = history->next) {
        AiChatHistory *hist = (AiChatHistory *)history->data;
        chat_msg = json_object_new();
        
        /* Map roles: user -> USER, assistant -> CHATBOT */
        const char *role = g_strcmp0(hist->role, "assistant") == 0 ? "CHATBOT" : "USER";
        json_object_set_string_member(chat_msg, "role", role);
        json_object_set_string_member(chat_msg, "message", hist->content);
        json_array_add_object_element(chat_history, chat_msg);
    }
    
    /* Build request according to Cohere Chat API format */
    json_object_set_string_member(request, "model", buddy->model ? buddy->model : "command-r");
    json_object_set_string_member(request, "message", message);
    json_object_set_array_member(request, "chat_history", chat_history);
    
    /* Add system message if configured (called "preamble" in Cohere) */
    if (buddy->instructions && *buddy->instructions) {
        json_object_set_string_member(request, "preamble", buddy->instructions);
    }
    
    /* Add generation parameters */
    json_object_set_double_member(request, "temperature", 0.7);
    json_object_set_int_member(request, "max_tokens", 4096);
    
    return request;
}

/* Parse a response from Cohere */
static char*
cohere_parse_response(JsonObject *response, GError **error)
{
    const char *text;
    
    if (!json_object_has_member(response, "text")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No text in response");
        }
        return NULL;
    }
    
    text = json_object_get_string_member(response, "text");
    if (text == NULL) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Null text in response");
        }
        return NULL;
    }
    
    return g_strdup(text);
}

/* Get the authentication header for Cohere */
static const char*
cohere_get_auth_header(AiChatAccount *account)
{
    static char auth_header[512];
    const char *api_key;
    
    api_key = purple_account_get_string(account->account, "api_key", "");
    g_snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    
    return auth_header;
}

/* Validate a response from Cohere */
static gboolean
cohere_validate_response(JsonObject *response, GError **error)
{
    if (json_object_has_member(response, "message")) {
        const char *error_msg = json_object_get_string_member(response, "message");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Cohere API Error: %s", 
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Get the full URL for a chat request */
static char*
cohere_get_chat_url(LLMProvider *provider, AiChatBuddy *buddy)
{
    return g_strdup_printf("%s%s", provider->endpoint_url, provider->chat_endpoint);
}

/* Get additional headers for Cohere */
static GHashTable*
cohere_get_additional_headers(AiChatAccount *account, AiChatBuddy *buddy)
{
    GHashTable *headers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const char *api_key = purple_account_get_string(account->account, "api_key", "");
    char *auth_header = g_strdup_printf("Bearer %s", api_key);
    
    g_hash_table_insert(headers, g_strdup("Content-Type"), g_strdup("application/json"));
    g_hash_table_insert(headers, g_strdup("Authorization"), auth_header);
    
    return headers;
}

/* Parse error response */
static char*
cohere_parse_error(JsonObject *response)
{
    const char *error_msg;
    
    if (!json_object_has_member(response, "message")) {
        return g_strdup("Unknown error");
    }
    
    error_msg = json_object_get_string_member(response, "message");
    
    if (error_msg) {
        return g_strdup(error_msg);
    } else {
        return g_strdup("Unknown error");
    }
}

/* Check if model supports a specific feature */
static gboolean
cohere_model_supports_feature(const char *model, const char *feature)
{
    if (g_strcmp0(feature, "vision") == 0) {
        /* Cohere models don't support vision yet */
        return FALSE;
    }
    
    if (g_strcmp0(feature, "functions") == 0) {
        /* Command-R models support function calling */
        return g_str_has_prefix(model, "command-r");
    }
    
    return FALSE;
}

/* Cohere provider definition */
static LLMProvider cohere_provider = {
    .name = "cohere",
    .display_name = "Cohere",
    .endpoint_url = "https://api.cohere.ai",
    .chat_endpoint = "/v1/chat",
    .models = cohere_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_COHERE,
    .supports_streaming = TRUE,
    .supports_vision = FALSE,
    .supports_functions = TRUE,
    .max_context_length = 128000,  /* Command-R models have 128k context */
    .format_request = cohere_format_request,
    .parse_response = cohere_parse_response,
    .get_auth_header = cohere_get_auth_header,
    .validate_response = cohere_validate_response,
    .get_chat_url = cohere_get_chat_url,
    .get_additional_headers = cohere_get_additional_headers,
    .parse_error = cohere_parse_error,
    .model_supports_feature = cohere_model_supports_feature
};

/* Initialize the Cohere provider */
void
llm_provider_cohere_init(void)
{
    llm_provider_registry_register(&cohere_provider);
}

