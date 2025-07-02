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

/* Popular Ollama models (these would be discovered dynamically in a real implementation) */
static const char *ollama_models[] = {
    "llama3.1:70b",
    "llama3.1:8b",
    "llama3.1:latest",
    "llama3:70b",
    "llama3:8b",
    "llama3:latest",
    "mistral:7b",
    "mistral:latest",
    "mixtral:8x7b",
    "mixtral:latest",
    "codellama:13b",
    "codellama:7b",
    "codellama:latest",
    "phi3:14b",
    "phi3:3.8b",
    "phi3:latest",
    "gemma2:27b",
    "gemma2:9b",
    "gemma2:2b",
    "qwen2.5:72b",
    "qwen2.5:14b",
    "qwen2.5:7b",
    "deepseek-coder:33b",
    "deepseek-coder:6.7b",
    "deepseek-coder:latest",
    NULL
};

/* Format a chat request for Ollama Chat API */
static JsonObject*
ollama_format_request(ChatGptBuddy *buddy, const char *message)
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
    
    /* Build request according to Ollama Chat API format */
    json_object_set_string_member(request, "model", buddy->model ? buddy->model : "llama3.1:latest");
    json_object_set_array_member(request, "messages", messages);
    json_object_set_boolean_member(request, "stream", FALSE);  /* Disable streaming for now */
    
    /* Add generation options */
    JsonObject *options = json_object_new();
    json_object_set_double_member(options, "temperature", 0.7);
    json_object_set_int_member(options, "num_predict", 4096);
    json_object_set_object_member(request, "options", options);
    
    return request;
}

/* Parse a response from Ollama */
static char*
ollama_parse_response(JsonObject *response, GError **error)
{
    JsonObject *message;
    const char *content;
    
    if (!json_object_has_member(response, "message")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No message in response");
        }
        return NULL;
    }
    
    message = json_object_get_object_member(response, "message");
    if (!json_object_has_member(message, "content")) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "No content in message");
        }
        return NULL;
    }
    
    content = json_object_get_string_member(message, "content");
    if (content == NULL) {
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                                "Null content in response");
        }
        return NULL;
    }
    
    return g_strdup(content);
}

/* Get the authentication header for Ollama (none needed for local) */
static const char*
ollama_get_auth_header(ChatGptAccount *account)
{
    return "";  /* Ollama doesn't require authentication for local instances */
}

/* Validate a response from Ollama */
static gboolean
ollama_validate_response(JsonObject *response, GError **error)
{
    if (json_object_has_member(response, "error")) {
        const char *error_msg = json_object_get_string_member(response, "error");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Ollama Error: %s", 
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Get the full URL for a chat request */
static char*
ollama_get_chat_url(LLMProvider *provider, ChatGptBuddy *buddy)
{
    ChatGptAccount *account = purple_connection_get_protocol_data(purple_account_get_connection(buddy->buddy->account));
    const char *custom_endpoint = purple_account_get_string(account->account, "ollama_endpoint", "");
    
    /* Use custom endpoint if specified, otherwise default */
    if (custom_endpoint && *custom_endpoint) {
        return g_strdup_printf("%s%s", custom_endpoint, provider->chat_endpoint);
    } else {
        return g_strdup_printf("%s%s", provider->endpoint_url, provider->chat_endpoint);
    }
}

/* Get additional headers for Ollama */
static GHashTable*
ollama_get_additional_headers(ChatGptAccount *account, ChatGptBuddy *buddy)
{
    GHashTable *headers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    g_hash_table_insert(headers, g_strdup("Content-Type"), g_strdup("application/json"));
    
    return headers;
}

/* Parse error response */
static char*
ollama_parse_error(JsonObject *response)
{
    const char *error_msg;
    
    if (!json_object_has_member(response, "error")) {
        return g_strdup("Unknown error");
    }
    
    error_msg = json_object_get_string_member(response, "error");
    
    if (error_msg) {
        return g_strdup(error_msg);
    } else {
        return g_strdup("Unknown error");
    }
}

/* Check if model supports a specific feature */
static gboolean
ollama_model_supports_feature(const char *model, const char *feature)
{
    if (g_strcmp0(feature, "vision") == 0) {
        /* Some Ollama models support vision (llava, bakllava) */
        return g_strstr_len(model, -1, "llava") != NULL ||
               g_strstr_len(model, -1, "bakllava") != NULL;
    }
    
    if (g_strcmp0(feature, "functions") == 0) {
        /* Most newer models support function calling */
        return g_strstr_len(model, -1, "llama3") != NULL ||
               g_strstr_len(model, -1, "mistral") != NULL ||
               g_strstr_len(model, -1, "qwen") != NULL;
    }
    
    return FALSE;
}

/* Ollama provider definition */
static LLMProvider ollama_provider = {
    .name = "ollama",
    .display_name = "Ollama (Local)",
    .endpoint_url = "http://localhost:11434",
    .chat_endpoint = "/api/chat",
    .models = ollama_models,
    .needs_api_key = FALSE,
    .is_local = TRUE,
    .api_format = API_FORMAT_OLLAMA,
    .supports_streaming = TRUE,
    .supports_vision = TRUE,  /* Some models do */
    .supports_functions = TRUE,
    .max_context_length = 32768,  /* Varies by model and configuration */
    .format_request = ollama_format_request,
    .parse_response = ollama_parse_response,
    .get_auth_header = ollama_get_auth_header,
    .validate_response = ollama_validate_response,
    .get_chat_url = ollama_get_chat_url,
    .get_additional_headers = ollama_get_additional_headers,
    .parse_error = ollama_parse_error,
    .model_supports_feature = ollama_model_supports_feature
};

/* Initialize the Ollama provider */
void
llm_provider_ollama_init(void)
{
    llm_provider_registry_register(&ollama_provider);
}

