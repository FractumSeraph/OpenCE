/*
MBEDTLS_THREADS_CHECK.C

A check that Mbed TLS, as the builds make it (MBEDTLS_THREADING_C:
tools/linux_build.py MBEDTLS_DEFINES), takes several threads at once, as
the game has them: the game thread's DTLS with browsers
(port/linux/src/posix_dtls.c: its certificate made, handshakes with a DTLS
client here, records each way), while other threads use it as the game
list's and Delta Stats' requests, the downloads and a dedicated server's
control link do (port/linux/src/posix_browser.c, posix_update.c,
server/platform/control_tls.c): PSA crypto set up, its random generator
drawn on, keys made and destroyed (its key slots), P-256 keys made, and a
CTR-DRBG seeded from the entropy sources. None of them takes a lock of its
own. Built with ThreadSanitizer by tools/test_mbedtls_threads.py, which
fails on any race it sees (as it does without MBEDTLS_THREADING_C: the PSA
random generator's state, for one). Prints PASS or the failures.
*/

#include "posix.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/psa_util.h"
#include "mbedtls/ssl.h"
#include "psa/crypto.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum
{
	HANDSHAKES = 12,
	OTHER_THREADS = 3,
	MAXIMUM_DATAGRAM = 1500,
	QUEUE_LENGTH = 32,
};

static atomic_int game_done, failures;

static void fail(const char *what, int code)
{
	atomic_fetch_add(&failures, 1);
	printf("FAIL: %s (%d)\n", what, code);
}

/* ---------- the game thread: posix_dtls.c's server, with a client here */

struct queue
{
	int count, first;
	int sizes[QUEUE_LENGTH];
	unsigned char data[QUEUE_LENGTH][MAXIMUM_DATAGRAM];
};

/* (the client's datagrams to posix_dtls.c's end, and back) */
static struct queue to_client;

static int client_send(void *context, const unsigned char *data, size_t size)
{
	int handle = *(int *)context;

	posix_dtls_input(handle, data, (int)size);
	return (int)size;
}

static int client_receive(void *context, unsigned char *buffer, size_t size)
{
	int length;

	(void)context;
	if (!to_client.count)
		return MBEDTLS_ERR_SSL_WANT_READ;
	length = to_client.sizes[to_client.first];
	if ((size_t)length > size)
		length = (int)size;
	memcpy(buffer, to_client.data[to_client.first], (size_t)length);
	to_client.first = (to_client.first + 1) % QUEUE_LENGTH;
	to_client.count--;
	return length;
}

static void server_output(int handle)
{
	unsigned char datagram[MAXIMUM_DATAGRAM];
	int size;

	while ((size = posix_dtls_output(handle, datagram, sizeof(datagram))) > 0)
	{
		int index = (to_client.first + to_client.count) % QUEUE_LENGTH;

		if (to_client.count == QUEUE_LENGTH)
			continue;
		memcpy(to_client.data[index], datagram, (size_t)size);
		to_client.sizes[index] = size;
		to_client.count++;
	}
}

/* (no timers: nothing is lost here) */
static void timer_set(void *context, uint32_t intermediate, uint32_t final)
{
	(void)context;
	(void)intermediate;
	(void)final;
}

static int timer_get(void *context)
{
	(void)context;
	return 0;
}

static int one_connection(mbedtls_ssl_config *config, unsigned long *now)
{
	static const char ping[] = "a datagram from the browser", pong[] = "and one back";
	unsigned char buffer[2048];
	mbedtls_ssl_context client;
	int handle, steps, result, ok = 0, server_got = 0;

	memset(&to_client, 0, sizeof(to_client));
	handle = posix_dtls_open();
	if (handle < 0)
	{
		fail("posix_dtls_open", handle);
		return 0;
	}
	mbedtls_ssl_init(&client);
	if ((result = mbedtls_ssl_setup(&client, config)) != 0)
	{
		fail("the client's mbedtls_ssl_setup", result);
		goto done;
	}
	mbedtls_ssl_set_bio(&client, &handle, client_send, client_receive, NULL);
	mbedtls_ssl_set_timer_cb(&client, NULL, timer_set, timer_get);
	for (steps = 0; steps < 200; steps++)
	{
		result = mbedtls_ssl_handshake(&client);
		if (result != 0 && result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			fail("the client's handshake", result);
			goto done;
		}
		if (posix_dtls_receive(handle, (*now)++, buffer, sizeof(buffer)) < 0)
		{
			fail("posix_dtls.c's handshake", -1);
			goto done;
		}
		server_output(handle);
		if (result == 0)
			break;
	}
	if (result != 0)
	{
		fail("the handshake never ended", result);
		goto done;
	}
	if ((result = mbedtls_ssl_write(&client, (const unsigned char *)ping, sizeof(ping))) != (int)sizeof(ping))
	{
		fail("the client's record", result);
		goto done;
	}
	for (steps = 0; steps < 20 && !server_got; steps++)
	{
		result = posix_dtls_receive(handle, (*now)++, buffer, sizeof(buffer));
		if (result < 0)
		{
			fail("posix_dtls_receive", result);
			goto done;
		}
		if (result > 0)
		{
			if (result != (int)sizeof(ping) || memcmp(buffer, ping, sizeof(ping)))
			{
				fail("posix_dtls.c's end received something else", result);
				goto done;
			}
			server_got = 1;
		}
		server_output(handle);
	}
	if (!server_got || !posix_dtls_send(handle, pong, sizeof(pong)))
	{
		fail("posix_dtls.c's end did not take or answer the record", server_got);
		goto done;
	}
	server_output(handle);
	result = mbedtls_ssl_read(&client, buffer, sizeof(buffer));
	if (result != (int)sizeof(pong) || memcmp(buffer, pong, sizeof(pong)))
	{
		fail("the client's answer", result);
		goto done;
	}
	ok = 1;
done:
	mbedtls_ssl_free(&client);
	posix_dtls_close(handle);
	return ok;
}

