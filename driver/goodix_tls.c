/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Goodix GM168 -- OpenSSL TLS-PSK server
 *
 * Fix log (vs initial):
 *   [FIX-1] O_NONBLOCK on client_fd -- tls_pull never blocks the main thread
 *   [FIX-2] Correct order in TLS loop: USB read -> feed -> spinloop pull
 *   [FIX-3] Explicit pthread_join in deinit -- no thread leak
 *   [FIX-4] No mutex needed: SSL thread and driver thread use *different* fds
 *           of the socketpair; after SSL_accept the thread exits, so
 *           SSL_read/SSL_write run single-threaded from the GLib main loop.
 *   [G9-A]  SO_RCVTIMEO on sock_fd: SSL_accept can't hang forever even if
 *           the driver dies between TLS_START and the first handshake feed.
 *           Addresses AUDIT C2.
 *   [G9-B]  cancel_requested + shutdown(sock_fd): dev_deactivate can unblock
 *           a stuck SSL_accept immediately instead of waiting for the next
 *           dev_activate to tear down the thread. Addresses AUDIT C1.
 *   [G9-C]  SSL_read busy-loop shrunk from 50x5ms (250ms blocking main
 *           thread) to 5x1ms (5ms). Addresses AUDIT H10.
 */

#define FP_COMPONENT "goodix_gm168"

#include "goodix_tls.h"
#include "goodix_proto.h"
#include "drivers_api.h"

#include <errno.h>
#include <fcntl.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>      /* nanosleep */
#include <unistd.h>

// ─── Internal helpers ─────────────────────────────────────────────────────────

static GError *
err_from_ssl (void)
{
    // Drain the WHOLE OpenSSL error queue, not just the first error —
    // PSK / cipher failures usually surface as a stack (e.g. "bad
    // record mac" + "cipher operation failed"). The first-only path
    // hides the root cause.
    GString *all = g_string_new (NULL);
    unsigned long first_code = 0;
    unsigned long code;
    char buf[256];
    int  count = 0;

    while ((code = ERR_get_error ()) != 0) {
        if (first_code == 0)
            first_code = code;
        ERR_error_string_n (code, buf, sizeof (buf));
        if (count > 0)
            g_string_append (all, " | ");
        g_string_append (all, buf);
        count++;
    }
    if (count == 0)
        g_string_assign (all, "unknown SSL error (queue empty)");
    GError *err = g_error_new_literal (G_FILE_ERROR, (gint)first_code, all->str);
    g_string_free (all, TRUE);
    return err;
}

// ─── PSK callback ─────────────────────────────────────────────────────────────
//
// The MCU (client) sends identity="Client_identity" and expects our PSK.
static unsigned int
psk_server_cb (SSL *ssl, const char *identity, unsigned char *psk,
               unsigned int max_psk_len)
{
    (void)ssl;
    (void)identity;  /* MCU always sends "Client_identity" */

    /*
     * goodix_gm168_psk holds the 32-byte device-specific PSK that the
     * MCU was provisioned with at the factory (or by the Windows driver
     * during install).  We loaded it at dev_activate from
     * <dir>/psk.bin (see gm168_load_psk_from_file).  Rewriting the PSK
     * requires an active TLS session or the IAP service mode, so for
     * normal operation we just use what's already on the device.
     */
    unsigned int psk_len = (unsigned int)sizeof (goodix_gm168_psk);

    if (psk_len > max_psk_len)
        return 0;

    memcpy (psk, goodix_gm168_psk, psk_len);
    return psk_len;
}

// ─── SSL context setup ────────────────────────────────────────────────────────

