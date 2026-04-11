/*
 * TLS wrapper declarations for OscaWeb browser.
 * Called from Oscan via C-FFI.
 */

#ifndef TLS_WRAPPER_H
#define TLS_WRAPPER_H

#include "osc_runtime.h"
#include <stdint.h>

int32_t tls_init(void);
int32_t tls_connect_to(osc_str host, int32_t port);
int32_t tls_send_bytes(int32_t handle, osc_str data, int32_t data_len);
int32_t tls_recv_byte(int32_t handle);
void    tls_close_conn(int32_t handle);
void    tls_cleanup(void);

#endif /* TLS_WRAPPER_H */
