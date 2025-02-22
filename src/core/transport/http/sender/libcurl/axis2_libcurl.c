/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifdef AXIS2_LIBCURL_ENABLED

#include "axis2_libcurl.h"
#include <axiom_soap.h>
#include <axutil_string.h>
#include <axis2_http_transport.h>
#include <axiom_output.h>
#include <axis2_op_ctx.h>
#include <axis2_ctx.h>
#include <axis2_conf_ctx.h>
#include <axis2_http_client.h>
#include <axiom_xml_writer.h>
#include <axutil_property.h>
#include <axutil_param.h>
#include <axutil_types.h>
#include <axutil_generic_obj.h>
#include <axis2_const.h>
#include <axis2_util.h>
#include <stdlib.h>
#include <axis2_http_sender.h>
#include <axis2_http_transport.h>
#include "libcurl_stream.h"

static int ref = 0;

struct axis2_libcurl
{
    axis2_char_t *memory;
    axutil_array_list_t *alist;
    unsigned int size;
    const axutil_env_t *env;
    char errorbuffer[CURL_ERROR_SIZE];
    CURL *handler;
    axis2_bool_t cookies;
};

static size_t 
axis2_libcurl_write_memory_callback(
    void *ptr,
    size_t size,
    size_t nmemb,
    void *data);

static size_t 
axis2_libcurl_header_callback(
    void *ptr,
    size_t size,
    size_t nmemb,
    void *data);

static void
axis2_libcurl_free_headers(    
    axis2_libcurl_t *curl,
    const axutil_env_t * env);

axis2_status_t AXIS2_CALL
axis2_libcurl_set_options(
    CURL *handler,
    const axutil_env_t * env,
    axis2_msg_ctx_t * msg_ctx);

