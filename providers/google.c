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
#include "../libchatgpt.h"

/* Google Gemini models */
static const char *google_models[] = {
    "gemini-1.5-pro",
    "gemini-1.5-flash",
    "gemini-1.0-pro",
    "gemini-pro-vision",
    NULL
};

/* Format a chat request for Google GenerateContent API */
static JsonObject*
google_format_request(ChatGptBuddy *buddy, const char *message)
{
    JsonObject *request;
    JsonArray *contents;
    JsonObject *content;
    JsonArray *parts;
    JsonObject *part;
    GList *history;
    
    request = json_object_new();
    contents = json_array_new();
    
    /* Add system instruction if configured */
    if (buddy->instructions && *buddy->instructions) {
        JsonObject *system_instruction = json_object_new();
        JsonArray *system_parts = json_array_new();
        JsonObject *system_part = json_object_new();
        
        json_object_set_string_member(system_part, "text", buddy->instructions);
        json_array_add_object_element(system_parts, system_part);
        json_object_set_array_member(system_instruction, "parts", system_parts);
        json_object_set_object_member(request, "systemInstruction", system_instruction);
    }
    
    /* Add conversation history */
    for (history = buddy->history; history != NULL; history = history->next) {
        ChatGptHistory *hist = (ChatGptHistory *)history->data;
        
        content = json_object_new();
        parts = json_array_new();
        part = json_object_new();
        
        /* Map roles: user -> user, assistant -> model */
        const char *role = g_strcmp0(hist->role, "assistant") == 0 ? "model" : "user";
        json_object_set_string_member(content, "role", role);
        json_object_set_string_member(part, "text", hist->content);
        json_array_add_object_element(parts, part);
        json_object_set_array_member(content, "parts", parts);
        json_array_add_object_element(contents, content);
    }
    
    /* Add current message */
    content = json_object_new();
    parts = json_array_new();
    part = json_object_new();
    
    json_object_set_string_member(content, "role", "user");
    json_object_set_string_member(part, "text", message);
    json_array_add_object_element(parts, part);
    json_object_set_array_member(content, "parts", parts);
    json_array_add_object_element(contents, content);
    
    /* Build request */
    json_object_set_array_member(request, "contents", contents);
    
    /* Add generation config */
    JsonObject *generation_config = json_object_new();
    json_object_set_double_member(generation_config, "temperature", 0.7);
    json_object_set_int_member(generation_config, "maxOutputTokens", 4096);
    json_object_set_object_member(request, "generationConfig", generation_config);
    
    return request;
}

/* Parse a response from Google Gemini */
static char*
google_parse_response(JsonObject *response, GError **error)
{
    JsonArray *candidates;
    JsonObject *candidate;
    JsonObject *content;
    JsonArray *parts;
    JsonObject *part;
    const char *text;
    
    if (!json_object_has_member(response, "candidates")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No candidates in response");
        }
        return NULL;
    }
    
    candidates = json_object_get_array_member(response, "candidates");
    if (json_array_get_length(candidates) == 0) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Empty candidates array");
        }
        return NULL;
    }
    
    candidate = json_array_get_object_element(candidates, 0);
    if (!json_object_has_member(candidate, "content")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No content in candidate");
        }
        return NULL;
    }
    
    content = json_object_get_object_member(candidate, "content");
    if (!json_object_has_member(content, "parts")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No parts in content");
        }
        return NULL;
    }
    
    parts = json_object_get_array_member(content, "parts");
    if (json_array_get_length(parts) == 0) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Empty parts array");
        }
        return NULL;
    }
    
    part = json_array_get_object_element(parts, 0);
    if (!json_object_has_member(part, "text")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No text in part");
        }
        return NULL;
    }
    
    text = json_object_get_string_member(part, "text");
    if (text == NULL) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Null text in response");
        }
        return NULL;
    }
    
    return g_strdup(text);
}

/* Get the authentication header for Google (not used, API key goes in URL) */
static const char*
google_get_auth_header(ChatGptAccount *account)
{
    return "";  /* Google uses API key in URL parameter */
}

/* Validate a response from Google */
static gboolean
google_validate_response(JsonObject *response, GError **error)
{
    if (json_object_has_member(response, "error")) {
        JsonObject *error_obj = json_object_get_object_member(response, "error");
        const char *error_msg = json_object_get_string_member(error_obj, "message");
        gint64 error_code = json_object_get_int_member(error_obj, "code");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Google API Error (%ld): %s", 
                                error_code,
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Get the full URL for a chat request (includes API key) */
static char*
google_get_chat_url(LLMProvider *provider, ChatGptBuddy *buddy)
{
    ChatGptAccount *account = purple_connection_get_protocol_data(purple_account_get_connection(buddy->buddy->account));
    const char *api_key = purple_account_get_string(account->account, "api_key", "");
    const char *model = buddy->model ? buddy->model : "gemini-1.5-pro";
    
    return g_strdup_printf("%s/v1beta/models/%s:generateContent?key=%s", 
                          provider->endpoint_url, model, api_key);
}

/* Get additional headers for Google */
static GHashTable*
google_get_additional_headers(ChatGptAccount *account, ChatGptBuddy *buddy)
{
    GHashTable *headers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    g_hash_table_insert(headers, g_strdup("Content-Type"), g_strdup("application/json"));
    
    return headers;
}

/* Parse error response */
static char*
google_parse_error(JsonObject *response)
{
    JsonObject *error_obj;
    const char *error_msg;
    gint64 error_code;
    
    if (!json_object_has_member(response, "error")) {
        return g_strdup("Unknown error");
    }
    
    error_obj = json_object_get_object_member(response, "error");
    error_msg = json_object_get_string_member(error_obj, "message");
    error_code = json_object_get_int_member(error_obj, "code");
    
    if (error_msg) {
        return g_strdup_printf("Error %ld: %s", error_code, error_msg);
    } else {
        return g_strdup_printf("Error %ld: Unknown error", error_code);
    }
}

/* Check if model supports a specific feature */
static gboolean
google_model_supports_feature(const char *model, const char *feature)
{
    if (g_strcmp0(feature, "vision") == 0) {
        /* Gemini Pro Vision and 1.5 models support vision */
        return g_str_has_suffix(model, "vision") || g_str_has_prefix(model, "gemini-1.5");
    }
    
    if (g_strcmp0(feature, "functions") == 0) {
        /* Gemini 1.5 models support function calling */
        return g_str_has_prefix(model, "gemini-1.5");
    }
    
    return FALSE;
}

/* Google provider definition */
static LLMProvider google_provider = {
    .name = "google",
    .display_name = "Google Gemini",
    .endpoint_url = "https://generativelanguage.googleapis.com",
    .chat_endpoint = "/v1beta/models/{model}:generateContent",  /* Template, actual URL built in get_chat_url */
    .models = google_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_GOOGLE,
    .supports_streaming = TRUE,
    .supports_vision = TRUE,
    .supports_functions = TRUE,
    .max_context_length = 1000000,  /* Gemini 1.5 has 1M context window */
    .format_request = google_format_request,
    .parse_response = google_parse_response,
    .get_auth_header = google_get_auth_header,
    .validate_response = google_validate_response,
    .get_chat_url = google_get_chat_url,
    .get_additional_headers = google_get_additional_headers,
    .parse_error = google_parse_error,
    .model_supports_feature = google_model_supports_feature
};

/* Initialize the Google provider */
void
llm_provider_google_init(void)
{
    llm_provider_registry_register(&google_provider);
}