static SSL_CTX *
create_ctx (void)
{
    SSL_CTX *ctx = SSL_CTX_new (TLS_server_method ());
    if (!ctx)
        return NULL;

    // TLS 1.2 only — the version GM168 uses (verified in Binary Ninja).
    SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version (ctx, TLS1_2_VERSION);

    // OpenSSL 3.0+ default security level is 2, which rejects PSK-CBC
    // suites as "legacy". The sensor only speaks PSK-AES128-CBC-SHA256,
    // so we must drop the level. SECLEVEL=0 permits everything (we
    // control the cipher list above, so this is safe).
    SSL_CTX_set_security_level (ctx, 0);

    // The GM168 firmware predates RFC 7627 (Extended Master Secret) and
    // RFC 5746 (secure renegotiation). OpenSSL 3.0+ defaults to enforcing
    // EMS and refusing legacy renegotiation, which derives different
    // master-secret keys than the sensor expects and surfaces as
    // "decryption failed or bad record mac" on the very first Finished.
    // Disable both — confirmed-correct PSK + legacy KDF matches the MCU.
    SSL_CTX_set_options (ctx,
                         SSL_OP_NO_EXTENDED_MASTER_SECRET |
                         SSL_OP_NO_ENCRYPT_THEN_MAC |
                         SSL_OP_NO_RENEGOTIATION |
                         SSL_OP_NO_TICKET |
                         SSL_OP_LEGACY_SERVER_CONNECT |
                         SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION);

    // The GM168 firmware is TLS-1.0-era retrofitted to TLS 1.2 PSK. Its
    // ClientHello carries NO extensions block, but advertises secure
    // renegotiation via the SCSV pseudo-cipher 0x00FF. RFC 5746 then
    // requires us to echo an empty renegotiation_info extension in the
    // ServerHello — which the sensor's firmware does NOT account for in
    // its handshake transcript hash, breaking Finished verify_data.
    //
    // SSL_OP_NO_RENEGOTIATION + SSL_OP_NO_TICKET together convince
    // OpenSSL 3.x to omit the ServerHello extensions block when the
    // ClientHello had none, restoring transcript symmetry.
    // NB: SSL_OP_NO_ENCRYPT_THEN_MAC is the load-bearing flag here.
    // OpenSSL 3.x defaults to the RFC 7366 Encrypt-then-MAC extension for
    // CBC suites via a fused AES-CBC-HMAC-SHA EtM cipher implementation,
    // but the GM168 firmware does legacy MAC-then-Encrypt. The mismatch
    // makes the very first record fail with the queue
    //   1C800066 Provider routines::cipher operation failed
    //   0A000119 SSL routines::decryption failed or bad record mac
    //   0A000139 SSL routines::record layer failure
    // which is exactly what we saw before disabling EtM.

    // IMPORTANT: CBC-SHA256, NOT GCM!
    if (SSL_CTX_set_cipher_list (ctx, GOODIX_GM168_TLS_CIPHER) != 1) {
        g_warning ("goodix-gm168: failed to set cipher list: %s",
                   GOODIX_GM168_TLS_CIPHER);
        SSL_CTX_free (ctx);
        return NULL;
    }

    SSL_CTX_set_psk_server_callback (ctx, psk_server_cb);
    return ctx;
}

// ─── TLS server thread ────────────────────────────────────────────────────────
//
// The thread blocks inside SSL_accept.  Data flows over the socketpair:
//   driver → write(client_fd) → kernel → SSL thread reads from sock_fd
//   SSL thread writes to sock_fd → kernel → driver reads from client_fd
// This is fully thread-safe: write/read happen on DIFFERENT ends of the
// socketpair.  A mutex would only be needed if we had to make several
// feed+pull operations atomic — we don't.

typedef struct {
    GoodixGM168TlsServer *server;
    GError               *error;
} TlsCompletion;

static void
tls_completion_free (gpointer user_data)
{
    TlsCompletion *completion = user_data;

    g_clear_error (&completion->error);
    g_free (completion);
}

static gboolean
tls_complete_on_owner_context (gpointer user_data)
{
    TlsCompletion *completion = user_data;
    GoodixGM168TlsServer *self = completion->server;
    GSource *source;
    gboolean deliver;

    g_mutex_lock (&self->state_lock);
    source = self->completion_source;
    self->completion_source = NULL;
    deliver = !self->cancel_requested && !self->completion_delivered;
    if (deliver)
        self->completion_delivered = TRUE;
    g_mutex_unlock (&self->state_lock);

    if (source)
        g_source_unref (source);

    if (!deliver)
        return G_SOURCE_REMOVE;

    if (completion->error) {
        g_warning ("goodix-gm168: SSL_accept failed: %s",
                   completion->error->message);
    } else {
        int flags = fcntl (self->sock_fd, F_GETFL, 0);
        if (flags >= 0)
            fcntl (self->sock_fd, F_SETFL, flags | O_NONBLOCK);
        g_debug ("goodix-gm168: TLS handshake complete");
    }

    self->on_connected (self, completion->error, self->user_data);
    return G_SOURCE_REMOVE;
}

