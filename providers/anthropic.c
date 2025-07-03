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

/* Anthropic Claude models */
static const char *anthropic_models[] = {
    "claude-3-5-sonnet-20241022",
    "claude-3-5-haiku-20241022",
    "claude-3-opus-20240229",
    "claude-3-sonnet-20240229",
    "claude-3-haiku-20240307",
    NULL
};

/* Format a chat request for Anthropic Messages API */
static JsonObject*
anthropic_format_request(AiChatBuddy *buddy, const char *message)
{
    JsonObject *request;
    JsonArray *messages;
    JsonObject *msg;
    GList *history;
    
    request = json_object_new();
    messages = json_array_new();
    
    /* Add conversation history */
    for (history = buddy->history; history != NULL; history = history->next) {
        AiChatHistory *hist = (AiChatHistory *)history->data;
        msg = json_object_new();
        json_object_set_string_member(msg, "role", hist->role);
        json_object_set_string_member(msg, "content", hist->content);
        json_array_add_object_element(messages, msg);
    }
    
    /* Add current message */
    msg = json_object_new();
    json_object_set_string_member(msg, "role", "user");
    json_object_set_string_member(msg, "content", message);
    json_array_add_object_element(messages, msg);
    
    /* Build request according to Anthropic Messages API format */
    json_object_set_string_member(request, "model", buddy->model ? buddy->model : "claude-3-5-sonnet-20241022");
    json_object_set_int_member(request, "max_tokens", 4096);
    json_object_set_array_member(request, "messages", messages);
    
    /* Add system message if configured */
    if (buddy->instructions && *buddy->instructions) {
        json_object_set_string_member(request, "system", buddy->instructions);
    }
    
    return request;
}

/* Parse a response from Anthropic */
static char*
anthropic_parse_response(JsonObject *response, GError **error)
{
    JsonArray *content;
    JsonObject *content_block;
    const char *text;
    
    if (!json_object_has_member(response, "content")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No content in response");
        }
        return NULL;
    }
    
    content = json_object_get_array_member(response, "content");
    if (json_array_get_length(content) == 0) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Empty content array");
        }
        return NULL;
    }
    
    content_block = json_array_get_object_element(content, 0);
    if (!json_object_has_member(content_block, "text")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No text in content block");
        }
        return NULL;
    }
    
    text = json_object_get_string_member(content_block, "text");
    if (text == NULL) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Null text in response");
        }
        return NULL;
    }
    
    return g_strdup(text);
}

/* Get the authentication header for Anthropic */
static const char*
anthropic_get_auth_header(AiChatAccount *account)
{
    static char auth_header[512];
    const char *api_key;
    
    api_key = purple_account_get_string(account->account, "api_key", "");
    g_snprintf(auth_header, sizeof(auth_header), "%s", api_key);
    
    return auth_header;
}

/* Validate a response from Anthropic */
static gboolean
anthropic_validate_response(JsonObject *response, GError **error)
{
    if (json_object_has_member(response, "error")) {
        JsonObject *error_obj = json_object_get_object_member(response, "error");
        const char *error_msg = json_object_get_string_member(error_obj, "message");
        const char *error_type = json_object_get_string_member(error_obj, "type");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Anthropic API Error (%s): %s", 
                                error_type ? error_type : "unknown",
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Get the full URL for a chat request */
static char*
anthropic_get_chat_url(LLMProvider *provider, AiChatBuddy *buddy)
{
    return g_strdup_printf("%s%s", provider->endpoint_url, provider->chat_endpoint);
}

/* Get additional headers for Anthropic */
static GHashTable*
anthropic_get_additional_headers(AiChatAccount *account, AiChatBuddy *buddy)
{
    GHashTable *headers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    g_hash_table_insert(headers, g_strdup("Content-Type"), g_strdup("application/json"));
    g_hash_table_insert(headers, g_strdup("x-api-key"), g_strdup(anthropic_get_auth_header(account)));
    g_hash_table_insert(headers, g_strdup("anthropic-version"), g_strdup("2023-06-01"));
    
    return headers;
}

/* Parse error response */
static char*
anthropic_parse_error(JsonObject *response)
{
    JsonObject *error_obj;
    const char *error_msg;
    const char *error_type;
    
    if (!json_object_has_member(response, "error")) {
        return g_strdup("Unknown error");
    }
    
    error_obj = json_object_get_object_member(response, "error");
    error_msg = json_object_get_string_member(error_obj, "message");
    error_type = json_object_get_string_member(error_obj, "type");
    
    if (error_type && error_msg) {
        return g_strdup_printf("%s: %s", error_type, error_msg);
    } else if (error_msg) {
        return g_strdup(error_msg);
    } else {
        return g_strdup("Unknown error");
    }
}

/* Check if model supports a specific feature */
static gboolean
anthropic_model_supports_feature(const char *model, const char *feature)
{
    if (g_strcmp0(feature, "vision") == 0) {
        /* Claude 3 models support vision */
        return g_str_has_prefix(model, "claude-3");
    }
    
    if (g_strcmp0(feature, "functions") == 0) {
        /* Claude 3 models support function calling */
        return g_str_has_prefix(model, "claude-3");
    }
    
    return FALSE;
}

/* Anthropic provider definition */
static LLMProvider anthropic_provider = {
    .name = "anthropic",
    .display_name = "Anthropic",
    .endpoint_url = "https://api.anthropic.com",
    .chat_endpoint = "/v1/messages",
    .models = anthropic_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_ANTHROPIC,
    .supports_streaming = TRUE,
    .supports_vision = TRUE,
    .supports_functions = TRUE,
    .max_context_length = 200000,  /* Claude 3 has 200k context window */
    .format_request = anthropic_format_request,
    .parse_response = anthropic_parse_response,
    .get_auth_header = anthropic_get_auth_header,
    .validate_response = anthropic_validate_response,
    .get_chat_url = anthropic_get_chat_url,
    .get_additional_headers = anthropic_get_additional_headers,
    .parse_error = anthropic_parse_error,
    .model_supports_feature = anthropic_model_supports_feature
};

/* Initialize the Anthropic provider */
void
llm_provider_anthropic_init(void)
{
    llm_provider_registry_register(&anthropic_provider);
}