static void *game_thread(void *unused)
{
	mbedtls_ssl_config config;
	unsigned char fingerprint[32];
	unsigned long now = 1;
	int connection;

	(void)unused;
	/* (its certificate made here, as the game thread makes it, while the
	others draw on the random generator) */
	if (!posix_dtls_fingerprint(fingerprint))
		fail("posix_dtls_fingerprint", 0);
	mbedtls_ssl_config_init(&config);
	if (mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_DATAGRAM,
			MBEDTLS_SSL_PRESET_DEFAULT) != 0)
	{
		fail("the client's configuration", 0);
	}
	else
	{
		mbedtls_ssl_conf_min_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
		mbedtls_ssl_conf_max_tls_version(&config, MBEDTLS_SSL_VERSION_TLS1_2);
		mbedtls_ssl_conf_rng(&config, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
		/* (a browser checks the certificate by its hash: p2p_webrtc.c) */
		mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_NONE);
		for (connection = 0; connection < HANDSHAKES && !atomic_load(&failures); connection++)
			one_connection(&config, &now);
	}
	mbedtls_ssl_config_free(&config);
	atomic_store(&game_done, 1);
	return NULL;
}

/* ---------- the other threads: HTTPS's uses of Mbed TLS */

static void *other_thread(void *argument)
{
	int index = (int)(long)argument;
	int round;

	for (round = 0; !atomic_load(&game_done) || round < 4; round++)
	{
		unsigned char random[64];
		psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
		mbedtls_svc_key_id_t key;
		psa_status_t status;

		/* (each request sets PSA crypto up first, as posix_browser.c's and
		posix_update.c's do) */
		if ((status = psa_crypto_init()) != PSA_SUCCESS)
		{
			fail("psa_crypto_init", (int)status);
			break;
		}
		if ((status = psa_generate_random(random, sizeof(random))) != PSA_SUCCESS)
			fail("psa_generate_random", (int)status);
		/* (a TLS 1.3 handshake's ECDHE key, in a key slot) */
		psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
		psa_set_key_bits(&attributes, 256);
		psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
		psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
		if ((status = psa_generate_key(&attributes, &key)) != PSA_SUCCESS)
			fail("psa_generate_key", (int)status);
		else
			psa_destroy_key(key);
		if (index == 0 && round % 4 == 0)
		{
			/* (control_tls.c's certificate key, from its own CTR-DRBG) */
			mbedtls_entropy_context entropy;
			mbedtls_ctr_drbg_context drbg;
			mbedtls_ecp_keypair pair;
			int result;

			mbedtls_entropy_init(&entropy);
			mbedtls_ctr_drbg_init(&drbg);
			mbedtls_ecp_keypair_init(&pair);
			if ((result = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0 ||
				(result = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &pair, mbedtls_ctr_drbg_random, &drbg)) != 0)
			{
				fail("a CTR-DRBG's P-256 key", result);
			}
			mbedtls_ecp_keypair_free(&pair);
			mbedtls_ctr_drbg_free(&drbg);
			mbedtls_entropy_free(&entropy);
		}
		else if (index == 1 && round % 4 == 0)
		{
			/* (the player key's first making, posix_browser.c) */
			mbedtls_ecp_keypair pair;
			int result;

			mbedtls_ecp_keypair_init(&pair);
			if ((result = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, &pair, mbedtls_psa_get_random,
					MBEDTLS_PSA_RANDOM_STATE)) != 0)
			{
				fail("a P-256 key from PSA's random generator", result);
			}
			mbedtls_ecp_keypair_free(&pair);
		}
	}
	return NULL;
}

int main(void)
{
	pthread_t game, others[OTHER_THREADS];
	long index;

	/* (all at once: the game thread's first use of PSA crypto, its
	certificate's making, races the others' psa_crypto_init) */
	for (index = 0; index < OTHER_THREADS; index++)
		pthread_create(&others[index], NULL, other_thread, (void *)index);
	pthread_create(&game, NULL, game_thread, NULL);
	pthread_join(game, NULL);
	for (index = 0; index < OTHER_THREADS; index++)
		pthread_join(others[index], NULL);
	if (atomic_load(&failures))
		return 1;
	printf("PASS\n");
	return 0;
}
