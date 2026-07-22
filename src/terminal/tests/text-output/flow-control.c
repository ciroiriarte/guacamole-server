/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "terminal/terminal.h"
#include "terminal/terminal-priv.h"

#include <CUnit/CUnit.h>
#include <guacamole/client.h>
#include <guacamole/mem.h>
#include <guacamole/protocol-constants.h>
#include <guacamole/socket.h>
#include <guacamole/stream.h>
#include <guacamole/user.h>

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Test fixture for the text-output helpers. Only the fields used by the
 * text-output implementation are initialized, avoiding the full terminal render
 * thread while still exercising real guac_client_for_owner(), user streams,
 * ACK handlers, and protocol serialization.
 */
typedef struct text_output_fixture {
    guac_client* client;
    guac_user* owner;
    guac_terminal* term;
    int read_fd;
    int write_fd;
    guac_client_log_level last_log_level;
    int log_count;
} text_output_fixture;

static void capture_log(guac_client* client, guac_client_log_level level,
        const char* format, va_list ap) {

    text_output_fixture* fixture = (text_output_fixture*) client->data;
    fixture->last_log_level = level;
    fixture->log_count++;

}

static text_output_fixture* text_output_fixture_alloc(void) {

    int pipe_fds[2];
    CU_ASSERT_EQUAL_FATAL(pipe(pipe_fds), 0);

    text_output_fixture* fixture = guac_mem_zalloc(sizeof(text_output_fixture));
    fixture->read_fd = pipe_fds[0];
    fixture->write_fd = pipe_fds[1];

    fixture->client = guac_client_alloc();
    CU_ASSERT_PTR_NOT_NULL_FATAL(fixture->client);
    fixture->client->data = fixture;
    fixture->client->log_handler = capture_log;

    fixture->owner = guac_user_alloc();
    CU_ASSERT_PTR_NOT_NULL_FATAL(fixture->owner);
    fixture->owner->owner = 1;
    fixture->owner->socket = guac_socket_open(fixture->write_fd);
    CU_ASSERT_PTR_NOT_NULL_FATAL(fixture->owner->socket);
    fixture->client->__owner = fixture->owner;

    fixture->term = guac_mem_zalloc(sizeof(guac_terminal));
    fixture->term->client = fixture->client;
    pthread_mutex_init(&fixture->term->lock, NULL);

    return fixture;

}

static void text_output_fixture_free(text_output_fixture* fixture) {

    if (fixture->term != NULL) {
        guac_terminal_text_output_close(fixture->term);
        pthread_mutex_destroy(&fixture->term->lock);
        guac_mem_free(fixture->term);
    }

    if (fixture->owner != NULL) {
        if (fixture->owner->socket != NULL)
            guac_socket_free(fixture->owner->socket);
        guac_user_free(fixture->owner);
    }

    if (fixture->client != NULL) {
        fixture->client->__owner = NULL;
        guac_client_free(fixture->client);
    }

    if (fixture->read_fd != -1)
        close(fixture->read_fd);

    guac_mem_free(fixture);

}

static char* text_output_fixture_read(text_output_fixture* fixture) {

    guac_socket_free(fixture->owner->socket);
    fixture->owner->socket = NULL;

    char* buffer = guac_mem_zalloc(8192);
    int offset = 0;
    int numread;

    while ((numread = read(fixture->read_fd, buffer + offset,
                    8191 - offset)) > 0)
        offset += numread;

    buffer[offset] = '\0';
    close(fixture->read_fd);
    fixture->read_fd = -1;

    return buffer;

}

void test_text_output__disable_copy_blocks_open(void) {

    CU_ASSERT_FALSE(guac_terminal_text_output_should_open(0, 0));
    CU_ASSERT_FALSE(guac_terminal_text_output_should_open(0, 1));
    CU_ASSERT_TRUE(guac_terminal_text_output_should_open(1, 0));
    CU_ASSERT_FALSE(guac_terminal_text_output_should_open(1, 1));

}