axis2_status_t AXIS2_CALL
axis2_libcurl_send(
    axis2_libcurl_t *data,
    axiom_output_t * om_output,
    const axutil_env_t * env,
    axis2_msg_ctx_t * msg_ctx,
    axiom_soap_envelope_t * out,
    const axis2_char_t * str_url,
    const axis2_char_t * soap_action)
{
    struct curl_slist *headers = NULL;
    axiom_soap_body_t *soap_body;
    axis2_bool_t is_soap = AXIS2_TRUE;
    axis2_bool_t send_via_get = AXIS2_FALSE;
    axis2_bool_t send_via_head = AXIS2_FALSE;
    axis2_bool_t send_via_put = AXIS2_FALSE;
    axis2_bool_t send_via_delete = AXIS2_FALSE;
    axis2_bool_t doing_mtom = AXIS2_FALSE;
    axiom_node_t *body_node = NULL;
    axiom_node_t *data_out = NULL;
    axutil_property_t *method = NULL;
    axis2_char_t *method_value = NULL;
    axiom_xml_writer_t *xml_writer = NULL;
    axis2_char_t *buffer = NULL;
    unsigned int buffer_size = 0;
    int content_length = -1;
    axis2_char_t *content_type = NULL;
    axis2_char_t *content_len = AXIS2_HTTP_HEADER_CONTENT_LENGTH_;
    const axis2_char_t *char_set_enc = NULL;
    axis2_char_t *content = AXIS2_HTTP_HEADER_CONTENT_TYPE_;
    axis2_char_t *soap_action_header = AXIS2_HTTP_HEADER_SOAP_ACTION_;
    axutil_stream_t *in_stream;
    axutil_property_t *trans_in_property;
    axutil_string_t *char_set_enc_str;
    axis2_byte_t *output_stream = NULL;
    int output_stream_size = 0;
    CURL *handler;
    axis2_conf_ctx_t *conf_ctx = NULL;
    axis2_conf_t *conf = NULL;
    axis2_transport_out_desc_t *trans_desc = NULL;
    axutil_param_t *write_xml_declaration_param = NULL;
    axutil_hash_t *transport_attrs = NULL;
    axis2_bool_t write_xml_declaration = AXIS2_FALSE;
    axutil_property_t *property;
    int *response_length = NULL;
    axis2_char_t *tmp_strcat = NULL;
    long status_code = 0;
    double content_length_dbl = 0;
    /* http header property to get HTTP headers from msg_ctx and give
     * it to http_sender */
    axutil_property_t *http_property = NULL;
    axutil_array_list_t *array_list;

    AXIS2_PARAM_CHECK(env->error, data, AXIS2_FAILURE);
    AXIS2_PARAM_CHECK(env->error, data->handler, AXIS2_FAILURE);

    handler = data->handler;
    curl_easy_reset(handler);
    curl_easy_setopt(handler, CURLOPT_ERRORBUFFER, &data->errorbuffer);

    /* User-Agent:Axis2/C header */
    headers = curl_slist_append(headers, AXIS2_HTTP_HEADER_USER_AGENT_AXIS2C);
    /* headers = curl_slist_append(headers, AXIS2_HTTP_HEADER_ACCEPT_); */
    /* headers = curl_slist_append(headers, AXIS2_HTTP_HEADER_EXPECT_); */
    curl_easy_setopt(handler, CURLOPT_ACCEPT_ENCODING, "gzip,deflate");

    http_property =
        axis2_msg_ctx_get_property (msg_ctx,
                                    env, AXIS2_TRANSPORT_HEADER_PROPERTY);
    if (http_property)
    {
        array_list = (axutil_array_list_t *)
            axutil_property_get_value (http_property, env);
        /* axis2_http_sender_add_header_list (request, env, array_list); */
        int ii = 0;
        int kk = 0;
        axis2_http_header_t *http_header = NULL;
        axis2_char_t *header_str = NULL;
        ii = axutil_array_list_size (array_list, env);
        for (; kk < ii; kk++) {
            http_header = (axis2_http_header_t *) axutil_array_list_get (array_list, env, kk);
            axis2_char_t *name = axis2_http_header_get_name(http_header, env);
            axis2_char_t *value = axis2_http_header_get_value(http_header, env);
            int plen = axutil_strlen(name) + axutil_strlen(value) + 1;
            header_str = AXIS2_MALLOC (env->allocator, sizeof (axis2_char_t) * plen + 1);
            sprintf(header_str, "%s:%s", name, value);

            headers = curl_slist_append(headers, header_str);
            AXIS2_LOG_DEBUG(env->log, AXIS2_LOG_SI, header_str);

            AXIS2_FREE(env->allocator, header_str);
            header_str = NULL;
        }
    }

    if (AXIS2_FAILURE == axis2_libcurl_set_options(handler, env, msg_ctx))
    {
        return AXIS2_FAILURE;
    }

    if (AXIS2_TRUE == axis2_msg_ctx_get_doing_rest(msg_ctx, env))
    {
        is_soap = AXIS2_FALSE;
    }
    else
    {
        is_soap = AXIS2_TRUE;
    }

    if (!is_soap)
    {
        soap_body = axiom_soap_envelope_get_body(out, env);
        if (!soap_body)
        {
            AXIS2_HANDLE_ERROR(env,
                            AXIS2_ERROR_SOAP_ENVELOPE_OR_SOAP_BODY_NULL,
                            AXIS2_FAILURE);
            return AXIS2_FAILURE;
        }

        body_node = axiom_soap_body_get_base_node(soap_body, env);
        if (!body_node)
        {
            AXIS2_HANDLE_ERROR(env,
                               AXIS2_ERROR_SOAP_ENVELOPE_OR_SOAP_BODY_NULL,
                               AXIS2_FAILURE);

            return AXIS2_FAILURE;
        }
        data_out = axiom_node_get_first_element(body_node, env);

        method = (axutil_property_t *) axis2_msg_ctx_get_property(msg_ctx, env,
                                                                  AXIS2_HTTP_METHOD);

        if (method)
        {
            method_value =
                (axis2_char_t *) axutil_property_get_value(method, env);
        }

        /* The default is POST */
        if (method_value && 0 == axutil_strcmp(method_value, AXIS2_HTTP_GET))
        {
            send_via_get = AXIS2_TRUE;
        }
        else if (method_value && 0 == axutil_strcmp(method_value, AXIS2_HTTP_HEAD))
        {
            send_via_head = AXIS2_TRUE;
        }
        else if (method_value && 0 == axutil_strcmp(method_value, AXIS2_HTTP_PUT))
        {
            send_via_put = AXIS2_TRUE;
        }
        else if (method_value && 0 == axutil_strcmp(method_value, AXIS2_HTTP_DELETE))
        {
            send_via_delete = AXIS2_TRUE;
        }
    }

    conf_ctx = axis2_msg_ctx_get_conf_ctx (msg_ctx, env);
    if (conf_ctx)
    {
        conf = axis2_conf_ctx_get_conf (conf_ctx, env);
    }

    if (conf)
    {
        trans_desc = axis2_conf_get_transport_out (conf,
                                                   env, AXIS2_TRANSPORT_ENUM_HTTP);
    }

    if (trans_desc)
    {
        write_xml_declaration_param =
            axutil_param_container_get_param
            (axis2_transport_out_desc_param_container (trans_desc, env), env,
             AXIS2_XML_DECLARATION);
    }


    if (write_xml_declaration_param)
    {
        transport_attrs = 
            axutil_param_get_attributes (write_xml_declaration_param, env);
        if (transport_attrs)
        {
            axutil_generic_obj_t *obj = NULL;
            axiom_attribute_t *write_xml_declaration_attr = NULL;
            axis2_char_t *write_xml_declaration_attr_value = NULL;

            obj = axutil_hash_get (transport_attrs, AXIS2_ADD_XML_DECLARATION,
                           AXIS2_HASH_KEY_STRING);
            if (obj)
            {
                write_xml_declaration_attr = (axiom_attribute_t *) 
                    axutil_generic_obj_get_value (obj,
                                                  env);
            }
            if (write_xml_declaration_attr)
            {
                write_xml_declaration_attr_value = 
                    axiom_attribute_get_value (write_xml_declaration_attr, env);
            }
            if (write_xml_declaration_attr_value && 
                0 == axutil_strcasecmp (write_xml_declaration_attr_value, 
                                        AXIS2_VALUE_TRUE))
            {
                write_xml_declaration = AXIS2_TRUE;
            }
        }
    }

    if (write_xml_declaration)
    {
        axiom_output_write_xml_version_encoding (om_output, env);
    }

    if (!send_via_get && !send_via_head && !send_via_delete)
    {
        xml_writer = axiom_output_get_xml_writer(om_output, env);

        char_set_enc_str = axis2_msg_ctx_get_charset_encoding(msg_ctx, env);

        if (!char_set_enc_str)
        {
            char_set_enc = AXIS2_DEFAULT_CHAR_SET_ENCODING;
        }
        else
        {
            char_set_enc = axutil_string_get_buffer(char_set_enc_str, env);
        }

        if (!send_via_put && is_soap)
        {
            doing_mtom = axis2_msg_ctx_get_doing_mtom(msg_ctx, env);

            axiom_output_set_do_optimize(om_output, env, doing_mtom);
            axiom_soap_envelope_serialize(out, env, om_output, AXIS2_FALSE);
            if (AXIS2_TRUE == axis2_msg_ctx_get_is_soap_11(msg_ctx, env))
            {
                if (AXIS2_ESC_DOUBLE_QUOTE != *soap_action)
                {
                    axis2_char_t *tmp_soap_action = NULL;
                    tmp_soap_action =
                        AXIS2_MALLOC(env->allocator,
                                     (axutil_strlen(soap_action) +
                                      5) * sizeof(axis2_char_t));
                    sprintf(tmp_soap_action, "\"%s\"", soap_action);
                    tmp_strcat = axutil_stracat(env, soap_action_header,tmp_soap_action);
                    headers = curl_slist_append(headers, tmp_strcat);
                    AXIS2_FREE(env->allocator, tmp_strcat);
                    AXIS2_FREE(env->allocator, tmp_soap_action);
                }
                else
                {
                    tmp_strcat = axutil_stracat(env, soap_action_header, soap_action);
                    headers = curl_slist_append(headers, tmp_strcat );
                    AXIS2_FREE(env->allocator, tmp_strcat);
                }
            }

            if (doing_mtom)
            {
		axis2_char_t *temp_content_type = NULL;
                /*axiom_output_flush(om_output, env, &output_stream,
                                   &output_stream_size);*/
                axiom_output_flush(om_output, env);
                content_type =
                    (axis2_char_t *) axiom_output_get_content_type(om_output,
                                                                   env);

                /*Create a local copy of content_type, to uniformize the memory management
                 over this function (axiom_output_get_content_type() returns a weak link)*/
                temp_content_type = axutil_strdup(env,content_type);
                content_type = temp_content_type;

                if (AXIS2_TRUE != axis2_msg_ctx_get_is_soap_11(msg_ctx, env))
                {
                    if (axutil_strcmp(soap_action, ""))
                    {
                        /* handle SOAP action for SOAP 1.2 case */
                        temp_content_type = axutil_stracat (env,
                                                            content_type,
                                                            AXIS2_CONTENT_TYPE_ACTION);
                        content_type = temp_content_type;
                        temp_content_type = axutil_stracat (env,
                                                            content_type,
                                                            soap_action);
                        AXIS2_FREE (env->allocator, content_type);
                        content_type = temp_content_type;
                        temp_content_type =
                            axutil_stracat (env, content_type, 
                                            AXIS2_ESC_DOUBLE_QUOTE_STR);
                        AXIS2_FREE (env->allocator, content_type);
                        content_type = temp_content_type;
                    }
                }
            }
            else if (AXIS2_TRUE == axis2_msg_ctx_get_is_soap_11(msg_ctx, env))
            {
                axis2_char_t *temp_content_type = NULL;
                content_type =
                    (axis2_char_t *) AXIS2_HTTP_HEADER_ACCEPT_TEXT_XML;
                content_type = axutil_stracat(env, content_type, 
                                              AXIS2_CONTENT_TYPE_CHARSET);
                temp_content_type =
                    axutil_stracat(env, content_type, char_set_enc);
                AXIS2_FREE(env->allocator, content_type);
                content_type = temp_content_type;
            }
            else
            {
                axis2_char_t *temp_content_type = NULL;
                content_type =
                    (axis2_char_t *) AXIS2_HTTP_HEADER_ACCEPT_APPL_SOAP;
                content_type = axutil_stracat(env, content_type, 
                                              AXIS2_CONTENT_TYPE_CHARSET);
                temp_content_type =
                    axutil_stracat(env, content_type, char_set_enc);
                AXIS2_FREE(env->allocator, content_type);
                content_type = temp_content_type;
                if (axutil_strcmp(soap_action, ""))
                {
                    temp_content_type =
                        axutil_stracat(env, content_type, 
                                       AXIS2_CONTENT_TYPE_ACTION);
                    AXIS2_FREE(env->allocator, content_type);
                    content_type = temp_content_type;
                    temp_content_type =
                        axutil_stracat(env, content_type, soap_action);
                    AXIS2_FREE(env->allocator, content_type);
                    content_type = temp_content_type;
                }
                temp_content_type = axutil_stracat(env, content_type, 
                                                   AXIS2_SEMI_COLON_STR);
                AXIS2_FREE(env->allocator, content_type);
                content_type = temp_content_type;
            }
        }
        else if (is_soap)
        {
            AXIS2_LOG_ERROR (env->log, AXIS2_LOG_SI, "Attempt to send SOAP"
                             "message using HTTP PUT failed");
            return AXIS2_FAILURE;
        }
        else
        {
            axutil_property_t *content_type_property = NULL;
            axutil_hash_t *content_type_hash = NULL;
            axis2_char_t *content_type_value = NULL;

            axiom_node_serialize(data_out, env, om_output);
            content_type_property =
                (axutil_property_t *) 
                axis2_msg_ctx_get_property(msg_ctx, env,
                                           AXIS2_USER_DEFINED_HTTP_HEADER_CONTENT_TYPE);

            if (content_type_property)
            {
                content_type_hash =
                    (axutil_hash_t *)
                    axutil_property_get_value(content_type_property, env);

                if (content_type_hash)
                {
                    content_type_value =
                        (char *) axutil_hash_get(content_type_hash,
                                                 AXIS2_HTTP_HEADER_CONTENT_TYPE,
                                                 AXIS2_HASH_KEY_STRING);
                }
            }

            if (content_type_value)
            {
                content_type = content_type_value;
            }
            else
            {
                content_type = axutil_strdup(env,AXIS2_HTTP_HEADER_ACCEPT_TEXT_XML);
            }

        }

        buffer = axiom_xml_writer_get_xml(xml_writer, env);
        if (!doing_mtom)
        {
            buffer_size = axiom_xml_writer_get_xml_size(xml_writer, env);
        }
        else
            buffer_size = output_stream_size;
        {
            /*
            * Curl calculates the content-length automatically.
            * This commented section is not only unnecessary,
            * it interferes with authentication.
            *
            * NTLM, for example, will send an empty request
            * first (no body) to get the auth challenge
            * before resending with actual content.
            */
            /*
            char tmp_buf[10];
            sprintf(tmp_buf, "%d", buffer_size);
            tmp_strcat = axutil_stracat(env, content_len, tmp_buf);
            headers = curl_slist_append(headers, tmp_strcat);
            AXIS2_FREE(env->allocator, tmp_strcat);
            tmp_strcat = NULL;
            */

            tmp_strcat = axutil_stracat(env, content, content_type);
            headers = curl_slist_append(headers, tmp_strcat);
            AXIS2_FREE(env->allocator, tmp_strcat);
            tmp_strcat = NULL;
        }

        if (!doing_mtom)
        {
            curl_easy_setopt(handler, CURLOPT_POSTFIELDSIZE, buffer_size);
            curl_easy_setopt(handler, CURLOPT_POSTFIELDS, buffer);
        }
        else
        {
            curl_easy_setopt(handler, CURLOPT_POSTFIELDSIZE,
                             output_stream_size);
            curl_easy_setopt(handler, CURLOPT_POSTFIELDS, output_stream);
        }


        if (send_via_put)
        {
            curl_easy_setopt(handler, CURLOPT_CUSTOMREQUEST, AXIS2_HTTP_PUT);
        }
        curl_easy_setopt(handler, CURLOPT_URL, str_url);
    }
    else
    {
        axis2_char_t *request_param;
        axis2_char_t *url_encode;
        request_param =
            (axis2_char_t *) axis2_http_sender_get_param_string(NULL, env,
                                                                msg_ctx);
        url_encode = axutil_strcat(env, str_url, AXIS2_Q_MARK_STR, 
                                   request_param, NULL);
        if (send_via_get)
        {
            curl_easy_setopt(handler, CURLOPT_HTTPGET, 1);
        }
        else if (send_via_head)
        {
            curl_easy_setopt(handler, CURLOPT_NOBODY, 1);
        }
        else if (send_via_delete)
        {
            curl_easy_setopt(handler, CURLOPT_CUSTOMREQUEST, AXIS2_HTTP_DELETE);
        }
        curl_easy_setopt(handler, CURLOPT_URL, url_encode);
    }

    {
        axis2_bool_t manage_session;
        manage_session = axis2_msg_ctx_get_manage_session(msg_ctx, env);
        if (manage_session == AXIS2_TRUE) 
        {
            if (data->cookies == AXIS2_FALSE)
            {
                /* Ensure cookies enabled to manage session */
                /* Pass empty cookie string to enable cookies */
                curl_easy_setopt(handler, CURLOPT_COOKIEFILE, " ");	
                data->cookies = AXIS2_TRUE;
            }
        }
        else if (data->cookies == AXIS2_TRUE)
        {
            /* Pass special string ALL to reset cookies if any have been enabled. */
            /* If cookies have ever been enabled, we reset every time as long as 
               manage_session is false, as there is no clear curl option to 
               turn off the cookie engine once enabled. */
            curl_easy_setopt(handler, CURLOPT_COOKIELIST, AXIS2_ALL);	
        }
    }

    curl_easy_setopt(handler, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handler, CURLOPT_WRITEFUNCTION,
                     axis2_libcurl_write_memory_callback);
    curl_easy_setopt(handler, CURLOPT_WRITEDATA, data);

    curl_easy_setopt (handler, CURLOPT_HEADERFUNCTION, axis2_libcurl_header_callback);

    curl_easy_setopt (handler, CURLOPT_WRITEHEADER, data);

    /* Free response data from previous request */
    if( data->size )
    {
        if (data->memory)
        {
            AXIS2_FREE(data->env->allocator, data->memory);
        }
        data->size = 0;
    }

    if (curl_easy_perform(handler)) 
    {
        AXIS2_LOG_ERROR (env->log, AXIS2_LOG_SI, "%s", &data->errorbuffer);
        AXIS2_HANDLE_ERROR(env, AXIS2_ERROR_HTTP_CLIENT_TRANSPORT_ERROR,
                           AXIS2_FAILURE);
        return AXIS2_FAILURE;
    }

    in_stream = axutil_stream_create_libcurl(env, data->memory, data->size);
    trans_in_property = axutil_property_create(env);
    axutil_property_set_scope(trans_in_property, env, AXIS2_SCOPE_REQUEST);
    axutil_property_set_free_func(trans_in_property, env,
                                  libcurl_stream_free);
    axutil_property_set_value(trans_in_property, env, in_stream);
    axis2_msg_ctx_set_property(msg_ctx, env, AXIS2_TRANSPORT_IN,
                               trans_in_property);

    curl_easy_getinfo(handler, CURLINFO_RESPONSE_CODE, &status_code);
    axis2_msg_ctx_set_status_code (msg_ctx, env, status_code);

    AXIS2_FREE(data->env->allocator, content_type);

    curl_easy_getinfo(handler, CURLINFO_CONTENT_TYPE, &content_type);

    if (content_type)
    {    
        if (strstr (content_type, AXIS2_HTTP_HEADER_ACCEPT_MULTIPART_RELATED)
            && strstr (content_type, AXIS2_HTTP_HEADER_ACCEPT_XOP_XML))
        {
            axis2_ctx_t *axis_ctx =
                axis2_op_ctx_get_base (axis2_msg_ctx_get_op_ctx (msg_ctx, env),
                                       env);
            property = axutil_property_create (env);
            axutil_property_set_scope (property, env, AXIS2_SCOPE_REQUEST);
            axutil_property_set_value (property,
                                       env, axutil_strdup (env, content_type));
            axis2_ctx_set_property (axis_ctx,
                                    env, MTOM_RECIVED_CONTENT_TYPE, property);
        }
    } 


    curl_easy_getinfo(handler, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length_dbl);

    /* Note: This conversion will result in a loss of precision
    Content-Length should be represented as a 64-bit number */

    if (content_length >= 0)
    {
        response_length = AXIS2_MALLOC (env->allocator, sizeof (int));
        memcpy (response_length, &content_length, sizeof (int));
        property = axutil_property_create (env);
        axutil_property_set_scope (property, env, AXIS2_SCOPE_REQUEST);
        axutil_property_set_value (property, env, response_length);
        axis2_msg_ctx_set_property (msg_ctx, env,
                                    AXIS2_HTTP_HEADER_CONTENT_LENGTH, property);
    }

    curl_slist_free_all (headers);
    /* release the read http headers. */
    /* (commenting out the call below is a clever way to force a premature EOF 
       condition in subsequent messages, as they will be read using the content-length
       of the first message.) */
    axis2_libcurl_free_headers(data, env);

    /* no need to free content_type since now we only have a weak reference
     and curl will do this */

    return AXIS2_SUCCESS;
}

