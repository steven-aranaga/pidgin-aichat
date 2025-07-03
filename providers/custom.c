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

/* Default models for custom provider (user can override) */
static const char *custom_models[] = {
    "gpt-3.5-turbo",
    "gpt-4",
    "claude-3-sonnet",
    "llama-2-7b",
    "llama-2-13b",
    "mistral-7b",
    "custom-model-1",
    "custom-model-2",
    NULL
};

/* Format a chat request for Custom provider (assumes OpenAI format by default) */
static JsonObject*
custom_format_request(AiChatBuddy *buddy, const char *message)
{
    JsonObject *request;
    JsonArray *messages;
    JsonObject *msg;
    GList *history;
    
    request = json_object_new();
    messages = json_array_new();
    
    /* Add system message if configured */
    if (buddy->instructions && *buddy->instructions) {
        msg = json_object_new();
        json_object_set_string_member(msg, "role", "system");
        json_object_set_string_member(msg, "content", buddy->instructions);
        json_array_add_object_element(messages, msg);
    }
    
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
    
    /* Build request (OpenAI format by default) */
    json_object_set_string_member(request, "model", buddy->model ? buddy->model : "gpt-3.5-turbo");
    json_object_set_array_member(request, "messages", messages);
    json_object_set_double_member(request, "temperature", 0.7);
    
    return request;
}

/* Parse a response from Custom provider (assumes OpenAI format by default) */
static char*
custom_parse_response(JsonObject *response, GError **error)
{
    JsonArray *choices;
    JsonObject *choice;
    JsonObject *message;
    const char *content;
    
    /* Try OpenAI format first */
    if (json_object_has_member(response, "choices")) {
        choices = json_object_get_array_member(response, "choices");
        if (json_array_get_length(choices) > 0) {
            choice = json_array_get_object_element(choices, 0);
            message = json_object_get_object_member(choice, "message");
            content = json_object_get_string_member(message, "content");
            
            if (content != NULL) {
                return g_strdup(content);
            }
        }
    }
    
    /* Try simple text response */
    if (json_object_has_member(response, "text")) {
        content = json_object_get_string_member(response, "text");
        if (content != NULL) {
            return g_strdup(content);
        }
    }
    
    /* Try response field */
    if (json_object_has_member(response, "response")) {
        content = json_object_get_string_member(response, "response");
        if (content != NULL) {
            return g_strdup(content);
        }
    }
    
    /* Try message field */
    if (json_object_has_member(response, "message")) {
        JsonObject *msg_obj = json_object_get_object_member(response, "message");
        if (msg_obj && json_object_has_member(msg_obj, "content")) {
            content = json_object_get_string_member(msg_obj, "content");
            if (content != NULL) {
                return g_strdup(content);
            }
        }
    }
    
    if (error) {
        *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "No recognizable content in response");
    }
    return NULL;
}

/* Get the authentication header for Custom provider */
static const char*
custom_get_auth_header(AiChatAccount *account)
{
    static char auth_header[512];
    const char *api_key;
    const char *auth_method;
    
    api_key = purple_account_get_string(account->account, "api_key", "");
    auth_method = purple_account_get_string(account->account, "custom_auth_method", "bearer");
    
    if (g_strcmp0(auth_method, "bearer") == 0) {
        g_snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    } else if (g_strcmp0(auth_method, "api_key") == 0) {
        g_snprintf(auth_header, sizeof(auth_header), "%s", api_key);
    } else {
        g_snprintf(auth_header, sizeof(auth_header), "%s", api_key);
    }
    
    return auth_header;
}