void test_text_output__opens_owner_stream_and_acks_blobs(void) {

    text_output_fixture* fixture = text_output_fixture_alloc();

    guac_terminal_text_output_open(fixture->term, "STDOUT", 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(fixture->term->text_output_stream);
    CU_ASSERT_EQUAL(fixture->term->text_output_stream->index % 2, 0);
    CU_ASSERT_PTR_EQUAL(fixture->term->text_output_stream->data, fixture->term);
    CU_ASSERT_PTR_NOT_NULL(fixture->term->text_output_stream->ack_handler);

    guac_terminal_text_output_write(fixture->term, "hello", 5);
    guac_terminal_lock(fixture->term);
    guac_terminal_text_output_flush(fixture->term);
    guac_terminal_unlock(fixture->term);
    CU_ASSERT_EQUAL(fixture->term->text_output_inflight, 1);

    fixture->term->text_output_stream->ack_handler(fixture->owner,
            fixture->term->text_output_stream, "OK",
            GUAC_PROTOCOL_STATUS_SUCCESS);
    CU_ASSERT_EQUAL(fixture->term->text_output_inflight, 0);

    guac_terminal_text_output_close(fixture->term);
    char* instructions = text_output_fixture_read(fixture);

    CU_ASSERT_PTR_NOT_NULL(strstr(instructions,
                "4.pipe,1.0,24.application/octet-stream,6.STDOUT;"));
    CU_ASSERT_PTR_NOT_NULL(strstr(instructions,
                "4.blob,1.0,8.aGVsbG8=;"));
    CU_ASSERT_PTR_NOT_NULL(strstr(instructions, "3.end,1.0;"));

    guac_mem_free(instructions);
    text_output_fixture_free(fixture);

}

void test_text_output__tee_mode_drops_when_consumer_stalls(void) {

    text_output_fixture* fixture = text_output_fixture_alloc();

    guac_terminal_text_output_open(fixture->term, "STDOUT", 0);
    fixture->term->text_output_inflight = GUAC_TERMINAL_TEXT_OUTPUT_MAX_INFLIGHT;

    guac_terminal_text_output_write(fixture->term, "drop", 4);
    guac_terminal_lock(fixture->term);
    guac_terminal_text_output_flush(fixture->term);
    guac_terminal_unlock(fixture->term);

    CU_ASSERT_EQUAL(fixture->client->state, GUAC_CLIENT_RUNNING);
    CU_ASSERT_EQUAL(fixture->term->text_output_length, 0);
    CU_ASSERT_EQUAL(fixture->term->text_output_inflight,
            GUAC_TERMINAL_TEXT_OUTPUT_MAX_INFLIGHT);
    CU_ASSERT_EQUAL(fixture->last_log_level, GUAC_LOG_WARNING);

    guac_terminal_text_output_close(fixture->term);
    char* instructions = text_output_fixture_read(fixture);

    CU_ASSERT_PTR_NOT_NULL(strstr(instructions,
                "4.pipe,1.0,24.application/octet-stream,6.STDOUT;"));
    CU_ASSERT_PTR_NULL(strstr(instructions, "4.blob"));
    CU_ASSERT_PTR_NOT_NULL(strstr(instructions, "3.end,1.0;"));

    guac_mem_free(instructions);
    text_output_fixture_free(fixture);

}

void test_text_output__raw_mode_aborts_when_consumer_stalls(void) {

    text_output_fixture* fixture = text_output_fixture_alloc();

    guac_terminal_text_output_open(fixture->term, "STDOUT", 1);
    fixture->term->text_output_inflight = GUAC_TERMINAL_TEXT_OUTPUT_MAX_INFLIGHT;

    guac_terminal_text_output_write(fixture->term, "abort", 5);

    CU_ASSERT_EQUAL(fixture->client->state, GUAC_CLIENT_STOPPING);
    CU_ASSERT_EQUAL(fixture->term->text_output_length, 0);
    CU_ASSERT_EQUAL(fixture->term->text_output_inflight,
            GUAC_TERMINAL_TEXT_OUTPUT_MAX_INFLIGHT);
    CU_ASSERT_EQUAL(fixture->last_log_level, GUAC_LOG_ERROR);

    guac_terminal_text_output_close(fixture->term);
    char* instructions = text_output_fixture_read(fixture);

    CU_ASSERT_PTR_NOT_NULL(strstr(instructions,
                "4.pipe,1.0,24.application/octet-stream,6.STDOUT;"));
    CU_ASSERT_PTR_NULL(strstr(instructions, "4.blob"));
    CU_ASSERT_PTR_NOT_NULL(strstr(instructions, "3.end,1.0;"));

    guac_mem_free(instructions);
    text_output_fixture_free(fixture);

}