/**
 * axis2_libcurl_set_auth_options maps authentication AXIS2/C options to
 * libcURL options.
 *
 * CURLOPT_USERPWD - char * user:password for authentication
 * CURLOPT_HTTPAUTH - long bitmask which authentication methods to use
 */
static axis2_status_t
axis2_libcurl_set_auth_options(
    CURL *handler,
    const axutil_env_t * env,
    axis2_msg_ctx_t * msg_ctx)
{
    axutil_property_t *property = NULL;
    axis2_char_t *uname = NULL;
    axis2_char_t *passwd = NULL;
    axis2_char_t *auth_type = NULL;
    int auth_type_mask = 0;

    property = axis2_msg_ctx_get_property(msg_ctx, env, AXIS2_HTTP_AUTH_UNAME);
    if (property)
    {
        uname = (axis2_char_t *) axutil_property_get_value(property, env);
    }
    property = axis2_msg_ctx_get_property(msg_ctx, env,
                                          AXIS2_HTTP_AUTH_PASSWD);
    if (property)
    {
        passwd = (axis2_char_t *) axutil_property_get_value(property, env);
    }
    if (uname && passwd)
    {
        axis2_char_t buffer[256];
        strncpy(buffer, uname, 256);
        strncat(buffer, ":", 256);
        strncat(buffer, passwd, 256);
        curl_easy_setopt(handler, CURLOPT_USERPWD, buffer);
    }
    
    property = (axutil_property_t *) 
        axis2_msg_ctx_get_property(msg_ctx, env, AXIS2_HTTP_AUTH_TYPE);

    if (property)
    {
        auth_type = (axis2_char_t *) axutil_property_get_value(property, env);
    }

    if (auth_type)
    {
         if(0 == axutil_strcmp(auth_type, AXIS2_HTTP_AUTH_TYPE_BASIC))
             auth_type_mask |= CURLAUTH_BASIC;
         if(0 == axutil_strcmp(auth_type, AXIS2_HTTP_AUTH_TYPE_DIGEST))
             auth_type_mask |= CURLAUTH_DIGEST;
         if(0 == axutil_strcmp(auth_type, AXIS2_HTTP_AUTH_TYPE_NTLM))
             auth_type_mask |= CURLAUTH_NTLM;
         if(0 == axutil_strcmp(auth_type, AXIS2_HTTP_AUTH_TYPE_GSSNEGOTIATE))
             auth_type_mask |= CURLAUTH_GSSNEGOTIATE;
         if(0 == axutil_strcmp(auth_type, AXIS2_HTTP_AUTH_TYPE_ANY))
             auth_type_mask |= CURLAUTH_ANY;
         if(0 == axutil_strcmp(auth_type, AXIS2_HTTP_AUTH_TYPE_ANYSAFE))
             auth_type_mask |= CURLAUTH_ANYSAFE;

        if(auth_type_mask != 0)
             curl_easy_setopt(handler, CURLOPT_HTTPAUTH, auth_type_mask);
    }
    else
    {
        /* Uses anonymous connection.*/
    }

    return AXIS2_SUCCESS;
}

