/**
 * @file robusto_misc_init.c
 * @author Nicklas Börjesson (<nicklasb at gmail dot com>)
 * @brief Functionality for generic handling of concurrency; tasks/threads and their synchronization.
 * @note This is for the most normal cases, for more advanced variants, look into using the platform specific variants
 * @version 0.1
 * @date 2023-02-19
 *
 * @copyright
 * Copyright (c) 2023, Nicklas Börjesson <nicklasb at gmail dot com>
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

#include <robusto_init_internal.h>
#include <robusto_init.h>
#ifdef CONFIG_ROBUSTO_PUBSUB_SERVER
#include <robusto_pubsub_server.h>
#endif
#ifdef CONFIG_ROBUSTO_UMTS_SERVER
#include <robusto_umts.h>
#endif

void robusto_misc_stop() {
   
}

void robusto_misc_start() {
    #ifdef CONFIG_ROBUSTO_PUBSUB_SERVER
    robusto_pubsub_server_start();
    #endif
    #ifdef CONFIG_ROBUSTO_PUBSUB_CLIENT
    //  Note that this has been deliberately commented is, that this probably has to be like this.
    //  robusto_pubsub_client_start();
    #endif
}


void robusto_misc_init(char * _log_prefix) {

    #ifdef CONFIG_ROBUSTO_PUBSUB_SERVER
    robusto_pubsub_server_init(_log_prefix);
    #endif
    
    #ifdef CONFIG_ROBUSTO_PUBSUB_CLIENT
    //  Note that this has been deliberately commented is, that this probably has to be like this.
    //  robusto_pubsub_client_init();
    #endif   
    #ifdef CONFIG_ROBUSTO_UMTS_SERVER
    robusto_umts_init(_log_prefix);
    #endif
}



void register_misc_service() {
    #if defined(CONFIG_ROBUSTO_PUBSUB_SERVER) || defined(CONFIG_ROBUSTO_UMTS_SERVER) 
    register_service(robusto_misc_init, robusto_misc_start, robusto_misc_stop, 2, "Miscellaneous");    
    #endif
}