/* Validate a response from Custom provider */
static gboolean
custom_validate_response(JsonObject *response, GError **error)
{
    /* Check for common error formats */
    if (json_object_has_member(response, "error")) {
        JsonObject *error_obj = json_object_get_object_member(response, "error");
        const char *error_msg = json_object_get_string_member(error_obj, "message");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Custom API Error: %s", 
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    if (json_object_has_member(response, "detail")) {
        const char *error_msg = json_object_get_string_member(response, "detail");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Custom API Error: %s", 
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Get the full URL for a chat request */
static char*
custom_get_chat_url(LLMProvider *provider, AiChatBuddy *buddy)
{
    AiChatAccount *account = purple_connection_get_protocol_data(purple_account_get_connection(buddy->buddy->account));
    const char *custom_endpoint = purple_account_get_string(account->account, "custom_endpoint", "");
    const char *custom_path = purple_account_get_string(account->account, "custom_chat_path", "/v1/chat/completions");
    
    /* Use custom endpoint if specified */
    if (custom_endpoint && *custom_endpoint) {
        return g_strdup_printf("%s%s", custom_endpoint, custom_path);
    } else {
        return g_strdup_printf("%s%s", provider->endpoint_url, provider->chat_endpoint);
    }
}

/* Get additional headers for Custom provider */
static GHashTable*
custom_get_additional_headers(AiChatAccount *account, AiChatBuddy *buddy)
{
    GHashTable *headers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const char *api_key = purple_account_get_string(account->account, "api_key", "");
    const char *auth_method = purple_account_get_string(account->account, "custom_auth_method", "bearer");
    const char *auth_header_name = purple_account_get_string(account->account, "custom_auth_header", "Authorization");
    
    g_hash_table_insert(headers, g_strdup("Content-Type"), g_strdup("application/json"));
    
    /* Add authentication header based on method */
    if (api_key && *api_key) {
        if (g_strcmp0(auth_method, "bearer") == 0) {
            char *auth_value = g_strdup_printf("Bearer %s", api_key);
            g_hash_table_insert(headers, g_strdup(auth_header_name), auth_value);
        } else if (g_strcmp0(auth_method, "api_key") == 0) {
            g_hash_table_insert(headers, g_strdup(auth_header_name), g_strdup(api_key));
        } else if (g_strcmp0(auth_method, "custom") == 0) {
            const char *custom_auth_value = purple_account_get_string(account->account, "custom_auth_value", "");
            if (custom_auth_value && *custom_auth_value) {
                g_hash_table_insert(headers, g_strdup(auth_header_name), g_strdup(custom_auth_value));
            }
        }
    }
    
    return headers;
}

/* Parse error response */
static char*
custom_parse_error(JsonObject *response)
{
    JsonObject *error_obj;
    const char *error_msg;
    
    /* Try OpenAI-style error */
    if (json_object_has_member(response, "error")) {
        error_obj = json_object_get_object_member(response, "error");
        error_msg = json_object_get_string_member(error_obj, "message");
        if (error_msg) {
            return g_strdup(error_msg);
        }
    }
    
    /* Try FastAPI-style error */
    if (json_object_has_member(response, "detail")) {
        error_msg = json_object_get_string_member(response, "detail");
        if (error_msg) {
            return g_strdup(error_msg);
        }
    }
    
    /* Try simple message */
    if (json_object_has_member(response, "message")) {
        error_msg = json_object_get_string_member(response, "message");
        if (error_msg) {
            return g_strdup(error_msg);
        }
    }
    
    return g_strdup("Unknown error");
}

/* Check if model supports a specific feature */
static gboolean
custom_model_supports_feature(const char *model, const char *feature)
{
    /* For custom providers, we can't know for sure, so assume basic support */
    if (g_strcmp0(feature, "functions") == 0) {
        return TRUE;  /* Assume function calling is supported */
    }
    
    if (g_strcmp0(feature, "vision") == 0) {
        /* Only assume vision if model name suggests it */
        return g_strstr_len(model, -1, "vision") != NULL ||
               g_strstr_len(model, -1, "gpt-4") != NULL;
    }
    
    return FALSE;
}

/* Custom provider definition */
static LLMProvider custom_provider = {
    .name = "custom",
    .display_name = "Custom Endpoint",
    .endpoint_url = "https://api.example.com",  /* Default, user can override */
    .chat_endpoint = "/v1/chat/completions",
    .models = custom_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,  /* Could be local or remote */
    .api_format = API_FORMAT_OPENAI,  /* Assume OpenAI format by default */
    .supports_streaming = TRUE,
    .supports_vision = TRUE,
    .supports_functions = TRUE,
    .max_context_length = 32768,  /* Conservative default */
    .format_request = custom_format_request,
    .parse_response = custom_parse_response,
    .get_auth_header = custom_get_auth_header,
    .validate_response = custom_validate_response,
    .get_chat_url = custom_get_chat_url,
    .get_additional_headers = custom_get_additional_headers,
    .parse_error = custom_parse_error,
    .model_supports_feature = custom_model_supports_feature
};

/* Initialize the Custom provider */
void
llm_provider_custom_init(void)
{
    llm_provider_registry_register(&custom_provider);
}