/**
 * axis2_libcurl_set_proxy_options maps proxy AXIS2/C options to
 * libcURL options.
 *
 * CURLOPT_PROXY - char * proxy hostname
 * CURLOPT_PROXYPORT - long proxy listen port
 * CURLOPT_PROXYUSERPWD - char * user:password to authenticate to proxy
 *
 * TODO:
 * CURLOPT_PROXYTYPE - long enum type of proxy (HTTP, SOCKS)
 * CURLOPT_PROXYAUTH - long bitmask which authentication methods to use for proxy
 */
static axis2_status_t
axis2_libcurl_set_proxy_options(
    CURL *handler,
    const axutil_env_t * env,
    axis2_msg_ctx_t * msg_ctx)
{
    axis2_conf_ctx_t *conf_ctx = NULL;
    axis2_conf_t *conf = NULL;
    axis2_transport_out_desc_t *trans_desc = NULL;
    axutil_param_t *proxy_param = NULL;
    axutil_hash_t *transport_attrs = NULL;
    axutil_property_t *property = NULL;
    axis2_char_t *uname = NULL;
    axis2_char_t *passwd = NULL;
    axis2_char_t *proxy_host = NULL;
    axis2_char_t *proxy_port = NULL;
    axis2_char_t *auth_type = NULL;
    int auth_type_mask = 0;

    property = axis2_msg_ctx_get_property(msg_ctx, env,
                                          AXIS2_PROXY_AUTH_UNAME);
    if (property)
    {
        uname = (axis2_char_t *) axutil_property_get_value(property, env);
    }
    property = axis2_msg_ctx_get_property(msg_ctx, env,
                                          AXIS2_PROXY_AUTH_PASSWD);
    if (property)
    {
        passwd = (axis2_char_t *) axutil_property_get_value(property, env);
    }

    conf_ctx = axis2_msg_ctx_get_conf_ctx(msg_ctx, env);
    if (conf_ctx)
    {
        conf = axis2_conf_ctx_get_conf(conf_ctx, env);
        if (conf)
        {
            trans_desc = axis2_conf_get_transport_out(conf, env, 
                                                      AXIS2_TRANSPORT_ENUM_HTTP);
        }
    }
    if (trans_desc)
    {
        proxy_param = axutil_param_container_get_param(
                axis2_transport_out_desc_param_container(trans_desc, env), 
                env, AXIS2_HTTP_PROXY_API);
        if (!proxy_param)
        {
            proxy_param = axutil_param_container_get_param(
                axis2_transport_out_desc_param_container(trans_desc, env), 
                env, AXIS2_HTTP_PROXY);
        } 
        if (proxy_param)
        {
            transport_attrs = axutil_param_get_attributes(proxy_param, env);
        }
    }

    if (transport_attrs)
    {
        axutil_generic_obj_t *obj = NULL;
        axiom_attribute_t *attr = NULL;

        if (!uname || !passwd)
        {
            obj = axutil_hash_get(transport_attrs, AXIS2_HTTP_PROXY_USERNAME,
                                  AXIS2_HASH_KEY_STRING);
            if (obj)
            {
                attr = (axiom_attribute_t *) 
                    axutil_generic_obj_get_value(obj, env);
            }
            if (attr)
            {
                uname = axiom_attribute_get_value(attr, env);
            }

            attr = NULL;
            obj = axutil_hash_get(transport_attrs, AXIS2_HTTP_PROXY_PASSWORD,
                                  AXIS2_HASH_KEY_STRING);
            if (obj)
            {
                attr = (axiom_attribute_t *) 
                    axutil_generic_obj_get_value(obj, env);
            }
            if (attr)
            {
                passwd = axiom_attribute_get_value(attr, env);
            }
        }

        obj = axutil_hash_get(transport_attrs, AXIS2_HTTP_PROXY_HOST,
                              AXIS2_HASH_KEY_STRING);
        if (obj)
        {
            attr = (axiom_attribute_t *) 
                axutil_generic_obj_get_value(obj, env);
        }
        if (attr)
        {
            proxy_host = axiom_attribute_get_value(attr, env);
        }
        if (proxy_host)
        {
            curl_easy_setopt(handler, CURLOPT_PROXY, proxy_host);
        }

        attr = NULL;
        obj = axutil_hash_get(transport_attrs, AXIS2_HTTP_PROXY_PORT,
                              AXIS2_HASH_KEY_STRING);
        if (obj)
        {
            attr = (axiom_attribute_t *) 
                axutil_generic_obj_get_value(obj, env);
        }
        if (attr)
        {
            proxy_port = axiom_attribute_get_value(attr, env);
        }
        if (proxy_port)
        {
            curl_easy_setopt(handler, CURLOPT_PROXYPORT,
                             AXIS2_ATOI(proxy_port));
        }
    }
    if (uname && passwd)
    {
        axis2_char_t buffer[256];
        strncpy(buffer, uname, 256);
        strncat(buffer, ":", 256);
        strncat(buffer, passwd, 256);
        curl_easy_setopt(handler, CURLOPT_PROXYUSERPWD, buffer);
    }

    property = (axutil_property_t *)
        axis2_msg_ctx_get_property(msg_ctx, env, AXIS2_PROXY_AUTH_TYPE);

    if (property)
    {
        auth_type = (axis2_char_t *) axutil_property_get_value(property, env);
    }

    if (auth_type)
    {
        if(0 == axutil_strcmp(auth_type, AXIS2_PROXY_AUTH_TYPE_BASIC))
            auth_type_mask |= CURLAUTH_BASIC;
        if(0 == axutil_strcmp(auth_type, AXIS2_PROXY_AUTH_TYPE_DIGEST))
            auth_type_mask |= CURLAUTH_DIGEST;
        if(0 == axutil_strcmp(auth_type, AXIS2_PROXY_AUTH_TYPE_NTLM))
            auth_type_mask |= CURLAUTH_NTLM;

        if(auth_type_mask != 0)
            curl_easy_setopt(handler, CURLOPT_PROXYAUTH, auth_type_mask);
    }
    else
    {
        /* Uses anonymous connection.*/
    }

    return AXIS2_SUCCESS;
}

