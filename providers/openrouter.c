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

/* OpenRouter popular models */
static const char *openrouter_models[] = {
    "openai/gpt-4-turbo",
    "openai/gpt-4",
    "openai/gpt-3.5-turbo",
    "anthropic/claude-3-5-sonnet",
    "anthropic/claude-3-opus",
    "anthropic/claude-3-haiku",
    "google/gemini-pro-1.5",
    "google/gemini-pro",
    "meta-llama/llama-3.1-70b-instruct",
    "meta-llama/llama-3.1-8b-instruct",
    "mistralai/mixtral-8x7b-instruct",
    "mistralai/mistral-7b-instruct",
    "cohere/command-r-plus",
    "cohere/command-r",
    "qwen/qwen-2.5-72b-instruct",
    "deepseek/deepseek-chat",
    NULL
};

/* Format a chat request for OpenRouter (uses OpenAI format) */
static JsonObject*
openrouter_format_request(ChatGptBuddy *buddy, const char *message)
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
        ChatGptHistory *hist = (ChatGptHistory *)history->data;
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
    
    /* Build request */
    json_object_set_string_member(request, "model", buddy->model ? buddy->model : "openai/gpt-3.5-turbo");
    json_object_set_array_member(request, "messages", messages);
    json_object_set_double_member(request, "temperature", 0.7);
    
    return request;
}

/* Parse a response from OpenRouter (uses OpenAI format) */
static char*
openrouter_parse_response(JsonObject *response, GError **error)
{
    JsonArray *choices;
    JsonObject *choice;
    JsonObject *message;
    const char *content;
    
    if (!json_object_has_member(response, "choices")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No choices in response");
        }
        return NULL;
    }
    
    choices = json_object_get_array_member(response, "choices");
    if (json_array_get_length(choices) == 0) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Empty choices array");
        }
        return NULL;
    }
    
    choice = json_array_get_object_element(choices, 0);
    message = json_object_get_object_member(choice, "message");
    content = json_object_get_string_member(message, "content");
    
    if (content == NULL) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No content in response");
        }
        return NULL;
    }
    
    return g_strdup(content);
}

/* Get the authentication header for OpenRouter */
static const char*
openrouter_get_auth_header(ChatGptAccount *account)
{
    static char auth_header[512];
    const char *api_key;
    
    api_key = purple_account_get_string(account->account, "api_key", "");
    g_snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    
    return auth_header;
}

/* Validate a response from OpenRouter */
static gboolean
openrouter_validate_response(JsonObject *response, GError **error)
{
    if (json_object_has_member(response, "error")) {
        JsonObject *error_obj = json_object_get_object_member(response, "error");
        const char *error_msg = json_object_get_string_member(error_obj, "message");
        const char *error_type = json_object_get_string_member(error_obj, "type");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "OpenRouter API Error (%s): %s", 
                                error_type ? error_type : "unknown",
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Get the full URL for a chat request */
static char*
openrouter_get_chat_url(LLMProvider *provider, ChatGptBuddy *buddy)
{
    return g_strdup_printf("%s%s", provider->endpoint_url, provider->chat_endpoint);
}

/* Get additional headers for OpenRouter */
static GHashTable*
openrouter_get_additional_headers(ChatGptAccount *account, ChatGptBuddy *buddy)
{
    GHashTable *headers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    const char *api_key = purple_account_get_string(account->account, "api_key", "");
    char *auth_header = g_strdup_printf("Bearer %s", api_key);
    
    g_hash_table_insert(headers, g_strdup("Content-Type"), g_strdup("application/json"));
    g_hash_table_insert(headers, g_strdup("Authorization"), auth_header);
    
    /* OpenRouter-specific headers */
    g_hash_table_insert(headers, g_strdup("HTTP-Referer"), g_strdup("https://github.com/steven-aranaga/pidgin-aichat"));
    g_hash_table_insert(headers, g_strdup("X-Title"), g_strdup("Pidgin AI Chat"));
    
    return headers;
}

/* Parse error response */
static char*
openrouter_parse_error(JsonObject *response)
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
openrouter_model_supports_feature(const char *model, const char *feature)
{
    if (g_strcmp0(feature, "vision") == 0) {
        /* Vision models on OpenRouter */
        return g_strstr_len(model, -1, "gpt-4") != NULL ||
               g_strstr_len(model, -1, "claude-3") != NULL ||
               g_strstr_len(model, -1, "gemini-pro") != NULL;
    }
    
    if (g_strcmp0(feature, "functions") == 0) {
        /* Most models on OpenRouter support function calling */
        return TRUE;
    }
    
    return FALSE;
}

/* OpenRouter provider definition */
static LLMProvider openrouter_provider = {
    .name = "openrouter",
    .display_name = "OpenRouter",
    .endpoint_url = "https://openrouter.ai",
    .chat_endpoint = "/api/v1/chat/completions",
    .models = openrouter_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = TRUE,
    .supports_functions = TRUE,
    .max_context_length = 128000,  /* Varies by model, using conservative estimate */
    .format_request = openrouter_format_request,
    .parse_response = openrouter_parse_response,
    .get_auth_header = openrouter_get_auth_header,
    .validate_response = openrouter_validate_response,
    .get_chat_url = openrouter_get_chat_url,
    .get_additional_headers = openrouter_get_additional_headers,
    .parse_error = openrouter_parse_error,
    .model_supports_feature = openrouter_model_supports_feature
};

/* Initialize the OpenRouter provider */
void
llm_provider_openrouter_init(void)
{
    llm_provider_registry_register(&openrouter_provider);
}