static void *
tls_serve_thread (void *arg)
{
    GoodixGM168TlsServer *self = arg;
    TlsCompletion *completion = g_new0 (TlsCompletion, 1);
    GSource *source = g_idle_source_new ();
    gboolean queue = FALSE;
    int ret;

    completion->server = self;
    ret = SSL_accept (self->ssl);
    if (ret <= 0)
        completion->error = err_from_ssl ();

    g_source_set_callback (source, tls_complete_on_owner_context,
                           completion, tls_completion_free);

    g_mutex_lock (&self->state_lock);
    if (!self->cancel_requested && !self->completion_queued) {
        self->completion_queued = TRUE;
        self->completion_source = g_source_ref (source);
        g_source_attach (source, self->owner_context);
        queue = TRUE;
    }
    g_mutex_unlock (&self->state_lock);

    if (!queue)
        g_source_destroy (source);
    g_source_unref (source);
    return NULL;
}

// ─── Public API ───────────────────────────────────────────────────────────────

gboolean
goodix_gm168_tls_init (GoodixGM168TlsServer *self, GError **error)
{
    g_assert (self);
    g_assert (self->on_connected);

    self->ctx = NULL;
    self->ssl = NULL;
    self->sock_fd = -1;
    self->client_fd = -1;
    self->owner_context = g_main_context_ref_thread_default ();
    self->completion_source = NULL;
    self->thread_started = FALSE;
    self->cancel_requested = FALSE;
    self->completion_queued = FALSE;
    self->completion_delivered = FALSE;
    self->feed_pending = g_byte_array_new ();
    self->feed_offset = 0;
    g_mutex_init (&self->state_lock);
    self->sync_initialized = TRUE;

    // Init OpenSSL
    SSL_load_error_strings ();
    OpenSSL_add_ssl_algorithms ();

    self->ctx = create_ctx ();
    if (!self->ctx) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "goodix-gm168: failed to create SSL context");
        g_main_context_unref (self->owner_context);
        self->owner_context = NULL;
        g_clear_pointer (&self->feed_pending, g_byte_array_unref);
        g_mutex_clear (&self->state_lock);
        self->sync_initialized = FALSE;
        return FALSE;
    }

    // socketpair: sock_fd ↔ client_fd
    int fds[2];
    if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        g_set_error (error, G_FILE_ERROR, errno,
                     "goodix-gm168: socketpair failed: %s", strerror (errno));
        SSL_CTX_free (self->ctx);
        self->ctx = NULL;
        g_main_context_unref (self->owner_context);
        self->owner_context = NULL;
        g_clear_pointer (&self->feed_pending, g_byte_array_unref);
        g_mutex_clear (&self->state_lock);
        self->sync_initialized = FALSE;
        return FALSE;
    }
    self->sock_fd   = fds[0];
    self->client_fd = fds[1];

    /*
     * [FIX-1] client_fd: set O_NONBLOCK so tls_pull() never stalls the main
     * thread when OpenSSL has not produced output yet (returns EAGAIN instead).
     * sock_fd remains blocking during SSL_accept, and will be switched to
     * non-blocking afterwards.
     */
    {
        int flags = fcntl (self->client_fd, F_GETFL, 0);
        if (flags >= 0) fcntl (self->client_fd, F_SETFL, flags | O_NONBLOCK);
    }

    /* Create SSL object and bind to sock_fd */
    self->ssl = SSL_new (self->ctx);
    if (!self->ssl) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "goodix-gm168: SSL_new failed");
        close (self->sock_fd);
        close (self->client_fd);
        self->sock_fd = -1;
        self->client_fd = -1;
        SSL_CTX_free (self->ctx);
        self->ctx = NULL;
        g_main_context_unref (self->owner_context);
        self->owner_context = NULL;
        g_clear_pointer (&self->feed_pending, g_byte_array_unref);
        g_mutex_clear (&self->state_lock);
        self->sync_initialized = FALSE;
        return FALSE;
    }
    SSL_set_fd (self->ssl, self->sock_fd);

    /* [G9-A] Bound SSL_accept by giving the underlying recv() a wall-clock
     * cap. If the driver stops feeding handshake bytes mid-flow, recv()
     * returns EAGAIN after this timeout; the serve thread treats that as
     * "handshake hung" and exits with an error instead of hanging
     * indefinitely. 5 seconds is ~100x the observed handshake time
     * (~50 ms total, ~13 ms for the busiest step in our trace data). */
    {
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        if (setsockopt (self->sock_fd, SOL_SOCKET, SO_RCVTIMEO,
                        &tv, sizeof (tv)) != 0) {
            g_warning ("goodix-gm168: SO_RCVTIMEO on sock_fd failed: %s",
                       strerror (errno));
            /* Non-fatal: cancel via shutdown() still works. */
        }
    }

    int thread_error = pthread_create (&self->thread, NULL,
                                       tls_serve_thread, self);
    if (thread_error != 0) {
        g_set_error (error, G_FILE_ERROR, thread_error,
                     "goodix-gm168: pthread_create failed: %s",
                     strerror (thread_error));
        SSL_free (self->ssl);
        self->ssl = NULL;
        close (self->sock_fd);
        close (self->client_fd);
        self->sock_fd = -1;
        self->client_fd = -1;
        SSL_CTX_free (self->ctx);
        self->ctx = NULL;
        g_main_context_unref (self->owner_context);
        self->owner_context = NULL;
        g_clear_pointer (&self->feed_pending, g_byte_array_unref);
        g_mutex_clear (&self->state_lock);
        self->sync_initialized = FALSE;
        return FALSE;
    }
    self->thread_started = TRUE;
    return TRUE;
}

