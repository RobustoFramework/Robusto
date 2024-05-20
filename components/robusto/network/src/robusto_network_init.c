
/**
 * @file robusto_network_init.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Robusto networking initialization
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2022, Nicklas Börjesson <nicklasb at gmail dot com>
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <robusto_network_init.h>
#include <robusto_init_internal.h>
#include <robusto_init.h>
#include <robusto_logging.h>
#include <robusto_system.h>

void robusto_network_stop()
{
    robusto_media_stop();
}

void robusto_network_start()
{
    robusto_incoming_start();
    robusto_media_start();
    robusto_qos_start();


}


void robusto_network_init(char *_log_prefix)
{
    robusto_message_building_init(_log_prefix);
    robusto_message_sending_init(_log_prefix);
    robusto_message_parsing_init(_log_prefix);
    robusto_message_fragment_init(_log_prefix);
    robusto_peers_init(_log_prefix);
    robusto_incoming_init(_log_prefix);
    robusto_media_init(_log_prefix);
    robusto_qos_init(_log_prefix);

}

void register_network_service()
{
    register_service(robusto_network_init, robusto_network_start, robusto_network_stop, 2, "Network services");
}