/**
 * axis2_libcurl_set_ssl_options maps SSL AXIS2/C options to
 * libcURL options.
 *
 * CURLOPT_SSL_VERIFYHOST - long enum whether to verify the server identity
 * CURLOPT_SSL_VERIFYPEER - long boolean whether to verify the server certificate
 */
static axis2_status_t
axis2_libcurl_set_ssl_options(
    CURL *handler,
    const axutil_env_t * env,
    axis2_msg_ctx_t * msg_ctx)
{
    axutil_property_t *property = NULL;
    axis2_char_t *verify_peer = NULL;
    axis2_char_t *verify_host = NULL;

    property = axis2_msg_ctx_get_property(msg_ctx, env,
                                          AXIS2_SSL_VERIFY_PEER);
    if (property)
    {
        verify_peer = (axis2_char_t *)
            axutil_property_get_value(property, env);
    }
    if (verify_peer)
    {
        if (0 == axutil_strcasecmp(verify_peer, AXIS2_VALUE_TRUE))
        {
            curl_easy_setopt(handler, CURLOPT_SSL_VERIFYPEER, 1);
        }
        else
        {
            curl_easy_setopt(handler, CURLOPT_SSL_VERIFYPEER, 0);
        }
    }

    property = axis2_msg_ctx_get_property(msg_ctx, env,
                                          AXIS2_SSL_VERIFY_HOST);
    if (property)
    {
        verify_host = (axis2_char_t *)
            axutil_property_get_value(property, env);
    }
    if (verify_host)
    {
        curl_easy_setopt(handler, CURLOPT_SSL_VERIFYHOST,
                         AXIS2_ATOI(verify_host));
    }

    return AXIS2_SUCCESS;
}