// Flush bytes already accepted by goodix_gm168_tls_feed() without ever
// waiting in the owner context. A short socket write advances feed_offset;
// EAGAIN leaves the exact remainder queued for the next feed/recv call.
static int
flush_feed_pending (GoodixGM168TlsServer *self)
{
    while (self->feed_offset < self->feed_pending->len) {
        ssize_t written;

        g_mutex_lock (&self->state_lock);
        if (self->cancel_requested || self->client_fd < 0) {
            g_mutex_unlock (&self->state_lock);
            errno = ECANCELED;
            return -1;
        }
        written = send (self->client_fd,
                        self->feed_pending->data + self->feed_offset,
                        self->feed_pending->len - self->feed_offset,
                        MSG_NOSIGNAL);
        g_mutex_unlock (&self->state_lock);

        if (written > 0) {
            self->feed_offset += (gsize) written;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 &&
            (errno == EAGAIN
#if EWOULDBLOCK != EAGAIN
             || errno == EWOULDBLOCK
#endif
            ))
            return 0;

        return -1;
    }

    g_byte_array_set_size (self->feed_pending, 0);
    self->feed_offset = 0;
    return 1;
}

// Hand raw TLS bytes from the MCU (out of a B0/B2 packet) to OpenSSL.
// The owner context never waits for socket capacity: complete and partial
// writes are drained immediately, while any remainder is retained in order.
int
goodix_gm168_tls_feed (GoodixGM168TlsServer *self, const guint8 *data,
                        guint32 length)
{
    g_byte_array_append (self->feed_pending, data, length);

    if (flush_feed_pending (self) < 0) {
        fp_warn ("goodix-gm168: tls_feed failed with %zu bytes pending: %s",
                 self->feed_pending->len - self->feed_offset,
                 strerror (errno));
        return -1;
    }

    return (int) length;
}

// Pull TLS bytes that OpenSSL wants to send to the MCU.
// O_NONBLOCK: returns -1/EAGAIN when no data is available (non-blocking).
// Call in a loop with 1-5 ms sleep until data arrives or timeout.
int
goodix_gm168_tls_pull (GoodixGM168TlsServer *self, guint8 *buf, guint16 size)
{
    ssize_t n = read (self->client_fd, buf, size);
    if (n < 0 && errno == EAGAIN)
        return 0;   // no data yet — not an error
    if (n < 0) {
        fp_warn ("goodix-gm168: tls_pull read error: %s (errno=%d)", strerror(errno), errno);
        return 0;
    }

    return (int)n;
}

