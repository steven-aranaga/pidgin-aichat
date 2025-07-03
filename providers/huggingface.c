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

/* Popular Hugging Face models (OpenAI-compatible endpoint) */
static const char *huggingface_models[] = {
    "meta-llama/Meta-Llama-3.1-70B-Instruct",
    "meta-llama/Meta-Llama-3.1-8B-Instruct",
    "mistralai/Mixtral-8x7B-Instruct-v0.1",
    "mistralai/Mistral-7B-Instruct-v0.3",
    "microsoft/DialoGPT-large",
    "microsoft/DialoGPT-medium",
    "HuggingFaceH4/zephyr-7b-beta",
    "teknium/OpenHermes-2.5-Mistral-7B",
    "NousResearch/Nous-Hermes-2-Mixtral-8x7B-DPO",
    "openchat/openchat-3.5-1210",
    "Qwen/Qwen2.5-72B-Instruct",
    "Qwen/Qwen2.5-7B-Instruct",
    NULL
};

/* Format a chat request for Hugging Face (uses OpenAI format) */
static JsonObject*
huggingface_format_request(AiChatBuddy *buddy, const char *message)
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
    
    /* Build request */
    json_object_set_string_member(request, "model", buddy->model ? buddy->model : "meta-llama/Meta-Llama-3.1-8B-Instruct");
    json_object_set_array_member(request, "messages", messages);
    json_object_set_double_member(request, "temperature", 0.7);
    json_object_set_int_member(request, "max_tokens", 2048);
    
    return request;
}

/* Parse a response from Hugging Face */
static char*
huggingface_parse_response(JsonObject *response, GError **error)
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

/* Get the authentication header for Hugging Face */
static const char*
huggingface_get_auth_header(AiChatAccount *account)
{
    static char auth_header[512];
    const char *api_key;
    
    api_key = purple_account_get_string(account->account, "api_key", "");
    g_snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    
    return auth_header;
}

/* Validate a response from Hugging Face */
static gboolean
huggingface_validate_response(JsonObject *response, GError **error)
{
    if (json_object_has_member(response, "error")) {
        JsonObject *error_obj = json_object_get_object_member(response, "error");
        const char *error_msg = json_object_get_string_member(error_obj, "message");
        const char *error_type = json_object_get_string_member(error_obj, "type");
        
        if (error) {
            *error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Hugging Face API Error (%s): %s", 
                                error_type ? error_type : "unknown",
                                error_msg ? error_msg : "Unknown error");
        }
        return FALSE;
    }
    
    return TRUE;
}

/* Get the full URL for a chat request */
static char*
huggingface_get_chat_url(LLMProvider *provider, AiChatBuddy *buddy)
{
    return g_strdup_printf("%s%s", provider->endpoint_url, provider->chat_endpoint);
}

/* Get additional headers for Hugging Face */
static GHashTable*
huggingface_get_additional_headers(AiChatAccount *account, AiChatBuddy *buddy)
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
huggingface_parse_error(JsonObject *response)
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
huggingface_model_supports_feature(const char *model, const char *feature)
{
    if (g_strcmp0(feature, "vision") == 0) {
        /* Most HF models don't support vision yet */
        return FALSE;
    }
    
    if (g_strcmp0(feature, "functions") == 0) {
        /* Some newer models support function calling */
        return g_strstr_len(model, -1, "Mixtral") != NULL ||
               g_strstr_len(model, -1, "Llama-3") != NULL ||
               g_strstr_len(model, -1, "Qwen") != NULL;
    }
    
    return FALSE;
}

/* Hugging Face provider definition */
static LLMProvider huggingface_provider = {
    .name = "huggingface",
    .display_name = "Hugging Face",
    .endpoint_url = "https://api-inference.huggingface.co",
    .chat_endpoint = "/v1/chat/completions",
    .models = huggingface_models,
    .needs_api_key = TRUE,
    .is_local = FALSE,
    .api_format = API_FORMAT_OPENAI,
    .supports_streaming = TRUE,
    .supports_vision = FALSE,
    .supports_functions = TRUE,
    .max_context_length = 32768,  /* Varies by model */
    .format_request = huggingface_format_request,
    .parse_response = huggingface_parse_response,
    .get_auth_header = huggingface_get_auth_header,
    .validate_response = huggingface_validate_response,
    .get_chat_url = huggingface_get_chat_url,
    .get_additional_headers = huggingface_get_additional_headers,
    .parse_error = huggingface_parse_error,
    .model_supports_feature = huggingface_model_supports_feature
};

/* Initialize the Hugging Face provider */
void
llm_provider_huggingface_init(void)
{
    llm_provider_registry_register(&huggingface_provider);
}