/**
 * axis2_libcurl_set_connection_options maps connection AXIS2/C options to
 * libcURL options.
 * CURLOPT_CONNECTTIMEOUT_MS - long connection timeout in milliseconds
 * CURLOPT_TIMEOUT_MS - long transfer timeout in milliseconds
 */
static axis2_status_t
axis2_libcurl_set_connection_options(
    CURL *handler,
    const axutil_env_t * env,
    axis2_msg_ctx_t * msg_ctx)
{
    axutil_property_t *property = NULL;
    long long_property_value = 0;

    /* check if timeout has been set by user using options 
     * with axis2_options_set_timeout_in_milli_seconds
     */
    property = axis2_msg_ctx_get_property(msg_ctx, env,
                                          AXIS2_HTTP_CONNECTION_TIMEOUT);
    if (property)
    {
        axis2_char_t *value = axutil_property_get_value(property, env);
        if (value) 
        {
            long_property_value = AXIS2_ATOI(value);
            curl_easy_setopt(handler, CURLOPT_CONNECTTIMEOUT_MS,
                             long_property_value);
        }
    }

    property = axis2_msg_ctx_get_property(msg_ctx, env,
                                          AXIS2_HTTP_SO_TIMEOUT);
    if (property)
    {
        axis2_char_t *value = axutil_property_get_value(property, env);
        if (value) 
        {
            long_property_value = AXIS2_ATOI(value);
            curl_easy_setopt(handler, CURLOPT_TIMEOUT_MS,
                             long_property_value);
        }
    }

    return AXIS2_SUCCESS;
}