// Read decrypted data from the MCU (after a successful handshake).
// Must only be called after goodix_gm168_tls_feed().
int
goodix_gm168_tls_recv (GoodixGM168TlsServer *self, guint8 *buf, guint32 size,
                        GError **error)
{
    /*
     * SSL_read can return SSL_ERROR_WANT_READ immediately after a
     * goodix_gm168_tls_feed() if the bytes haven't yet propagated
     * through the socketpair into OpenSSL's read buffer.
     *
     * [G9-C] Shrunk from 50x5ms (250ms blocking the main thread) to
     * 5x1ms (5ms cap). On WANT_READ after the cap, return 0 — the caller
     * loops back through CAP_RX which fetches more bytes from USB and
     * calls us again. Socketpair latency is <1ms in practice, so 5x1ms
     * still covers the legitimate case with margin.
     */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };  /* 1 ms */
    for (int attempt = 0; attempt < 5; attempt++) {
        if (flush_feed_pending (self) < 0) {
            if (error)
                *error = g_error_new (G_IO_ERROR, g_io_error_from_errno (errno),
                                      "TLS input feed failed: %s",
                                      strerror (errno));
            return -1;
        }

        int ret = SSL_read (self->ssl, buf, (int)size);
        if (ret > 0)
            return ret;
        int ssl_err = SSL_get_error (self->ssl, ret);
        if (ssl_err == SSL_ERROR_WANT_READ) {
            nanosleep (&ts, NULL);
            continue;
        }
        if (ssl_err == SSL_ERROR_ZERO_RETURN)
            return 0;

        if (error)
            *error = err_from_ssl ();
        return ret;
    }
    
    // WANT_READ timeout — let the caller fetch more bytes and retry.
    return 0;
}

void
goodix_gm168_tls_cancel (GoodixGM168TlsServer *self)
{
    if (!self || !self->sync_initialized)
        return;

    g_mutex_lock (&self->state_lock);
    if (!self->cancel_requested) {
        self->cancel_requested = TRUE;
        if (self->sock_fd >= 0)
            shutdown (self->sock_fd, SHUT_RDWR);
    }
    g_mutex_unlock (&self->state_lock);
}

// Send data to the MCU (OpenSSL encrypts and writes to sock_fd).
int
goodix_gm168_tls_send (GoodixGM168TlsServer *self, const guint8 *data,
                        guint16 length)
{
    return SSL_write (self->ssl, data, length);
}

void
goodix_gm168_tls_deinit (GoodixGM168TlsServer *self)
{
    GSource *completion_source;

    if (!self || !self->sync_initialized)
        return;

    g_mutex_lock (&self->state_lock);
    self->cancel_requested = TRUE;
    if (self->sock_fd >= 0)
        shutdown (self->sock_fd, SHUT_RDWR);
    completion_source = self->completion_source;
    self->completion_source = NULL;
    g_mutex_unlock (&self->state_lock);

    if (completion_source) {
        g_source_destroy (completion_source);
        g_source_unref (completion_source);
    }

    if (self->thread_started) {
        pthread_join (self->thread, NULL);
        self->thread_started = FALSE;
    }

    g_mutex_lock (&self->state_lock);
    if (self->sock_fd >= 0) {
        close (self->sock_fd);
        self->sock_fd = -1;
    }
    if (self->client_fd >= 0) {
        close (self->client_fd);
        self->client_fd = -1;
    }
    g_mutex_unlock (&self->state_lock);

    if (self->ssl) {
        SSL_free (self->ssl);
        self->ssl = NULL;
    }
    if (self->ctx) {
        SSL_CTX_free (self->ctx);
        self->ctx = NULL;
    }
    if (self->owner_context) {
        g_main_context_unref (self->owner_context);
        self->owner_context = NULL;
    }
    g_clear_pointer (&self->feed_pending, g_byte_array_unref);
    self->feed_offset = 0;

    self->sync_initialized = FALSE;
    g_mutex_clear (&self->state_lock);
}