/**
 * axis2_libcurl_set_options maps the AXIS2/C options to libcURL options.
 */
axis2_status_t
axis2_libcurl_set_options(
    CURL *handler,
    const axutil_env_t * env,
    axis2_msg_ctx_t * msg_ctx)
{
    if (axis2_libcurl_set_auth_options(handler, env, msg_ctx) != AXIS2_SUCCESS)
    {
        return AXIS2_FAILURE;
    }

    if (axis2_libcurl_set_proxy_options(handler, env, msg_ctx) !=
        AXIS2_SUCCESS)
    {
        return AXIS2_FAILURE;
    }

    if (axis2_libcurl_set_ssl_options(handler, env, msg_ctx) != AXIS2_SUCCESS)
    {
        return AXIS2_FAILURE;
    }

    if (axis2_libcurl_set_connection_options(handler, env, msg_ctx) !=
        AXIS2_SUCCESS)
    {
        return AXIS2_FAILURE;
    }

    return AXIS2_SUCCESS;
}

static size_t
axis2_libcurl_write_memory_callback(
    void *ptr,
    size_t size,
    size_t nmemb,
    void *data)
{
    size_t realsize = size * nmemb;
    axis2_libcurl_t *curl = (axis2_libcurl_t *) data;
    axis2_char_t *buffer =
        (axis2_char_t *) AXIS2_MALLOC(curl->env->allocator,
                                      curl->size + realsize + 1);
    if (buffer)
    {
        if (curl->size)
        {
            memcpy(&(buffer[0]), curl->memory, curl->size);
            AXIS2_FREE(curl->env->allocator, curl->memory);
        }

        memcpy(&(buffer[curl->size]), ptr, realsize);
        curl->size += (int)realsize;
        /* We are sure that the difference lies within the int range */
        buffer[curl->size] = 0;
        curl->memory = buffer;
    }
    return realsize;
}

static size_t
axis2_libcurl_header_callback(
    void *ptr,
    size_t size,
    size_t nmemb,
    void *data)
{
    axis2_char_t *memory;
    size_t realsize = size * nmemb;
    axis2_libcurl_t *curl = (axis2_libcurl_t *) data;
    memory = (axis2_char_t *)AXIS2_MALLOC(curl->env->allocator, realsize + 1);
    if (memory)
    {
        memcpy(&(memory[0]), ptr, realsize);
        memory[realsize] = 0;
        axutil_array_list_add(curl->alist, curl->env, memory);
    }
    return realsize;
}

axis2_libcurl_t * AXIS2_CALL
axis2_libcurl_create(
    const axutil_env_t * env)
{
    axis2_libcurl_t *curl = NULL;
    CURLcode code;


    if (!ref)
    {
        /* curl_global_init is not thread-safe so it would be better
           to do this, as well as the test and increment of ref, under
           mutex if one is available, or as part of an
           axis2_initialize() if a global initialize is created.
           Otherwise the client application should perform the the
           curl_global_init itself in a thread-safe fashion. 
        */
        code = curl_global_init(CURL_GLOBAL_ALL);
        if (code)	
        {
            AXIS2_LOG_ERROR(env->log, AXIS2_LOG_SI, 
                            "libcurl curl_global_init failed, error: %d", code); 
            return NULL;
        }
        ref++;
    }

    curl =
        (axis2_libcurl_t *) AXIS2_MALLOC(env->allocator,
                                         sizeof(axis2_libcurl_t));
    if (curl) 
    {
        curl->memory = 0;
        curl->size = 0;
        curl->alist = axutil_array_list_create(env, 15);
        curl->env = env;
        curl->handler = curl_easy_init();
        if (curl->handler) {
            curl_easy_setopt(curl->handler, CURLOPT_ACCEPT_ENCODING, "");
        }
        curl->cookies = AXIS2_FALSE;
        if ((!curl->alist) || (!curl->handler))
        {
            axis2_libcurl_free(curl, env);
            curl = 0;
        }
    }
    return curl;
}

void AXIS2_CALL
axis2_libcurl_free(
    axis2_libcurl_t *curl,
    const axutil_env_t * env)
{
    if (!curl)
    {
        return;
    }

    if (curl->handler)
    {
        curl_easy_cleanup (curl->handler); 
    }
    if (curl->alist)
    {
        axis2_libcurl_free_headers(curl, env);
        axutil_array_list_free(curl->alist, env);
        curl->alist = NULL;
    }
    if (curl->memory) 
    {
        AXIS2_FREE(env->allocator, curl->memory);
    }
    
    AXIS2_FREE(env->allocator, curl);
}

static void
axis2_libcurl_free_headers(    
    axis2_libcurl_t *curl,
    const axutil_env_t * env)
{
    int count = 0;
    axutil_array_list_t *header_group = curl->alist;
    if (header_group)
    {
        while ((count = axutil_array_list_size(header_group, env)) > 0)
        {
            axis2_char_t *header = axutil_array_list_remove(header_group, env, count-1);
            AXIS2_FREE(env->allocator, header);
        }
    }
}

#endif                          /* AXIS2_LIBCURL_ENABLED */



